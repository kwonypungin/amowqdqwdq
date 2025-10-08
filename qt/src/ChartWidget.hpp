#pragma once
#include <QWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QCandlestickSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <vector>
#include "types.hpp"

QT_CHARTS_USE_NAMESPACE

class ChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChartWidget(QWidget* parent = nullptr);
    void setCandles(const std::vector<Candle>& candles);
    void addBuyMarker(qint64 ts_ms, double price);
    void addSellMarker(qint64 ts_ms, double price);
    void setPosition(double avgPrice, double qty);

private:
    QChart* chart_{nullptr};
    QCandlestickSeries* candles_{nullptr};
    QScatterSeries* buys_{nullptr};
    QScatterSeries* sells_{nullptr};
    QDateTimeAxis* axisX_{nullptr};
    QValueAxis* axisY_{nullptr};
    std::vector<Candle> data_;
    double posAvg_{0.0};
    double posQty_{0.0};
};
