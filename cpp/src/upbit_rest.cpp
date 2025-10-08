#include "upbit_rest.hpp"
#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>

namespace {

constexpr double kMinNotionalKRW = 5000.0;
constexpr double kFeeRateTaker = 0.0005;

void ensure_curl_global() {
    static std::once_flag once;
    std::call_once(once, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::atexit([]() { curl_global_cleanup(); });
    });
}

std::string generate_nonce() {
    std::array<unsigned char, 16> bytes{};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : bytes) b = static_cast<unsigned char>(dist(gen));
    std::ostringstream oss;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

std::string bytes_to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string base64_url_encode(const std::string& input) {
    if (input.empty()) return {};
    int encoded_len = 4 * ((static_cast<int>(input.size()) + 2) / 3);
    std::string buffer(static_cast<size_t>(encoded_len), '\0');
    int actual_len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&buffer[0]),
                                     reinterpret_cast<const unsigned char*>(input.data()),
                                     static_cast<int>(input.size()));
    if (actual_len <= 0) return {};
    buffer.resize(static_cast<size_t>(actual_len));
    for (char& c : buffer) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!buffer.empty() && buffer.back() == '=') buffer.pop_back();
    return buffer;
}

std::string url_encode(const std::string& value) {
    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) return {};
    char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string result;
    if (escaped) {
        result.assign(escaped);
        curl_free(escaped);
    }
    curl_easy_cleanup(curl);
    return result;
}

std::string format_decimal(double value, int max_decimals) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(max_decimals) << value;
    auto str = oss.str();
    auto pos = str.find('.');
    if (pos != std::string::npos) {
        while (!str.empty() && str.back() == '0') str.pop_back();
        if (!str.empty() && str.back() == '.') str.pop_back();
    }
    if (str.empty()) str = "0";
    return str;
}

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    if (!userdata) return 0;
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string extract_uuid(const std::string& body) {
    auto key_pos = body.find("\"uuid\"");
    if (key_pos == std::string::npos) return {};
    auto colon = body.find(':', key_pos);
    if (colon == std::string::npos) return {};
    auto quote = body.find('"', colon + 1);
    if (quote == std::string::npos) return {};
    auto end = body.find('"', quote + 1);
    if (end == std::string::npos) return {};
    return body.substr(quote + 1, end - quote - 1);
}

} // namespace

UpbitRestClient::UpbitRestClient(const std::string& base_url) : base_url_(base_url) {}

void UpbitRestClient::set_credentials(std::string access_key, std::string secret_key) {
    access_key_ = std::move(access_key);
    secret_key_ = std::move(secret_key);
}

