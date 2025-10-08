cmake_minimum_required(VERSION 3.16)
project(upbit_trader_all LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(cpp)

# Qt UI (optional if Qt6 not found)
option(BUILD_QT_UI "Build Qt UI app" ON)
if (BUILD_QT_UI)
  find_package(Qt6 COMPONENTS Widgets Charts Network WebSockets Concurrent REQUIRED)
  if (Qt6_FOUND)
    add_subdirectory(qt)
  else()
    message(WARNING "Qt6 not found; skipping UI build")
  endif()
endif()
