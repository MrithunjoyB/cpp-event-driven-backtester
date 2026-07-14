#include "Metrics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {
double mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

double sample_stddev(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double avg = mean(values);
    double variance = 0.0;
    for (double value : values) {
        double diff = value - avg;
        variance += diff * diff;
    }
    return std::sqrt(variance / static_cast<double>(values.size() - 1));
}
}

PerformanceSummary Metrics::calculate(
    const std::string& ticker,
    const std::string& strategy,
    const std::string& parameter_set,
    double starting_capital,
    const std::vector<EquityPoint>& equity_curve,
    const std::vector<Trade>& trades,
    double benchmark_gross_return,
    double benchmark_net_return,
    const std::string& benchmark_ticker,
    const std::string& benchmark_execution_policy,
    const std::string& benchmark_cost_policy) {
    PerformanceSummary summary;
    summary.ticker = ticker;
    summary.strategy = strategy;
    summary.parameter_set = parameter_set;
    summary.benchmark_ticker = benchmark_ticker;
    summary.benchmark_execution_policy = benchmark_execution_policy;
    summary.benchmark_cost_policy = benchmark_cost_policy;
    summary.num_trades = static_cast<int>(trades.size());
    summary.benchmark_gross_return = benchmark_gross_return;
    summary.benchmark_net_return = benchmark_net_return;

    if (equity_curve.empty() || starting_capital <= 0.0) {
        return summary;
    }

    double ending_value = equity_curve.back().portfolio_value;
    summary.total_return = (ending_value / starting_capital) - 1.0;
    summary.excess_return = summary.total_return - summary.benchmark_net_return;

    std::vector<double> returns;
    for (std::size_t i = 1; i < equity_curve.size(); ++i) {
        double prev = equity_curve[i - 1].portfolio_value;
        double curr = equity_curve[i].portfolio_value;
        if (prev > 0.0) {
            returns.push_back((curr / prev) - 1.0);
        }
    }

    double avg_daily_return = mean(returns);
    double daily_vol = sample_stddev(returns);
    summary.volatility = daily_vol * std::sqrt(252.0);
    summary.sharpe = summary.volatility > 0.0 ? (avg_daily_return * 252.0) / summary.volatility : 0.0;

    double years = std::max(1.0 / 252.0, static_cast<double>(equity_curve.size()) / 252.0);
    summary.annualized_return = std::pow(1.0 + summary.total_return, 1.0 / years) - 1.0;

    summary.max_drawdown = 0.0;
    for (const auto& point : equity_curve) {
        summary.max_drawdown = std::min(summary.max_drawdown, point.drawdown);
    }

    int wins = 0;
    int closed_trades = 0;
    double gross_profit = 0.0;
    double gross_loss = 0.0;
    double trade_return_sum = 0.0;
    double total_costs = 0.0;
    double gross_turnover = 0.0;

    for (const auto& trade : trades) {
        total_costs += trade.cost + trade.slippage;
        gross_turnover += trade.price * trade.quantity;
        if (trade.action == "SELL") {
            ++closed_trades;
            trade_return_sum += trade.trade_return;
            if (trade.realized_pnl > 0.0) {
                ++wins;
                gross_profit += trade.realized_pnl;
            } else if (trade.realized_pnl < 0.0) {
                gross_loss += std::abs(trade.realized_pnl);
            }
        }
    }

    summary.win_rate = closed_trades > 0 ? static_cast<double>(wins) / closed_trades : 0.0;
    summary.profit_factor = gross_loss > 0.0 ? gross_profit / gross_loss : (gross_profit > 0.0 ? 999999.0 : 0.0);
    summary.average_trade_return = closed_trades > 0 ? trade_return_sum / closed_trades : 0.0;
    summary.turnover = starting_capital > 0.0 ? gross_turnover / starting_capital : 0.0;
    summary.total_transaction_costs = total_costs;
    summary.cost_drag = starting_capital > 0.0 ? total_costs / starting_capital : 0.0;

    return summary;
}
