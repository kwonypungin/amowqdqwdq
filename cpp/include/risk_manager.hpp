#pragma once
#include "types.hpp"

class RiskManager {
public:
    double calc_position_size(double equity_krw, double atr, double risk_per_trade);
    bool daily_stop_triggered(double daily_pnl_ratio, double daily_stop_ratio);
};

