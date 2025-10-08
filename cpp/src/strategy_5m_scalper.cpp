#include "strategy_5m_scalper.hpp"
#include <algorithm>

static double atr5(const std::vector<Candle>& c, int n) {
    if ((int)c.size() < n+1) return 0.0;
    double s = 0.0; int k = 0;
    for (size_t i = c.size()-n; i < c.size(); ++i) {
        double tr = std::max({c[i].high - c[i].low, std::abs(c[i].high - c[i-1].close), std::abs(c[i].low - c[i-1].close)});
        s += tr; ++k;
    }
    return k? s/k : 0.0;
}

TradeDecision Strategy5mScalper::evaluate(const std::vector<Candle>& c) {
    TradeDecision d;
    if (c.size() < 8) return d;
    const auto& last = c.back();
    double atr = atr5(c, 14);
    double hh = last.high;
    for (size_t i = c.size()-6; i < c.size()-1; ++i) hh = std::max(hh, c[i].high);
    bool breakout = last.close > hh;
    if (breakout && atr > 0.0) {
        d.enter_long = true;
        d.limit_price = last.close; // stub: use last close as limit
    }
    return d;
}

