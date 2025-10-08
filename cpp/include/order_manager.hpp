#pragma once
#include "types.hpp"
#include "upbit_rest.hpp"

class OrderManager {
public:
    explicit OrderManager(UpbitRestClient& rest,
                          double fee_rate = UpbitRestClient::taker_fee_rate(),
                          double min_notional = 5000.0);

    OrderResult place_order(const OrderRequest& req);
    OrderResult cancel_order(const CancelRequest& req);

private:
    UpbitRestClient& rest_;
    double fee_rate_;
    double min_notional_;
};
