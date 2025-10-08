#pragma once
#include <string>
#include <vector>

struct Candle {
    long long ts_ms{};
    double open{};
    double high{};
    double low{};
    double close{};
    double volume{};
};

struct Ticker24h {
    std::string market;
    double acc_trade_price_24h{};
};

struct OrderRequest {
    std::string market;
    std::string side; // buy/sell (converted to bid/ask internally)
    std::string ord_type; // limit/price/market
    double price{};
    double volume{};
};

struct OrderResult {
    bool accepted{false};
    std::string uuid;
    int http_status{0};
    std::string error_message;
    std::string raw_response;
};

struct CancelRequest {
    std::string uuid;
};
