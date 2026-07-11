cmake_minimum_required(VERSION 3.16)
project(CppEventDrivenBacktester VERSION 2.0.0 LANGUAGES CXX)

option(QUANT_ENABLE_TESTS "Build deterministic test targets" ON)
option(QUANT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(QUANT_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings" ON)
option(QUANT_ENABLE_BENCHMARKS "Enable benchmark CLI functionality" ON)

set(QUANT_CORE_SOURCES
    src/MarketData.cpp
    src/Strategy.cpp
    src/Portfolio.cpp
    src/AllocationPolicy.cpp
    src/PortfolioBacktester.cpp
    src/ResearchMethodology.cpp
    src/ExecutionHandler.cpp
    src/Backtester.cpp
    src/Analysis.cpp
    src/Metrics.cpp
    src/io/ResultExporter.cpp
)

add_library(quant_core STATIC ${QUANT_CORE_SOURCES})
target_compile_features(quant_core PUBLIC cxx_std_17)
target_include_directories(quant_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

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

    if(QUANT_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        else()
            message(FATAL_ERROR "QUANT_ENABLE_SANITIZERS requires GCC or Clang")
        endif()
    endif()
endfunction()

quant_apply_project_options(quant_core)

add_executable(quant_cli apps/quant_cli.cpp)
target_link_libraries(quant_cli PRIVATE quant_core)
target_compile_features(quant_cli PRIVATE cxx_std_17)
target_compile_definitions(quant_cli PRIVATE QUANT_PROJECT_VERSION="${PROJECT_VERSION}")
quant_apply_project_options(quant_cli)

include(CTest)
if(BUILD_TESTING AND QUANT_ENABLE_TESTS)
    add_executable(quant_tests tests/test_engine.cpp)
    target_link_libraries(quant_tests PRIVATE quant_core)
    target_compile_features(quant_tests PRIVATE cxx_std_17)
    quant_apply_project_options(quant_tests)

    add_test(NAME quant_tests COMMAND quant_tests)
    set_tests_properties(quant_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
