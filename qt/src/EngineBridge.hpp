#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QList>
#include <QPair>
#include <vector>
#include <limits>
#include "types.hpp"
#include "upbit_rest.hpp"

class QNetworkAccessManager;
class QNetworkReply;
class QWebSocket;
class QFutureWatcherBase;
class QJsonObject;

class EngineBridge : public QObject {
    Q_OBJECT
public:
    EngineBridge(QString access, QString secret, QObject* parent=nullptr);
    void start();
    const std::vector<Candle>& candles() const { return c5_; }

signals:
    void marketChanged(const QString& market);
    void candlesUpdated(const QString& market);
    void orderExecuted(const QString& market, qint64 ts_ms, double price, bool isBuy);
    void positionInfo(const QString& market, double qty, double avgPrice);
    void orderAccepted(const QString& market, const QString& uuid, bool isBuy, double price, double volume);
    void orderRejected(const QString& market, const QString& reason);

private slots:
    void onFiveMinuteTick();
    void onNetworkReply();
    void onPublicWsConnected();
    void onPrivateWsConnected();
    void onPublicWsClosed();
    void onPrivateWsClosed();
    void onPublicTextMessage(const QString& message);
    void onPublicBinaryMessage(const QByteArray& message);
    void onPrivateTextMessage(const QString& message);
    void onPrivateBinaryMessage(const QByteArray& message);

public slots:
    void placeLimitOrder(double price, double volume, bool isBuy);
    void cancelOrder(const QString& uuid);

private:
    enum class RequestKind { None, Markets, Tickers, Candles5m, Candles1m };

    struct PendingOrder {
        bool isBuy{};
        double price{};
        double volume{};
        qint64 submittedMs{};
        double filledVolume{0.0};
        double weightedFillPrice{0.0};
        double bestBidAtSubmit{0.0};
        double bestAskAtSubmit{0.0};
    };

    void fetchMarkets();
    void fetchNextTickerChunk();
    void evaluateNextCandidate();
    void fetchCandles(int unit, int count, RequestKind kind, const QString& market);
    void fetchCandles5m();
    void ensureSockets();
    void connectPublicSocket();
    void connectPrivateSocket();
    void subscribePublic(const QString& market);
    void subscribePrivate(const QString& market);
    void handlePublicMessage(const QByteArray& payload);
    void handlePrivateMessage(const QByteArray& payload);
    void processTradeMessage(const QJsonObject& obj);
    void processOrderbookMessage(const QJsonObject& obj);
    void processMyOrderMessage(const QJsonObject& obj);
    void updatePosition(bool isBuy, double price, double volume, qint64 ts_ms);
    QByteArray authToken(const QList<QPair<QString, QString>>& params = {}) const;
    void scheduleRealtimeEmit();
    void logRateLimit(const QString& context, int status, const QString& message);

    QString access_;
    QString secret_;
    QString market_;
    std::vector<Candle> c5_;
    QTimer timer_;
    class QNetworkAccessManager* net_{nullptr};
    class QNetworkReply* pending_{nullptr};
    RequestKind pendingKind_{RequestKind::None};
    QString pendingMarket_;
    int pendingUnit_{0};

    QStringList marketsKRW_;
    int nextTickerIndex_{0};
    QHash<QString, double> volume24h_;
    QStringList candidateQueue_;
    int candidateIndex_{0};
    QString bestMarket_;
    double bestScore_{-std::numeric_limits<double>::infinity()};
    bool selectionReady_{false};
    QWebSocket* wsPublic_{nullptr};
    QWebSocket* wsPrivate_{nullptr};
    QTimer wsReconnectTimer_;
    QTimer heartbeatTimer_;
    QString subscribedMarket_;
    bool wsPublicConnected_{false};
    bool wsPrivateConnected_{false};
    UpbitRestClient restClient_;
    QHash<QString, PendingOrder> pendingOrders_;
    double positionQty_{0.0};
    double positionAvg_{0.0};
    double bestBid_{0.0};
    double bestAsk_{0.0};
    qint64 lastRealtimeEmitMs_{0};
};
