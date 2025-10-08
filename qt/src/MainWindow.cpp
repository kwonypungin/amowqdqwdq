#include "MainWindow.hpp"
#include "ChartWidget.hpp"
#include "EngineBridge.hpp"
#include <QDockWidget>
#include <QStatusBar>
#include <QDateTime>

MainWindow::MainWindow(const QString& accessKey, const QString& secretKey, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Upbit 5m Scalper");
    resize(1280, 800);

    chart_ = new ChartWidget(this);
    setCentralWidget(chart_);

    engine_ = new EngineBridge(accessKey, secretKey, this);
    connect(engine_, &EngineBridge::marketChanged, this, [this](const QString& market) {
        statusBar()->showMessage(QStringLiteral("%1 선택. 5분봉 실시간 업데이트.").arg(market));
    });
    connect(engine_, &EngineBridge::candlesUpdated, this, &MainWindow::onCandlesUpdated);
    connect(engine_, &EngineBridge::orderExecuted, this, &MainWindow::onOrderExecuted);
    connect(engine_, &EngineBridge::positionInfo, this, &MainWindow::onPositionInfo);
    connect(engine_, &EngineBridge::orderAccepted, this, [this](const QString& market, const QString& uuid, bool isBuy, double price, double volume) {
        Q_UNUSED(market);
        statusBar()->showMessage(QStringLiteral("주문 접수 %1 %2 %3 @ %4 (%5)")
                                     .arg(uuid)
                                     .arg(isBuy ? QStringLiteral("매수") : QStringLiteral("매도"))
                                     .arg(volume, 0, 'f', 6)
                                     .arg(price, 0, 'f', 2)
                                     .arg(QDateTime::currentDateTime().toString("HH:mm:ss")), 5000);
    });
    connect(engine_, &EngineBridge::orderRejected, this, [this](const QString& market, const QString& reason) {
        Q_UNUSED(market);
        statusBar()->showMessage(QStringLiteral("주문 실패: %1").arg(reason), 5000);
    });

    statusBar()->showMessage("시장 정보를 불러오는 중...");
    engine_->start();
}

MainWindow::~MainWindow() = default;

void MainWindow::onCandlesUpdated(const QString& market) {
    Q_UNUSED(market);
    chart_->setCandles(engine_->candles());
}

void MainWindow::onOrderExecuted(const QString& market, qint64 ts_ms, double price, bool isBuy) {
    Q_UNUSED(market);
    if (isBuy) chart_->addBuyMarker(ts_ms, price);
    else chart_->addSellMarker(ts_ms, price);
}

void MainWindow::onPositionInfo(const QString& market, double qty, double avgPrice) {
    Q_UNUSED(market);
    chart_->setPosition(avgPrice, qty);
}
