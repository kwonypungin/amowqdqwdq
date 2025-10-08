#include "ChartWidget.hpp"
#include <QtCharts/QCandlestickSet>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QVBoxLayout>
#include <QDateTime>
#include <limits>
#include <algorithm>

ChartWidget::ChartWidget(QWidget* parent) : QWidget(parent) {
    chart_ = new QChart();
    candles_ = new QCandlestickSeries();
    candles_->setDecreasingColor(QColor("#d9534f"));
    candles_->setIncreasingColor(QColor("#5cb85c"));

    buys_ = new QScatterSeries();
    buys_->setMarkerShape(QScatterSeries::MarkerShapeCircle);
    buys_->setColor(QColor("#00c853"));
    buys_->setMarkerSize(8.0);

    sells_ = new QScatterSeries();
    sells_->setMarkerShape(QScatterSeries::MarkerShapeRectangle);
    sells_->setColor(QColor("#ff3d00"));
    sells_->setMarkerSize(8.0);

    chart_->addSeries(candles_);
    chart_->addSeries(buys_);
    chart_->addSeries(sells_);
    chart_->legend()->hide();
    chart_->setTheme(QChart::ChartThemeDark);

    axisX_ = new QDateTimeAxis;
    axisX_->setFormat("MM-dd HH:mm");
    axisX_->setTitleText("Time");
    chart_->addAxis(axisX_, Qt::AlignBottom);
    candles_->attachAxis(axisX_);
    buys_->attachAxis(axisX_);
    sells_->attachAxis(axisX_);

    axisY_ = new QValueAxis;
    axisY_->setLabelFormat("%.0f");
    axisY_->setTitleText("Price");
    chart_->addAxis(axisY_, Qt::AlignLeft);
    candles_->attachAxis(axisY_);
    buys_->attachAxis(axisY_);
    sells_->attachAxis(axisY_);

    auto* view = new QChartView(chart_);
    view->setRenderHint(QPainter::Antialiasing);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(view);
}

void ChartWidget::setCandles(const std::vector<Candle>& candles) {
    data_ = candles;
    candles_->clear();
    buys_->clear();
    sells_->clear();
    qreal minPrice = std::numeric_limits<double>::max();
    qreal maxPrice = std::numeric_limits<double>::lowest();
    qint64 minTs = std::numeric_limits<qint64>::max();
    qint64 maxTs = 0;
    for (const auto& c : data_) {
        auto* set = new QCandlestickSet(c.open, c.high, c.low, c.close, static_cast<qreal>(c.ts_ms));
        candles_->append(set);
        minPrice = std::min(minPrice, std::min(c.low, c.close));
        maxPrice = std::max(maxPrice, std::max(c.high, c.close));
        minTs = std::min(minTs, c.ts_ms);
        maxTs = std::max(maxTs, c.ts_ms);
    }
    if (axisX_ && minTs < maxTs) {
        axisX_->setRange(QDateTime::fromMSecsSinceEpoch(minTs), QDateTime::fromMSecsSinceEpoch(maxTs));
    }
    if (axisY_ && minPrice < maxPrice) {
        axisY_->setRange(minPrice * 0.995, maxPrice * 1.005);
    }
}

void ChartWidget::addBuyMarker(qint64 ts_ms, double price) {
    buys_->append(ts_ms, price);
}

void ChartWidget::addSellMarker(qint64 ts_ms, double price) {
    sells_->append(ts_ms, price);
}

void ChartWidget::setPosition(double avgPrice, double qty) {
    posAvg_ = avgPrice; posQty_ = qty;
}
