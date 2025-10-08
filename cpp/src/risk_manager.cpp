#include "risk_manager.hpp"
#include <algorithm>

double RiskManager::calc_position_size(double equity_krw, double atr, double risk_per_trade) {
    if (atr <= 0.0) return 0.0;
    double risk_amt = equity_krw * risk_per_trade;
    return std::max(0.0, risk_amt / atr);
}

bool RiskManager::daily_stop_triggered(double daily_pnl_ratio, double daily_stop_ratio) {
    return daily_pnl_ratio <= -std::abs(daily_stop_ratio);
}

