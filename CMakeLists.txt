cmake_minimum_required(VERSION 3.16)
project(CppEventDrivenBacktester VERSION 1.0.0 LANGUAGES CXX)

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
    src/random/StableRng.cpp
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
                -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
            )
        elseif(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX)
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
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
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
    quant_add_test(performance_tests tests/refactor/test_performance.cpp)
    quant_add_test(stable_rng_tests tests/refactor/test_stable_rng.cpp)
    add_test(NAME reproducibility_tests COMMAND ${Python3_EXECUTABLE} tests/test_reproducibility.py)
    set_tests_properties(reproducibility_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME reproducibility_integration_tests COMMAND ${Python3_EXECUTABLE} tests/test_reproducibility_integration.py)
    set_tests_properties(reproducibility_integration_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME final_audit_tests COMMAND ${Python3_EXECUTABLE} tests/test_final_audit.py)
    set_tests_properties(final_audit_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME final_audit_validator COMMAND ${Python3_EXECUTABLE} scripts/validate_final_audit.py audit/final)
    set_tests_properties(final_audit_validator PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME stable_rng_python_reference COMMAND ${Python3_EXECUTABLE} scripts/reference_stable_rng.py)
    set_tests_properties(stable_rng_python_reference PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME synthetic_data_tests COMMAND ${Python3_EXECUTABLE} tests/test_synthetic_data.py)
    set_tests_properties(synthetic_data_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME user_market_data_tests COMMAND ${Python3_EXECUTABLE} tests/test_user_market_data.py)
    set_tests_properties(user_market_data_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME public_data_boundary_tests COMMAND ${Python3_EXECUTABLE} tests/test_public_data_boundary.py)
    set_tests_properties(public_data_boundary_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME public_data_boundary_validator COMMAND ${Python3_EXECUTABLE} scripts/validate_public_data_boundary.py)
    set_tests_properties(public_data_boundary_validator PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME release_metadata_tests COMMAND ${Python3_EXECUTABLE} tests/test_release_metadata.py --cli $<TARGET_FILE:quant_cli>)
    set_tests_properties(release_metadata_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME release_provenance_tests COMMAND ${Python3_EXECUTABLE} tests/test_release_provenance.py)
    set_tests_properties(release_provenance_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME release_artifact_tests COMMAND ${Python3_EXECUTABLE} tests/test_release_artifacts.py --cli $<TARGET_FILE:quant_cli>)
    set_tests_properties(release_artifact_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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
