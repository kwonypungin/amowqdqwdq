#pragma once
#include <string>
#include "upbit_rest.hpp"
#include "market_selector.hpp"
#include "strategy_5m_scalper.hpp"
#include "risk_manager.hpp"
#include "order_manager.hpp"

class Engine {
public:
    Engine();
    int run_once();
private:
    UpbitRestClient rest_;
    MarketSelector selector_;
    Strategy5mScalper strategy_;
    RiskManager risk_;
    OrderManager order_mgr_;
};
