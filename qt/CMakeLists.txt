set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Qt6Keychain CONFIG REQUIRED)

add_executable(upbit_ui
    src/main.cpp
    src/MainWindow.cpp
    src/LoginDialog.cpp
    src/ChartWidget.cpp
    src/EngineBridge.cpp
    resources/resources.qrc
)

target_include_directories(upbit_ui PRIVATE ${CMAKE_SOURCE_DIR}/cpp/include qt/src)

target_link_libraries(upbit_ui PRIVATE Qt6::Widgets Qt6::Charts Qt6::Network Qt6::WebSockets Qt6::Concurrent Qt6Keychain::Qt6Keychain OpenSSL::Crypto CURL::libcurl)
