#pragma once

#include "ExecutionHandler.h"
#include "MarketData.h"
#include "Metrics.h"
#include "Portfolio.h"
#include "Strategy.h"

#include <memory>
#include <string>
#include <vector>

struct BacktestConfig {
    std::string ticker{"AAPL"};
    double starting_capital{100000.0};
    double transaction_cost_rate{0.001};
    double slippage_rate{0.0005};
    std::string data_dir{"data"};
    std::string results_dir{"results"};
};

class Backtester {
public:
    explicit Backtester(BacktestConfig config);

    PerformanceSummary run(const Strategy& strategy);
    static void write_combined_summary(const std::string& filepath, const std::vector<PerformanceSummary>& summaries);

private:
    BacktestConfig config_;

    void ensure_results_dir() const;
    void write_trades(const std::string& filepath, const std::vector<Trade>& trades) const;
    void write_equity_curve(const std::string& filepath, const std::vector<EquityPoint>& equity_curve) const;
    void write_summary(const std::string& filepath, const PerformanceSummary& summary) const;
    std::string result_prefix(const std::string& strategy_name) const;
};

