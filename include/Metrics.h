#pragma once

#include "Portfolio.h"

#include <string>
#include <vector>

struct PerformanceSummary {
    int schema_version{2};
    std::string ticker;
    std::string strategy;
    std::string parameter_set;
    std::string benchmark_ticker;
    std::string benchmark_execution_policy;
    std::string benchmark_cost_policy;
    std::string excess_return_basis{"net_strategy_minus_net_benchmark"};
    double total_return{0.0};
    double benchmark_gross_return{0.0};
    double benchmark_net_return{0.0};
    double excess_return{0.0};
    double annualized_return{0.0};
    double volatility{0.0};
    double sharpe{0.0};
    double max_drawdown{0.0};
    double win_rate{0.0};
    double profit_factor{0.0};
    int num_trades{0};
    double turnover{0.0};
    double total_transaction_costs{0.0};
    double cost_drag{0.0};
    double average_trade_return{0.0};
};

class Metrics {
public:
    static PerformanceSummary calculate(
        const std::string& ticker,
        const std::string& strategy,
        const std::string& parameter_set,
        double starting_capital,
        const std::vector<EquityPoint>& equity_curve,
        const std::vector<Trade>& trades,
        double benchmark_gross_return,
        double benchmark_net_return,
        const std::string& benchmark_ticker = "same_asset",
        const std::string& benchmark_execution_policy = "first_close_decision_next_open_integer_shares",
        const std::string& benchmark_cost_policy = "strategy_costs_for_net_zero_costs_for_gross");
};
