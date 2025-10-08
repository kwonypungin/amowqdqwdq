#include "EngineBridge.hpp"
#include <QDateTime>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageAuthenticationCode>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>
#include <QScopeGuard>
#include <QWebSocket>
#include <QAbstractSocket>
#include <QtConcurrent/QtConcurrent>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcBridge, "engine.bridge")

namespace {
constexpr int kTickerBatchSize = 15;
constexpr int kTopCandidates = 10;
constexpr int kCandlesLookback5m = 120;
constexpr int kCandlesLookback1m = 60;
constexpr int kRealtimeEmitIntervalMs = 1'000;

double jsonToDouble(const QJsonValue& value) {
    if (value.isString()) return value.toString().toDouble();
    return value.toDouble();
}

qint64 jsonToTimestampMs(const QJsonValue& value) {
    if (value.isString()) {
        bool ok = false;
        qint64 v = value.toString().toLongLong(&ok);
        if (ok) return v;
    }
    return value.toVariant().toLongLong();
}
}

EngineBridge::EngineBridge(QString access, QString secret, QObject* parent)
    : QObject(parent), access_(std::move(access)), secret_(std::move(secret)), restClient_() {
    net_ = new QNetworkAccessManager(this);
    connect(&timer_, &QTimer::timeout, this, &EngineBridge::onFiveMinuteTick);

    restClient_.set_credentials(access_.toStdString(), secret_.toStdString());

    wsPublic_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    wsPrivate_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(wsPublic_, &QWebSocket::connected, this, &EngineBridge::onPublicWsConnected);
    connect(wsPrivate_, &QWebSocket::connected, this, &EngineBridge::onPrivateWsConnected);
    connect(wsPublic_, &QWebSocket::disconnected, this, &EngineBridge::onPublicWsClosed);
    connect(wsPrivate_, &QWebSocket::disconnected, this, &EngineBridge::onPrivateWsClosed);
    connect(wsPublic_, &QWebSocket::textMessageReceived, this, &EngineBridge::onPublicTextMessage);
    connect(wsPublic_, &QWebSocket::binaryMessageReceived, this, &EngineBridge::onPublicBinaryMessage);
    connect(wsPrivate_, &QWebSocket::textMessageReceived, this, &EngineBridge::onPrivateTextMessage);
    connect(wsPrivate_, &QWebSocket::binaryMessageReceived, this, &EngineBridge::onPrivateBinaryMessage);

    wsReconnectTimer_.setInterval(5'000);
    wsReconnectTimer_.setSingleShot(false);
    connect(&wsReconnectTimer_, &QTimer::timeout, this, &EngineBridge::ensureSockets);

    heartbeatTimer_.setInterval(15'000);
    heartbeatTimer_.setSingleShot(false);
    connect(&heartbeatTimer_, &QTimer::timeout, this, [this]() {
        if (wsPublic_ && wsPublicConnected_) wsPublic_->ping();
        if (wsPrivate_ && wsPrivateConnected_) wsPrivate_->ping();
    });
}

void EngineBridge::start() {
    fetchMarkets();
    timer_.start(30'000); // 30초마다 신규 데이터 확인
    ensureSockets();
    wsReconnectTimer_.start();
    heartbeatTimer_.start();
}

void EngineBridge::onFiveMinuteTick() {
    if (!selectionReady_ || market_.isEmpty()) return;
    fetchCandles5m();
}

void EngineBridge::fetchMarkets() {
    if (pending_) return;
    QUrl url("https://api.upbit.com/v1/market/all");
    QUrlQuery query;
    query.addQueryItem("isDetails", "false");
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "UpbitTrader/1.0");
    pendingKind_ = RequestKind::Markets;
    pendingMarket_.clear();
    pendingUnit_ = 0;
    pending_ = net_->get(req);
    connect(pending_, &QNetworkReply::finished, this, &EngineBridge::onNetworkReply);
}

void EngineBridge::fetchNextTickerChunk() {
    if (pending_) return;
    if (marketsKRW_.isEmpty()) {
        market_ = QStringLiteral("KRW-BTC");
        selectionReady_ = true;
        emit marketChanged(market_);
        if (subscribedMarket_ != market_) {
            subscribedMarket_ = market_;
            subscribePublic(market_);
            subscribePrivate(market_);
        }
        fetchCandles5m();
        return;
    }

    if (nextTickerIndex_ >= marketsKRW_.size()) {
        QList<QString> ordered = volume24h_.keys();
        std::sort(ordered.begin(), ordered.end(), [&](const QString& a, const QString& b) {
            return volume24h_.value(a) > volume24h_.value(b);
        });
        candidateQueue_.clear();
        const int limit = std::min(kTopCandidates, static_cast<int>(ordered.size()));
        for (int i = 0; i < limit; ++i) candidateQueue_.append(ordered.at(i));
        candidateIndex_ = 0;
        bestMarket_.clear();
        bestScore_ = -std::numeric_limits<double>::infinity();
        evaluateNextCandidate();
        return;
    }

    QStringList chunk;
    for (int i = 0; i < kTickerBatchSize && nextTickerIndex_ < marketsKRW_.size(); ++i) {
        chunk << marketsKRW_.at(nextTickerIndex_++);
    }

    if (chunk.isEmpty()) {
        evaluateNextCandidate();
        return;
    }

    QUrl url("https://api.upbit.com/v1/ticker");
    QUrlQuery query;
    query.addQueryItem("markets", chunk.join(","));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "UpbitTrader/1.0");
    pendingKind_ = RequestKind::Tickers;
    pendingMarket_.clear();
    pendingUnit_ = 0;
    pending_ = net_->get(req);
    connect(pending_, &QNetworkReply::finished, this, &EngineBridge::onNetworkReply);
}

void EngineBridge::evaluateNextCandidate() {
    if (pending_) return;
    if (candidateIndex_ >= candidateQueue_.size() || candidateQueue_.isEmpty()) {
        if (bestMarket_.isEmpty()) {
            if (!candidateQueue_.isEmpty()) bestMarket_ = candidateQueue_.first();
            else if (!marketsKRW_.isEmpty()) bestMarket_ = marketsKRW_.first();
            else bestMarket_ = QStringLiteral("KRW-BTC");
        }
        market_ = bestMarket_;
        selectionReady_ = true;
        emit marketChanged(market_);
        if (subscribedMarket_ != market_) {
            subscribedMarket_ = market_;
            subscribePublic(market_);
            subscribePrivate(market_);
        }
        fetchCandles5m();
        return;
    }

    const QString market = candidateQueue_.at(candidateIndex_++);
    fetchCandles(1, kCandlesLookback1m, RequestKind::Candles1m, market);
}

void EngineBridge::fetchCandles(int unit, int count, RequestKind kind, const QString& market) {
    if (pending_) return;
    QUrl url(QStringLiteral("https://api.upbit.com/v1/candles/minutes/%1").arg(unit));
    QUrlQuery query;
    query.addQueryItem("market", market);
    query.addQueryItem("count", QString::number(count));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "UpbitTrader/1.0");
    pendingKind_ = kind;
    pendingMarket_ = market;
    pendingUnit_ = unit;
    pending_ = net_->get(req);
    connect(pending_, &QNetworkReply::finished, this, &EngineBridge::onNetworkReply);
}

