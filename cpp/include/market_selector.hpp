#pragma once
#include <string>
#include <vector>
#include "types.hpp"

class MarketSelector {
public:
    std::string select_top_market(const std::vector<Ticker24h>& tickers,
                                  const std::vector<std::pair<std::string, std::vector<Candle>>>& candles_1m);
};

