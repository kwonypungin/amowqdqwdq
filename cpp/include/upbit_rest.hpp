#pragma once
#include <string>
#include <vector>
#include "types.hpp"

class UpbitRestClient {
public:
    explicit UpbitRestClient(const std::string& base_url = "https://api.upbit.com");

    std::vector<std::string> get_markets_krw();
    std::vector<Ticker24h> get_tickers(const std::vector<std::string>& markets);
    std::vector<Candle> get_candles_minutes(const std::string& market, int unit, int count);

    OrderResult post_order(const OrderRequest& req);
    OrderResult cancel_order(const CancelRequest& req);

    void set_credentials(std::string access_key, std::string secret_key);

    std::string build_authorization_token(const std::vector<std::pair<std::string, std::string>>& params = {}) const;

    static double normalize_price(double price);
    static double normalize_volume(double price, double volume, bool is_buy, double min_notional = 5000.0);
    static double taker_fee_rate();

private:
    std::string base_url_;
    std::string access_key_;
    std::string secret_key_;
};
