#pragma once
#include <QMainWindow>
#include <QString>

class ChartWidget;
class EngineBridge;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& accessKey, const QString& secretKey, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onCandlesUpdated(const QString& market);
    void onOrderExecuted(const QString& market, qint64 ts_ms, double price, bool isBuy);
    void onPositionInfo(const QString& market, double qty, double avgPrice);

private:
    ChartWidget* chart_{nullptr};
    EngineBridge* engine_{nullptr};
};

