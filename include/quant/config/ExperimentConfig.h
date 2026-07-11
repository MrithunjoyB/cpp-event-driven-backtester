#pragma once

#include <string>
#include <vector>

namespace quant::config {

struct ExecutionConfig {
    double starting_capital{100000.0};
    double commission_bps{10.0};
    double slippage_bps{5.0};
};

struct WalkForwardConfig {
    std::string window_mode{"calendar_duration"};
    int train_days{756};
    int test_days{126};
    int step_days{126};
    int train_years{3};
    int test_months{6};
    int step_months{6};
    std::string continuity_policy{"continuous_capital"};
    std::string boundary_position_policy{"liquidate_at_test_end_close"};
};

struct BenchmarkConfig {
    std::string ticker{"SPY"};
};

struct RegimeConfig {
    std::string method{"trend_200_sma_60_return_vol_20_expanding_median"};
};

struct BootstrapConfig {
    unsigned int random_seed{42};
};

struct PortfolioConfig {
    std::string allocation_policy;
    std::string rebalance_frequency{"monthly"};
    double max_weight{0.40};
    double cash_buffer{0.02};
    double min_trade_value{25.0};
    int volatility_lookback{60};
    int momentum_lookback{126};
    int top_n{3};
};

struct OutputConfig {
    std::string results_dir{"results/research/default_research"};
    std::string portfolio_results_dir{"results/portfolio"};
};

struct ParameterSelectionConfig {
    std::string objective{"sharpe_min_trades"};
    int minimum_trades{3};
};

struct ExperimentConfig {
    std::string name{"default_research"};
    std::vector<std::string> tickers{"AAPL", "MSFT", "SPY", "TSLA", "BTC-USD"};
    std::string strategy{"MA_Cross"};
    ExecutionConfig execution;
    WalkForwardConfig walk_forward;
    BenchmarkConfig benchmark;
    RegimeConfig regime;
    BootstrapConfig bootstrap;
    PortfolioConfig portfolio;
    OutputConfig output;
    ParameterSelectionConfig parameter_selection;
    int result_schema_version{2};
};

}  // namespace quant::config
