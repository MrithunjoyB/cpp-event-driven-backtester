#pragma once

#include "Backtester.h"
#include "Strategy.h"

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

struct ExperimentConfig {
    std::string experiment_name{"default_research"};
    std::vector<std::string> tickers;
    std::string strategy{"MA_Cross"};
    double starting_capital{100000.0};
    double commission_bps{10.0};
    double slippage_bps{5.0};
    int train_days{756};
    int test_days{126};
    int step_days{126};
    std::string window_mode{"calendar_duration"};
    int train_years{3};
    int test_months{6};
    int step_months{6};
    std::string continuity_policy{"continuous_capital"};
    std::string boundary_position_policy{"liquidate_at_test_end_close"};
    std::string benchmark{"SPY"};
    std::string objective{"sharpe_min_trades"};
    int minimum_trades{3};
    std::string regime_method{"trend_200_sma_60_return_vol_20_expanding_median"};
    unsigned int random_seed{42};
    std::string output_dir{"results/research/default_research"};
    std::string allocation_policy;
    std::string rebalance_frequency{"monthly"};
    double max_weight{0.40};
    double cash_buffer{0.02};
    double min_trade_value{25.0};
    int volatility_lookback{60};
    int momentum_lookback{126};
    int top_n{3};
    std::string portfolio_output_dir{"results/portfolio"};
    int result_schema_version{2};
};

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
    static void run_portfolio_research(const ExperimentConfig& experiment, const std::string& policy);
    static void run_bootstrap_research(const ExperimentConfig& experiment);

private:
    static void write_benchmark_timings(const std::string& filepath, const std::vector<BenchmarkTiming>& timings);
};
