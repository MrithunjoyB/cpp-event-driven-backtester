#pragma once

#include "Backtester.h"
#include "Strategy.h"
#include "quant/config/ExperimentConfig.h"

#include <memory>
#include <string>
#include <vector>

struct StrategySpec {
    std::string strategy;
    std::string parameter_set;
    std::unique_ptr<Strategy> instance;
};

struct BenchmarkTiming {
    std::string benchmark;
    double milliseconds{0.0};
    std::size_t observations{0};
};

using ExperimentConfig = quant::config::ExperimentConfig;

class Analysis {
public:
    static ExperimentConfig load_experiment_config(const std::string& filepath);
    static std::vector<StrategySpec> default_strategy_specs();
    static std::vector<StrategySpec> grid_strategy_specs();
    static std::vector<StrategySpec> grid_strategy_specs(const std::string& strategy_name);
    static std::vector<std::string> default_tickers();

    static std::vector<PerformanceSummary> run_cross_asset(const BacktestConfig& base_config);
    static std::vector<PerformanceSummary> run_parameter_grid(const BacktestConfig& base_config, const std::vector<std::string>& tickers);
    static void run_walk_forward(const BacktestConfig& base_config, const std::vector<std::string>& tickers);
    static void run_transaction_cost_sensitivity(const BacktestConfig& base_config, const std::vector<std::string>& tickers);
    static void run_regime_evaluation(const BacktestConfig& base_config, const std::vector<std::string>& tickers);
    static std::vector<BenchmarkTiming> run_performance_benchmarks(const BacktestConfig& base_config);
    static void run_research_experiment(const ExperimentConfig& experiment);
    static void run_bootstrap_research(const ExperimentConfig& experiment);

private:
    static void write_benchmark_timings(const std::string& filepath, const std::vector<BenchmarkTiming>& timings);
};
