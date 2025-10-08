#include "market_selector.hpp"
#include <cmath>
#include <limits>

static double realized_vol_1m(const std::vector<Candle>& c) {
    if (c.size() < 2) return 0.0;
    double s2 = 0.0; size_t n = 0;
    for (size_t i = 1; i < c.size(); ++i) {
        double r = std::log(c[i].close / c[i-1].close);
        s2 += r*r; ++n;
    }
    return std::sqrt(s2 / std::max<size_t>(1, n));
}

std::string MarketSelector::select_top_market(
    const std::vector<Ticker24h>& tickers,
    const std::vector<std::pair<std::string, std::vector<Candle>>>& candles_1m) {

    double best = -std::numeric_limits<double>::infinity();
    std::string best_mkt;
    for (const auto& t : tickers) {
        auto it = std::find_if(candles_1m.begin(), candles_1m.end(), [&](const auto& p){return p.first==t.market;});
        if (it == candles_1m.end()) continue;
        double rv = realized_vol_1m(it->second);
        double score = std::log(std::max(1e-9, t.acc_trade_price_24h)) * rv;
        if (score > best) { best = score; best_mkt = t.market; }
    }
    return best_mkt;
}