void EngineBridge::fetchCandles5m() {
    if (market_.isEmpty()) return;
    fetchCandles(5, kCandlesLookback5m, RequestKind::Candles5m, market_);
}

void EngineBridge::ensureSockets() {
    if (wsPublic_ && wsPublic_->state() == QAbstractSocket::UnconnectedState) {
        connectPublicSocket();
    }
    if (!access_.isEmpty() && !secret_.isEmpty() && wsPrivate_ && wsPrivate_->state() == QAbstractSocket::UnconnectedState) {
        connectPrivateSocket();
    }
}

void EngineBridge::connectPublicSocket() {
    if (!wsPublic_) return;
    wsPublicConnected_ = false;
    wsPublic_->open(QUrl(QStringLiteral("wss://api.upbit.com/websocket/v1")));
}

void EngineBridge::connectPrivateSocket() {
    if (!wsPrivate_) return;
    wsPrivateConnected_ = false;
    wsPrivate_->open(QUrl(QStringLiteral("wss://api.upbit.com/websocket/v1")));
}

void EngineBridge::subscribePublic(const QString& market) {
    if (!wsPublicConnected_ || !wsPublic_ || market.isEmpty()) return;
    QJsonArray arr;
    arr.append(QJsonObject{{"ticket", QStringLiteral("ui-public")}});
    arr.append(QJsonObject{{"type", QStringLiteral("trade")}, {"codes", QJsonArray{market}}});
    arr.append(QJsonObject{{"type", QStringLiteral("orderbook")}, {"codes", QJsonArray{market}}, {"isOnlyRealtime", true}});
    wsPublic_->sendBinaryMessage(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void EngineBridge::subscribePrivate(const QString& market) {
    if (!wsPrivateConnected_ || !wsPrivate_ || market.isEmpty()) return;
    const QByteArray token = authToken();
    if (token.isEmpty()) {
        qCWarning(lcBridge) << "Private WS auth token empty";
        return;
    }
    QJsonArray arr;
    arr.append(QJsonObject{{"ticket", QStringLiteral("ui-private")}});
    arr.append(QJsonObject{{"type", QStringLiteral("myOrders")}, {"codes", QJsonArray{market}}, {"isOnlyRealtime", true}});
    arr.append(QJsonObject{{"authorization", QString::fromUtf8(token)}});
    wsPrivate_->sendBinaryMessage(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void EngineBridge::onPublicWsConnected() {
    wsPublicConnected_ = true;
    if (!subscribedMarket_.isEmpty()) subscribePublic(subscribedMarket_);
}

void EngineBridge::onPrivateWsConnected() {
    wsPrivateConnected_ = true;
    if (!subscribedMarket_.isEmpty()) subscribePrivate(subscribedMarket_);
}

void EngineBridge::onPublicWsClosed() {
    wsPublicConnected_ = false;
    QTimer::singleShot(2'000, this, [this]() { ensureSockets(); });
}

void EngineBridge::onPrivateWsClosed() {
    wsPrivateConnected_ = false;
    if (!access_.isEmpty() && !secret_.isEmpty()) {
        QTimer::singleShot(2'000, this, [this]() { ensureSockets(); });
    }
}

void EngineBridge::onPublicTextMessage(const QString& message) {
    handlePublicMessage(message.toUtf8());
}

void EngineBridge::onPublicBinaryMessage(const QByteArray& message) {
    handlePublicMessage(message);
}

void EngineBridge::onPrivateTextMessage(const QString& message) {
    handlePrivateMessage(message.toUtf8());
}

void EngineBridge::onPrivateBinaryMessage(const QByteArray& message) {
    handlePrivateMessage(message);
}

void EngineBridge::handlePublicMessage(const QByteArray& payload) {
    if (payload.isEmpty()) return;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError) {
        const char first = payload.at(0);
        if (first != '{' && first != '[') return;
        qCWarning(lcBridge) << "Failed to parse public WS payload" << err.errorString();
        return;
    }
    if (!doc.isObject()) return;
    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();
    if (type == QLatin1String("trade")) processTradeMessage(obj);
    else if (type == QLatin1String("orderbook")) processOrderbookMessage(obj);
}

void EngineBridge::handlePrivateMessage(const QByteArray& payload) {
    if (payload.isEmpty()) return;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError) {
        const char first = payload.at(0);
        if (first != '{' && first != '[') return;
        qCWarning(lcBridge) << "Failed to parse private WS payload" << err.errorString();
        return;
    }
    if (!doc.isObject()) return;
    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();
    if (type == QLatin1String("myOrder") || type == QLatin1String("myOrders")) {
        processMyOrderMessage(obj);
    }
}

void EngineBridge::processTradeMessage(const QJsonObject& obj) {
    if (c5_.empty()) return;
    const QString code = obj.value("code").toString();
    if (!code.isEmpty() && !market_.isEmpty() && code != market_) return;
    const double price = jsonToDouble(obj.value("trade_price"));
    const double volume = jsonToDouble(obj.value("trade_volume"));
    const qint64 ts = jsonToTimestampMs(obj.value("trade_timestamp"));
    if (price <= 0.0 || ts <= 0) return;

    Candle& last = c5_.back();
    if (ts < last.ts_ms) return;
    const qint64 windowMs = 5LL * 60LL * 1000LL;
    if (ts - last.ts_ms >= windowMs) {
        Candle next{};
        next.ts_ms = ts;
        next.open = last.close;
        next.close = price;
        next.high = std::max(next.open, price);
        next.low = std::min(next.open, price);
        next.volume = volume;
        c5_.push_back(next);
        if (c5_.size() > static_cast<size_t>(kCandlesLookback5m)) {
            c5_.erase(c5_.begin());
        }
    } else {
        last.close = price;
        last.high = std::max(last.high, price);
        last.low = std::min(last.low, price);
        last.volume += volume;
    }
    scheduleRealtimeEmit();
}

void EngineBridge::processOrderbookMessage(const QJsonObject& obj) {
    const QJsonArray units = obj.value("orderbook_units").toArray();
    if (units.isEmpty()) return;
    const QJsonObject top = units.first().toObject();
    bestBid_ = jsonToDouble(top.value("bid_price"));
    bestAsk_ = jsonToDouble(top.value("ask_price"));
}

void EngineBridge::processMyOrderMessage(const QJsonObject& obj) {
    const QString uuid = obj.value("uuid").toString();
    const QString sideStr = obj.value("side").toString();
    const bool isBuy = sideStr.compare(QStringLiteral("bid"), Qt::CaseInsensitive) == 0;
    double tradePrice = jsonToDouble(obj.value("trade_price"));
    double tradeVolume = jsonToDouble(obj.value("trade_volume"));
    qint64 tradeTs = jsonToTimestampMs(obj.value("trade_timestamp"));

    const auto handleFill = [this, isBuy, &uuid](double price, double volume, qint64 ts) {
        if (price <= 0.0 || volume <= 0.0) return;
        if (ts <= 0) ts = QDateTime::currentMSecsSinceEpoch();
        emit orderExecuted(market_, ts, price, isBuy);
        updatePosition(isBuy, price, volume, ts);
        auto it = pendingOrders_.find(uuid);
        if (it != pendingOrders_.end()) {
            PendingOrder& ctx = it.value();
            const double prevFilled = ctx.filledVolume;
            ctx.filledVolume += volume;
            if (ctx.filledVolume > 0.0) {
                ctx.weightedFillPrice = ((ctx.weightedFillPrice * prevFilled) + price * volume) / ctx.filledVolume;
            }
            const double reference = ctx.isBuy
                    ? (ctx.bestAskAtSubmit > 0.0 ? ctx.bestAskAtSubmit : ctx.price)
                    : (ctx.bestBidAtSubmit > 0.0 ? ctx.bestBidAtSubmit : ctx.price);
            if (reference > 0.0) {
                const double slipAbs = ctx.isBuy ? price - reference : reference - price;
                const double slipBps = (slipAbs / reference) * 10'000.0;
                qCInfo(lcBridge) << "order" << uuid
                                 << "fill" << volume
                                 << "@" << price
                                 << "slippage" << slipAbs
                                 << "(" << slipBps << "bps)";
            }
            const double fillRate = ctx.volume > 0.0 ? ctx.filledVolume / ctx.volume : 1.0;
            qCInfo(lcBridge) << "order" << uuid << "fill-rate" << fillRate;
        }
    };

    if (tradeVolume > 0.0 && tradePrice > 0.0) {
        handleFill(tradePrice, tradeVolume, tradeTs);
    } else if (obj.contains("trades")) {
        const QJsonArray trades = obj.value("trades").toArray();
        for (const QJsonValue& v : trades) {
            const QJsonObject t = v.toObject();
            const double px = jsonToDouble(t.value("trade_price"));
            const double vol = jsonToDouble(t.value("trade_volume"));
            const qint64 ts = jsonToTimestampMs(t.value("trade_timestamp"));
            handleFill(px, vol, ts);
        }
    }

    const double remaining = jsonToDouble(obj.value("remaining_volume"));
    const QString state = obj.value("state").toString();
    if (remaining <= 0.0 || state == QLatin1String("done")) {
        auto it = pendingOrders_.find(uuid);
        if (it != pendingOrders_.end()) {
            const PendingOrder& ctx = it.value();
            const double fillRate = ctx.volume > 0.0 ? ctx.filledVolume / ctx.volume : 1.0;
            const double reference = ctx.isBuy
                    ? (ctx.bestAskAtSubmit > 0.0 ? ctx.bestAskAtSubmit : ctx.price)
                    : (ctx.bestBidAtSubmit > 0.0 ? ctx.bestBidAtSubmit : ctx.price);
            const double avgFill = ctx.weightedFillPrice;
            double slipAbs = 0.0;
            double slipBps = 0.0;
            if (reference > 0.0 && avgFill > 0.0) {
                slipAbs = ctx.isBuy ? avgFill - reference : reference - avgFill;
                slipBps = (slipAbs / reference) * 10'000.0;
            }
            qCInfo(lcBridge) << "order" << uuid
                             << "completed fill-rate" << fillRate
                             << "avg-fill" << avgFill
                             << "slippage" << slipAbs
                             << "(" << slipBps << "bps)";
        }
        pendingOrders_.remove(uuid);
    }
}

void EngineBridge::updatePosition(bool isBuy, double price, double volume, qint64 ts_ms) {
    Q_UNUSED(ts_ms);
    if (volume <= 0.0) return;
    if (isBuy) {
        const double totalCost = positionAvg_ * positionQty_ + price * volume;
        positionQty_ += volume;
        positionAvg_ = positionQty_ > 0.0 ? totalCost / positionQty_ : 0.0;
    } else {
        if (volume >= positionQty_ - 1e-8) {
            positionQty_ = 0.0;
            positionAvg_ = 0.0;
        } else {
            positionQty_ -= volume;
        }
    }
    emit positionInfo(market_, positionQty_, positionAvg_);
}

QByteArray EngineBridge::authToken(const QList<QPair<QString, QString>>& params) const {
    if (access_.isEmpty() || secret_.isEmpty()) return {};
    std::vector<std::pair<std::string, std::string>> native;
    native.reserve(params.size());
    for (const auto& p : params) native.emplace_back(p.first.toStdString(), p.second.toStdString());
    return QByteArray::fromStdString(restClient_.build_authorization_token(native));
}

void EngineBridge::scheduleRealtimeEmit() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastRealtimeEmitMs_ >= kRealtimeEmitIntervalMs) {
        lastRealtimeEmitMs_ = now;
        emit candlesUpdated(market_);
    }
}

void EngineBridge::logRateLimit(const QString& context, int status, const QString& message) {
    qCWarning(lcBridge) << "Rate limit" << context << "status" << status << message;
}

void EngineBridge::placeLimitOrder(double price, double volume, bool isBuy) {
    if (market_.isEmpty()) {
        emit orderRejected(market_, QStringLiteral("Market not selected"));
        return;
    }
    OrderRequest req;
    req.market = market_.toStdString();
    req.side = isBuy ? "buy" : "sell";
    req.ord_type = "limit";
    req.price = price;
    req.volume = volume;

    OrderRequest normalized = req;
    normalized.price = UpbitRestClient::normalize_price(price);
    normalized.volume = UpbitRestClient::normalize_volume(normalized.price, volume, isBuy);
    if (normalized.price <= 0.0 || normalized.volume <= 0.0) {
        emit orderRejected(market_, QStringLiteral("Invalid order parameters"));
        return;
    }

    auto* watcher = new QFutureWatcher<OrderResult>(this);
    connect(watcher, &QFutureWatcher<OrderResult>::finished, this, [this, watcher, normalized, isBuy]() {
        const OrderResult res = watcher->result();
        watcher->deleteLater();
        if (res.accepted) {
            const QString uuid = QString::fromStdString(res.uuid);
            PendingOrder ctx;
            ctx.isBuy = isBuy;
            ctx.price = normalized.price;
            ctx.volume = normalized.volume;
            ctx.submittedMs = QDateTime::currentMSecsSinceEpoch();
            ctx.filledVolume = 0.0;
            ctx.weightedFillPrice = 0.0;
            ctx.bestBidAtSubmit = bestBid_;
            ctx.bestAskAtSubmit = bestAsk_;
            pendingOrders_.insert(uuid, ctx);
            qCInfo(lcBridge) << "order" << uuid << "accepted" << (isBuy ? "BUY" : "SELL")
                             << "px" << ctx.price
                             << "vol" << ctx.volume
                             << "bestBid" << bestBid_
                             << "bestAsk" << bestAsk_;
            emit orderAccepted(market_, uuid, isBuy, normalized.price, normalized.volume);
        } else {
            QString msg = QString::fromStdString(res.error_message);
            if (msg.isEmpty()) msg = QString::fromStdString(res.raw_response);
            if (msg.isEmpty()) msg = QStringLiteral("unknown error");
            const QString reason = res.http_status > 0
                    ? QStringLiteral("HTTP %1 %2").arg(res.http_status).arg(msg)
                    : QStringLiteral("REST failure: %1").arg(msg);
            if (res.http_status == 429) {
                logRateLimit(QStringLiteral("order"), res.http_status, reason);
            } else {
                qCWarning(lcBridge) << "order rejected" << reason;
            }
            emit orderRejected(market_, reason);
        }
    });
    watcher->setFuture(QtConcurrent::run([client = &restClient_, normalized]() {
        return client->post_order(normalized);
    }));
}

void EngineBridge::cancelOrder(const QString& uuid) {
    if (uuid.isEmpty()) return;
    CancelRequest req;
    req.uuid = uuid.toStdString();
    auto* watcher = new QFutureWatcher<OrderResult>(this);
    connect(watcher, &QFutureWatcher<OrderResult>::finished, this, [this, watcher, uuid]() {
        const OrderResult res = watcher->result();
        watcher->deleteLater();
        if (!res.accepted) {
            QString msg = QString::fromStdString(res.error_message);
            if (msg.isEmpty()) msg = QString::fromStdString(res.raw_response);
            if (msg.isEmpty()) msg = QStringLiteral("unknown error");
            const QString reason = res.http_status > 0
                    ? QStringLiteral("Cancel HTTP %1 %2").arg(res.http_status).arg(msg)
                    : QStringLiteral("Cancel failed: %1").arg(msg);
            if (res.http_status == 429) {
                logRateLimit(QStringLiteral("cancel"), res.http_status, reason);
            } else {
                qCWarning(lcBridge) << reason;
            }
            emit orderRejected(market_, reason);
        } else {
            pendingOrders_.remove(uuid);
            qCInfo(lcBridge) << "order" << uuid << "cancel confirmed";
        }
    });
    watcher->setFuture(QtConcurrent::run([client = &restClient_, req]() {
        return client->cancel_order(req);
    }));
}

void EngineBridge::onNetworkReply() {
    if (!pending_) return;
    QNetworkReply* reply = pending_;
    pending_ = nullptr;
    const auto guard = qScopeGuard([reply]() { reply->deleteLater(); });

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(lcBridge) << "network request failed" << reply->errorString();
        const RequestKind failedKind = pendingKind_;
        pendingKind_ = RequestKind::None;
        QTimer::singleShot(2'000, this, [this, failedKind]() {
            switch (failedKind) {
                case RequestKind::Markets: fetchMarkets(); break;
                case RequestKind::Tickers: fetchNextTickerChunk(); break;
                case RequestKind::Candles1m: evaluateNextCandidate(); break;
                case RequestKind::Candles5m: fetchCandles5m(); break;
                case RequestKind::None:
                default: break;
            }
        });
        return;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);

    switch (pendingKind_) {
    case RequestKind::Markets: {
        marketsKRW_.clear();
        if (doc.isArray()) {
            const QJsonArray arr = doc.array();
            for (const QJsonValue& v : arr) {
                const QJsonObject obj = v.toObject();
                const QString market = obj.value("market").toString();
                if (market.startsWith("KRW-")) marketsKRW_.append(market);
            }
        }
        nextTickerIndex_ = 0;
        volume24h_.clear();
        fetchNextTickerChunk();
        break;
    }
    case RequestKind::Tickers: {
        if (doc.isArray()) {
            const QJsonArray arr = doc.array();
            for (const QJsonValue& v : arr) {
                const QJsonObject obj = v.toObject();
                const QString market = obj.value("market").toString();
                const double vol = obj.value("acc_trade_price_24h").toDouble();
                if (!market.isEmpty() && vol > 0.0) {
                    volume24h_.insert(market, vol);
                }
            }
        }
        fetchNextTickerChunk();
        break;
    }
    case RequestKind::Candles1m: {
        std::vector<Candle> minutes;
        if (doc.isArray()) {
            const QJsonArray arr = doc.array();
            minutes.reserve(arr.size());
            for (const QJsonValue& v : arr) {
                const QJsonObject obj = v.toObject();
                Candle c{};
                c.ts_ms = static_cast<long long>(obj.value("timestamp").toVariant().toLongLong());
                c.open = obj.value("opening_price").toDouble();
                c.high = obj.value("high_price").toDouble();
                c.low = obj.value("low_price").toDouble();
                c.close = obj.value("trade_price").toDouble();
                c.volume = obj.value("candle_acc_trade_volume").toDouble();
                minutes.push_back(c);
            }
        }
        if (minutes.size() > 1) {
            std::reverse(minutes.begin(), minutes.end());
            double sumSq = 0.0;
            size_t n = 0;
            for (size_t i = 1; i < minutes.size(); ++i) {
                if (minutes[i-1].close <= 0.0 || minutes[i].close <= 0.0) continue;
                const double r = std::log(minutes[i].close / minutes[i-1].close);
                sumSq += r * r;
                ++n;
            }
            const double rv = (n > 0) ? std::sqrt(sumSq / static_cast<double>(n)) : 0.0;
            const double vol24 = volume24h_.value(pendingMarket_, 0.0);
            if (rv > 0.0 && vol24 > 0.0) {
                const double score = std::log(vol24) * rv;
                if (score > bestScore_) {
                    bestScore_ = score;
                    bestMarket_ = pendingMarket_;
                }
            }
        }
        evaluateNextCandidate();
        break;
    }
    case RequestKind::Candles5m: {
        std::vector<Candle> updated;
        if (doc.isArray()) {
            const QJsonArray arr = doc.array();
            updated.reserve(arr.size());
            for (const QJsonValue& v : arr) {
                const QJsonObject obj = v.toObject();
                Candle c{};
                c.ts_ms = static_cast<long long>(obj.value("timestamp").toVariant().toLongLong());
                c.open = obj.value("opening_price").toDouble();
                c.high = obj.value("high_price").toDouble();
                c.low = obj.value("low_price").toDouble();
                c.close = obj.value("trade_price").toDouble();
                c.volume = obj.value("candle_acc_trade_volume").toDouble();
                updated.push_back(c);
            }
        }
        if (!updated.empty()) {
            std::reverse(updated.begin(), updated.end());
            c5_ = std::move(updated);
            emit candlesUpdated(pendingMarket_);
        }
        break;
    }
    case RequestKind::None:
    default:
        break;
    }

    pendingKind_ = RequestKind::None;
}
