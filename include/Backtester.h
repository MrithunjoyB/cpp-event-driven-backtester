#pragma once

#include "ExecutionHandler.h"
#include "MarketData.h"
#include "Metrics.h"
#include "Portfolio.h"
#include "Strategy.h"

#include <memory>
#include <string>
#include <vector>

struct BenchmarkResult {
    double gross_return{0.0};
    double net_return{0.0};
    std::string ticker;
    std::string execution_policy{"first_close_decision_next_open_integer_shares"};
    std::string cost_policy{"strategy_costs_for_net_zero_costs_for_gross"};
};

struct BacktestConfig {
    std::string ticker{"AAPL"};
    double starting_capital{100000.0};
    double transaction_cost_rate{0.001};
    double slippage_rate{0.0005};
    std::string data_dir{"data"};
    std::string results_dir{"results"};
    std::string start_date;
    std::string end_date;
    std::string benchmark_ticker{"same_asset"};
    bool liquidate_at_end{false};
    std::shared_ptr<const MarketData> immutable_market_data;
    std::shared_ptr<const BenchmarkResult> benchmark_override;
};

struct BacktestResult {
    PerformanceSummary summary;
    std::vector<Trade> trades;
    std::vector<EquityPoint> equity_curve;
};

class Backtester {
public:
    explicit Backtester(BacktestConfig config);

    PerformanceSummary run(const Strategy& strategy);
    BacktestResult run_detailed(const Strategy& strategy);

private:
    BacktestConfig config_;

    BenchmarkResult benchmark_return(const std::string& start_date, const std::string& end_date) const;
};
