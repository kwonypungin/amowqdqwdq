cmake_minimum_required(VERSION 3.16)
project(upbit_scalper CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_executable(upbit_scalper
    src/main.cpp
    src/engine.cpp
    src/market_selector.cpp
    src/strategy_5m_scalper.cpp
    src/risk_manager.cpp
    src/upbit_rest.cpp
    src/order_manager.cpp
)

target_include_directories(upbit_scalper PRIVATE include)
target_link_libraries(upbit_scalper PRIVATE OpenSSL::Crypto CURL::libcurl)
