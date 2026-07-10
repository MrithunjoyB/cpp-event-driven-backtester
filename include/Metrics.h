#pragma once

#include "Portfolio.h"

#include <string>
#include <vector>

struct PerformanceSummary {
    std::string ticker;
    std::string strategy;
    double total_return{0.0};
    double annualized_return{0.0};
    double volatility{0.0};
    double sharpe{0.0};
    double max_drawdown{0.0};
    double win_rate{0.0};
    double profit_factor{0.0};
    int num_trades{0};
    double average_trade_return{0.0};
    double transaction_cost_adjusted_return{0.0};
};

class Metrics {
public:
    static PerformanceSummary calculate(
        const std::string& ticker,
        const std::string& strategy,
        double starting_capital,
        const std::vector<EquityPoint>& equity_curve,
        const std::vector<Trade>& trades);
};

