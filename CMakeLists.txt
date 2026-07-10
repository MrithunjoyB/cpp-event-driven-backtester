cmake_minimum_required(VERSION 3.16)
project(CppEventDrivenBacktester)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(backtester
    src/main.cpp
    src/MarketData.cpp
    src/Strategy.cpp
    src/Portfolio.cpp
    src/ExecutionHandler.cpp
    src/Backtester.cpp
    src/Analysis.cpp
    src/Metrics.cpp
)

include(CTest)

if(BUILD_TESTING)
    add_executable(backtester_tests
        tests/test_engine.cpp
        src/MarketData.cpp
        src/Strategy.cpp
        src/Portfolio.cpp
        src/ExecutionHandler.cpp
        src/Backtester.cpp
        src/Analysis.cpp
        src/Metrics.cpp
    )

    add_test(NAME backtester_tests COMMAND backtester_tests)
endif()