double UpbitRestClient::normalize_price(double price) {
    if (price <= 0.0) return 0.0;
    double tick = 0.0001;
    if (price >= 2'000'000.0) tick = 1'000.0;
    else if (price >= 1'000'000.0) tick = 500.0;
    else if (price >= 500'000.0) tick = 100.0;
    else if (price >= 100'000.0) tick = 50.0;
    else if (price >= 50'000.0) tick = 10.0;
    else if (price >= 10'000.0) tick = 5.0;
    else if (price >= 1'000.0) tick = 1.0;
    else if (price >= 100.0) tick = 0.1;
    else if (price >= 10.0) tick = 0.01;
    else if (price >= 1.0) tick = 0.001;
    const double scaled = std::floor((price / tick) + 1e-9) * tick;
    const double rounded = std::round(scaled * 100000000.0) / 100000000.0;
    return rounded;
}

double UpbitRestClient::normalize_volume(double price, double volume, bool is_buy, double min_notional) {
    if (price <= 0.0) return 0.0;
    const double fee_rate = taker_fee_rate();
    const double epsilon = 1e-9;
    double target = volume;
    double min_volume = min_notional / price;
    if (is_buy && fee_rate > 0.0) {
        // ensure funds cover fee as well
        min_volume = std::max(min_volume, (min_notional / price) / (1.0 - fee_rate));
    }
    target = std::max(target, min_volume);
    double quantized = std::floor(target * 1e8 + epsilon) / 1e8;
    if (quantized < min_volume - epsilon) {
        quantized = std::ceil(min_volume * 1e8 - epsilon) / 1e8;
    }
    double notional = price * quantized;
    if (notional < min_notional - epsilon) {
        quantized = std::ceil((min_notional / price) * 1e8 - epsilon) / 1e8;
    }
    return quantized;
}

double UpbitRestClient::taker_fee_rate() {
    return kFeeRateTaker;
}

std::vector<std::string> UpbitRestClient::get_markets_krw() {
    // TODO: replace with live REST call
    return {"KRW-BTC", "KRW-ETH"};
}

std::vector<Ticker24h> UpbitRestClient::get_tickers(const std::vector<std::string>& markets) {
    // TODO: replace with live REST call
    std::vector<Ticker24h> out;
    for (const auto& m : markets) out.push_back({m, m == "KRW-BTC" ? 1.0 : 0.8});
    return out;
}

std::vector<Candle> UpbitRestClient::get_candles_minutes(const std::string& market, int unit, int count) {
    // TODO: replace with live REST call
    std::vector<Candle> v; v.reserve(static_cast<size_t>(count));
    double px = market == "KRW-BTC" ? 100.0 : 50.0;
    for (int i = 0; i < count; ++i) {
        v.push_back({1000LL * i, px, px*1.01, px*0.99, px*(1.0 + 0.001*i), 1.0});
    }
    return v;
}

std::string UpbitRestClient::build_authorization_token(const std::vector<std::pair<std::string, std::string>>& params) const {
    if (access_key_.empty() || secret_key_.empty()) return {};
    std::vector<std::pair<std::string, std::string>> sorted(params);
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    std::ostringstream query_stream;
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) query_stream << '&';
        query_stream << url_encode(sorted[i].first) << '=' << url_encode(sorted[i].second);
    }
    const std::string query = query_stream.str();
    std::string query_hash_hex;
    if (!query.empty()) {
        unsigned char hash[SHA512_DIGEST_LENGTH];
        SHA512(reinterpret_cast<const unsigned char*>(query.data()), query.size(), hash);
        query_hash_hex = bytes_to_hex(hash, SHA512_DIGEST_LENGTH);
    }

    const std::string nonce = generate_nonce();

    std::ostringstream payload_oss;
    payload_oss << R"({"access_key":")" << access_key_ << R"(","nonce":")" << nonce << R"(")";
    if (!query.empty()) {
        payload_oss << R"(,"query_hash":")" << query_hash_hex << R"(","query_hash_alg":"SHA512")";
    }
    payload_oss << '}';

    const std::string header = R"({"alg":"HS256","typ":"JWT"})";
    const std::string header_b64 = base64_url_encode(header);
    const std::string payload_b64 = base64_url_encode(payload_oss.str());
    if (header_b64.empty() || payload_b64.empty()) return {};
    const std::string signing_input = header_b64 + "." + payload_b64;

    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = 0;
    HMAC(EVP_sha256(),
         reinterpret_cast<const unsigned char*>(secret_key_.data()), static_cast<int>(secret_key_.size()),
         reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size(),
         mac, &mac_len);
    std::string signature(reinterpret_cast<char*>(mac), mac_len);
    const std::string signature_b64 = base64_url_encode(signature);
    if (signature_b64.empty()) return {};
    return "Bearer " + signing_input + "." + signature_b64;
}

OrderResult UpbitRestClient::post_order(const OrderRequest& req) {
    OrderResult result{};
    if (req.market.empty()) return result;
    const bool is_buy = req.side == "buy" || req.side == "bid";
    const std::string side = is_buy ? "bid" : "ask";
    std::string ord_type = req.ord_type.empty() ? "limit" : req.ord_type;
    std::transform(ord_type.begin(), ord_type.end(), ord_type.begin(), ::tolower);

    double price = req.price;
    double volume = req.volume;
    if (ord_type == "limit") {
        price = normalize_price(price);
        volume = normalize_volume(price, volume, is_buy, kMinNotionalKRW);
    }

    std::vector<std::pair<std::string, std::string>> params;
    params.emplace_back("market", req.market);
    params.emplace_back("side", side);
    params.emplace_back("ord_type", ord_type);

    std::ostringstream price_stream;
    std::ostringstream volume_stream;
    if (ord_type == "limit") {
        price_stream << format_decimal(price, 8);
        volume_stream << format_decimal(volume, 8);
        params.emplace_back("price", price_stream.str());
        params.emplace_back("volume", volume_stream.str());
    } else if (ord_type == "price") { // market buy
        price_stream << format_decimal(req.price, 8);
        params.emplace_back("price", price_stream.str());
    } else if (ord_type == "market") { // market sell
        volume_stream << format_decimal(req.volume, 8);
        params.emplace_back("volume", volume_stream.str());
    }

    const std::string auth = build_authorization_token(params);
    if (auth.empty()) return result;

    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) return result;

    std::string url = base_url_ + "/v1/orders";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    std::ostringstream body;
    body << '{';
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) body << ',';
        body << '"' << params[i].first << "\":\"" << params[i].second << '"';
    }
    body << '}';
    const std::string body_str = body.str();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    const std::string auth_header = "Authorization: " + auth;
    headers = curl_slist_append(headers, auth_header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    result.http_status = static_cast<int>(http_code);
    result.raw_response = response;
    if (rc != CURLE_OK) {
        result.error_message = curl_easy_strerror(rc);
        return result;
    }
    if (http_code >= 400) {
        if (http_code == 429) result.error_message = "HTTP 429 rate limited";
        else result.error_message = "HTTP error " + std::to_string(result.http_status);
        return result;
    }

    result.uuid = extract_uuid(response);
    result.accepted = !result.uuid.empty();
    return result;
}

OrderResult UpbitRestClient::cancel_order(const CancelRequest& req) {
    OrderResult result{};
    if (req.uuid.empty()) return result;

    std::vector<std::pair<std::string, std::string>> params;
    params.emplace_back("uuid", req.uuid);
    const std::string auth = build_authorization_token(params);
    if (auth.empty()) return result;

    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) return result;

    std::ostringstream url_oss;
    url_oss << base_url_ << "/v1/order?uuid=" << url_encode(req.uuid);
    const std::string url = url_oss.str();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    const std::string auth_header = "Authorization: " + auth;
    headers = curl_slist_append(headers, auth_header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    result.http_status = static_cast<int>(http_code);
    result.raw_response = response;
    if (rc != CURLE_OK) {
        result.error_message = curl_easy_strerror(rc);
        return result;
    }
    if (http_code >= 400) {
        if (http_code == 429) result.error_message = "HTTP 429 rate limited";
        else result.error_message = "HTTP error " + std::to_string(result.http_status);
        return result;
    }

    result.uuid = extract_uuid(response);
    result.accepted = !result.uuid.empty();
    return result;
}
