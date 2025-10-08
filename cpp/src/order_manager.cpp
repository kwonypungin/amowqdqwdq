#include "order_manager.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cctype>

OrderManager::OrderManager(UpbitRestClient& rest, double fee_rate, double min_notional)
    : rest_(rest), fee_rate_(fee_rate), min_notional_(min_notional) {}

OrderResult OrderManager::place_order(const OrderRequest& req) {
    OrderRequest normalized = req;
    const bool is_buy = req.side == "buy" || req.side == "bid";
    if (normalized.ord_type.empty()) normalized.ord_type = "limit";
    std::transform(normalized.ord_type.begin(), normalized.ord_type.end(),
                   normalized.ord_type.begin(), ::tolower);

    if (normalized.ord_type == "limit") {
        normalized.price = UpbitRestClient::normalize_price(req.price);
        normalized.volume = UpbitRestClient::normalize_volume(normalized.price, req.volume, is_buy, min_notional_);
    } else if (normalized.ord_type == "price") {
        // market buy: price field means total KRW
        normalized.price = std::max(req.price, min_notional_);
        normalized.price = std::floor(normalized.price);
        normalized.volume = 0.0;
    } else if (normalized.ord_type == "market") {
        // market sell: ensure volume respects minimal notional if price hint provided
        double ref_price = req.price > 0.0 ? req.price : normalized.price;
        if (ref_price <= 0.0) ref_price = 1.0;
        normalized.volume = UpbitRestClient::normalize_volume(ref_price, req.volume, is_buy, min_notional_);
    }

    auto res = rest_.post_order(normalized);
    if (res.accepted && normalized.ord_type == "limit") {
        const double gross = normalized.price * normalized.volume;
        const double fee_est = gross * fee_rate_;
        std::clog << "[order_manager] placed " << normalized.market << ' '
                  << (is_buy ? "BUY" : "SELL")
                  << " px=" << normalized.price
                  << " vol=" << normalized.volume
                  << " gross=" << gross
                  << " fee_est=" << fee_est
                  << " uuid=" << res.uuid
                  << " status=" << res.http_status << '\n';
    } else if (!res.accepted) {
        std::clog << "[order_manager] order failed status=" << res.http_status
                  << " error=" << res.error_message << '\n';
    }
    return res;
}

OrderResult OrderManager::cancel_order(const CancelRequest& req) {
    return rest_.cancel_order(req);
}
