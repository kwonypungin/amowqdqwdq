#include "engine.hpp"
#include <vector>
#include <utility>
#include <cstdlib>

Engine::Engine()
    : rest_(),
      selector_(),
      strategy_(),
      risk_(),
      order_mgr_(rest_) {
    if (const char* access = std::getenv("UPBIT_ACCESS_KEY")) {
        if (const char* secret = std::getenv("UPBIT_SECRET_KEY")) {
            rest_.set_credentials(access, secret);
        }
    }
}

int Engine::run_once() {
    auto markets = rest_.get_markets_krw();
    auto tickers = rest_.get_tickers(markets);

    std::vector<std::pair<std::string, std::vector<Candle>>> m1;
    for (const auto& m : markets) {
        m1.push_back({m, rest_.get_candles_minutes(m, 1, 60)});
    }
    auto market = selector_.select_top_market(tickers, m1);
    if (market.empty()) return 1;

    auto c5 = rest_.get_candles_minutes(market, 5, 50);
    auto decision = strategy_.evaluate(c5);
    if (decision.enter_long) {
        OrderRequest req{market, "buy", "limit", decision.limit_price, 0.001};
        auto res = order_mgr_.place_order(req);
        return res.accepted ? 0 : 2;
    }
    return 0;
}
