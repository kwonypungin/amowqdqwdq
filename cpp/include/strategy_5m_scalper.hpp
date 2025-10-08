#pragma once
#include <vector>
#include <string>
#include "types.hpp"

struct TradeDecision {
    bool enter_long{false};
    bool exit_position{false};
    double limit_price{};
};

class Strategy5mScalper {
public:
    TradeDecision evaluate(const std::vector<Candle>& candles_5m);
};

