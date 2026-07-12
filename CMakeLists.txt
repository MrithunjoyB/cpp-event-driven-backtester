cmake_minimum_required(VERSION 3.16)
project(CppEventDrivenBacktester VERSION 2.0.0 LANGUAGES CXX)

option(QUANT_ENABLE_TESTS "Build deterministic test targets" ON)
option(QUANT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
set(QUANT_SANITIZER "" CACHE STRING "Sanitizer selection: address, undefined, or address,undefined")
option(QUANT_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings" ON)
option(QUANT_ENABLE_BENCHMARKS "Enable benchmark CLI functionality" ON)

set(QUANT_CORE_SOURCES
    src/market_data/MarketData.cpp
    src/market_data/TradingCalendar.cpp
    src/market_data/CorporateAction.cpp
    src/strategies/Strategy.cpp
    src/portfolio/Portfolio.cpp
    src/allocation/AllocationPolicy.cpp
    src/portfolio/PortfolioBacktester.cpp
    src/portfolio/UnionPortfolioBacktester.cpp
    src/methodology/ResearchMethodology.cpp
    src/execution/ExecutionHandler.cpp
    src/experiments/Backtester.cpp
    src/experiments/Analysis.cpp
    src/experiments/BootstrapAnalyzer.cpp
    src/experiments/SelectionRisk.cpp
    src/analytics/Metrics.cpp
    src/analytics/PortfolioAttribution.cpp
    src/analytics/StatisticalAnalysis.cpp
    src/io/ResultExporter.cpp
    src/domain/Date.cpp
    src/config/ConfigLoader.cpp
    src/app/Application.cpp
)

add_library(quant_core STATIC ${QUANT_CORE_SOURCES})
target_compile_features(quant_core PUBLIC cxx_std_17)
target_include_directories(quant_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(quant_core PUBLIC QUANT_PROJECT_VERSION="${PROJECT_VERSION}")

function(quant_apply_project_options target)
    if(QUANT_ENABLE_STRICT_WARNINGS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
            )
        elseif(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        endif()
    endif()

    set(quant_sanitizer "${QUANT_SANITIZER}")
    if(QUANT_ENABLE_SANITIZERS AND quant_sanitizer STREQUAL "")
        set(quant_sanitizer "address,undefined")
    endif()
    if(NOT quant_sanitizer STREQUAL "")
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target} PRIVATE -fsanitize=${quant_sanitizer} -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=${quant_sanitizer})
        else()
            message(FATAL_ERROR "Sanitizers require GCC or Clang")
        endif()
    endif()
endfunction()

quant_apply_project_options(quant_core)

add_executable(quant_cli apps/quant_cli.cpp)
target_link_libraries(quant_cli PRIVATE quant_core)
target_compile_features(quant_cli PRIVATE cxx_std_17)
quant_apply_project_options(quant_cli)

include(CTest)
if(BUILD_TESTING AND QUANT_ENABLE_TESTS)
    function(quant_add_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE quant_core)
        target_compile_features(${target} PRIVATE cxx_std_17)
        quant_apply_project_options(${target})
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endfunction()

    quant_add_test(quant_regression_tests tests/test_engine.cpp)
    quant_add_test(domain_config_tests tests/refactor/test_domain_config.cpp)
    quant_add_test(methodology_tests tests/refactor/test_methodology.cpp)
    quant_add_test(export_tests tests/refactor/test_export.cpp)
    quant_add_test(bootstrap_tests tests/refactor/test_bootstrap.cpp)
    quant_add_test(calendar_tests tests/refactor/test_calendar.cpp)
    quant_add_test(corporate_action_tests tests/refactor/test_corporate_actions.cpp)
    quant_add_test(union_portfolio_tests tests/refactor/test_union_portfolio.cpp)
    quant_add_test(attribution_tests tests/refactor/test_attribution.cpp)
    quant_add_test(statistical_tests tests/refactor/test_statistics.cpp)
    quant_add_test(selection_risk_tests tests/refactor/test_selection_risk.cpp)

    add_test(NAME cli_help COMMAND quant_cli help)
    add_test(NAME cli_version COMMAND quant_cli version)
    add_test(NAME cli_validate_config COMMAND quant_cli validate-config --config configs/ma_walk_forward.json)
    add_test(NAME cli_dry_run COMMAND quant_cli run --config configs/ma_walk_forward.json --dry-run)
    add_test(NAME cli_unknown_command COMMAND quant_cli definitely-not-a-command)
    set_tests_properties(
        cli_help cli_version cli_validate_config cli_dry_run cli_unknown_command
        PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(cli_unknown_command PROPERTIES WILL_FAIL TRUE)
endif()
