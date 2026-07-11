#include "quant/io/ResultExporter.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace quant::io {
namespace {
std::string sanitize(const std::string& value) {
    std::string output = value;
    for (char& ch : output) {
        if (ch == ' ' || ch == '-' || ch == '/') {
            ch = '_';
        }
    }
    return output;
}

std::ofstream open_output(const std::string& filepath) {
    std::ofstream output(filepath);
    if (!output.is_open()) {
        throw std::runtime_error("Could not open result file for writing: " + filepath);
    }
    output << std::fixed << std::setprecision(6);
    return output;
}

void verify_output(const std::ofstream& output, const std::string& filepath) {
    if (!output.good()) {
        throw std::runtime_error("Failed while writing result file: " + filepath);
    }
}

void write_summary_header(std::ofstream& output) {
    output << "schema_version,ticker,strategy,parameter_set,benchmark_ticker,benchmark_execution_policy,benchmark_cost_policy,excess_return_basis,total_return,benchmark_gross_return,benchmark_net_return,excess_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,turnover,total_transaction_costs,cost_drag,average_trade_return\n";
}

void write_summary_row(std::ofstream& output, const PerformanceSummary& summary) {
    output << summary.schema_version << ',' << summary.ticker << ',' << summary.strategy << ','
           << summary.parameter_set << ',' << summary.benchmark_ticker << ','
           << summary.benchmark_execution_policy << ',' << summary.benchmark_cost_policy << ','
           << summary.excess_return_basis << ',' << summary.total_return << ','
           << summary.benchmark_gross_return << ',' << summary.benchmark_net_return << ','
           << summary.excess_return << ',' << summary.annualized_return << ',' << summary.volatility << ','
           << summary.sharpe << ',' << summary.max_drawdown << ',' << summary.win_rate << ','
           << summary.profit_factor << ',' << summary.num_trades << ',' << summary.turnover << ','
           << summary.total_transaction_costs << ',' << summary.cost_drag << ','
           << summary.average_trade_return << '\n';
}
}

void CsvResultExporter::write_backtest(const BacktestResult& result, const std::string& results_dir) {
    std::error_code error;
    std::filesystem::create_directories(results_dir, error);
    if (error) {
        throw std::runtime_error("Could not create result directory " + results_dir + ": " + error.message());
    }
    const std::string prefix = sanitize(result.summary.ticker) + "_" + sanitize(result.summary.strategy);
    const std::vector<std::string> trade_paths = {
        results_dir + "/" + prefix + "_trades.csv", results_dir + "/trades.csv"};
    const std::vector<std::string> equity_paths = {
        results_dir + "/" + prefix + "_equity_curve.csv", results_dir + "/equity_curve.csv"};
    const std::vector<std::string> summary_paths = {
        results_dir + "/" + prefix + "_performance_summary.csv", results_dir + "/performance_summary.csv"};

    for (const auto& path : trade_paths) {
        auto output = open_output(path);
        output << "date,ticker,strategy,action,price,quantity,cost,slippage,portfolio_value,realized_pnl,trade_return\n";
        for (const auto& trade : result.trades) {
            output << trade.date << ',' << trade.ticker << ',' << trade.strategy << ',' << trade.action << ','
                   << trade.price << ',' << trade.quantity << ',' << trade.cost << ',' << trade.slippage << ','
                   << trade.portfolio_value << ',' << trade.realized_pnl << ',' << trade.trade_return << '\n';
        }
        verify_output(output, path);
    }
    for (const auto& path : equity_paths) {
        auto output = open_output(path);
        output << "date,portfolio_value,cash,holdings,total_return,drawdown\n";
        for (const auto& point : result.equity_curve) {
            output << point.date << ',' << point.portfolio_value << ',' << point.cash << ',' << point.holdings << ','
                   << point.total_return << ',' << point.drawdown << '\n';
        }
        verify_output(output, path);
    }
    for (const auto& path : summary_paths) {
        auto output = open_output(path);
        write_summary_header(output);
        write_summary_row(output, result.summary);
        verify_output(output, path);
    }
}

void CsvResultExporter::write_combined_summary(
    const std::string& filepath,
    const std::vector<PerformanceSummary>& summaries) {
    auto output = open_output(filepath);
    write_summary_header(output);
    for (const auto& summary : summaries) {
        write_summary_row(output, summary);
    }
    verify_output(output, filepath);
}

void CsvResultExporter::write_portfolio(
    const PortfolioBacktestResult& result,
    const PortfolioBacktestConfig& config) {
    std::error_code error;
    std::filesystem::create_directories(config.results_dir, error);
    if (error) {
        throw std::runtime_error("Could not create portfolio result directory " + config.results_dir + ": " + error.message());
    }
    auto equity = open_output(config.results_dir + "/portfolio_equity_curve.csv");
    auto positions = open_output(config.results_dir + "/portfolio_positions.csv");
    auto orders = open_output(config.results_dir + "/portfolio_orders.csv");
    auto fills = open_output(config.results_dir + "/portfolio_fills.csv");
    auto rebalances = open_output(config.results_dir + "/portfolio_rebalances.csv");
    auto weights = open_output(config.results_dir + "/portfolio_allocation_weights.csv");
    auto costs = open_output(config.results_dir + "/portfolio_costs.csv");
    auto summary = open_output(config.results_dir + "/portfolio_performance_summary.csv");

    equity << "date,portfolio_value,cash,total_holdings_value,total_return,drawdown,gross_exposure\n";
    for (const auto& point : result.equity_curve) {
        equity << point.date << ',' << point.portfolio_value << ',' << point.cash << ',' << point.total_holdings_value
               << ',' << point.total_return << ',' << point.drawdown << ',' << point.gross_exposure << '\n';
    }
    positions << "date,ticker,quantity,price,market_value,target_weight,actual_weight,rebalance_id\n";
    for (const auto& point : result.positions) {
        positions << point.date << ',' << point.ticker << ',' << point.quantity << ',' << point.price << ','
                  << point.market_value << ',' << point.target_weight << ',' << point.actual_weight << ','
                  << point.rebalance_id << '\n';
    }
    fills << "rebalance_id,date,ticker,side,quantity,price,transaction_cost,slippage_cost,cash_after\n";
    orders << "rebalance_id,date,ticker,side,quantity,price,transaction_cost,slippage_cost\n";
    costs << "rebalance_id,date,ticker,transaction_cost,slippage_cost,total_cost\n";
    for (const auto& fill : result.fills) {
        fills << fill.rebalance_id << ',' << fill.date << ',' << fill.ticker << ',' << fill.side << ','
              << fill.quantity << ',' << fill.price << ',' << fill.transaction_cost << ',' << fill.slippage_cost
              << ',' << fill.cash_after << '\n';
        orders << fill.rebalance_id << ',' << fill.date << ',' << fill.ticker << ',' << fill.side << ','
               << fill.quantity << ',' << fill.price << ',' << fill.transaction_cost << ',' << fill.slippage_cost << '\n';
        costs << fill.rebalance_id << ',' << fill.date << ',' << fill.ticker << ',' << fill.transaction_cost << ','
              << fill.slippage_cost << ',' << fill.transaction_cost + fill.slippage_cost << '\n';
    }
    rebalances << "rebalance_id,date,policy_name,frequency,turnover,cash_buffer,max_weight,min_trade_value\n";
    weights << "rebalance_id,policy_name,ticker,target_weight\n";
    for (std::size_t i = 0; i < result.target_weights.size(); ++i) {
        const std::string date = i < result.rebalance_dates.size() ? result.rebalance_dates[i] : "";
        rebalances << i << ',' << date << ',' << result.summary.policy_name << ','
                   << PortfolioBacktester::frequency_name(config.rebalance_frequency) << ','
                   << (i < result.turnover_by_rebalance.size() ? result.turnover_by_rebalance[i] : 0.0) << ','
                   << config.allocation.cash_buffer << ',' << config.allocation.max_weight << ','
                   << config.allocation.min_trade_value << '\n';
        for (const auto& weight : result.target_weights[i]) {
            weights << i << ',' << result.summary.policy_name << ',' << weight.first << ',' << weight.second << '\n';
        }
    }
    summary << "policy_name,total_return,equal_weight_benchmark_return,spy_benchmark_return,excess_return,annualized_return,volatility,sharpe,sortino,max_drawdown,calmar,var_95,expected_shortfall_95,beta,alpha,information_ratio,turnover,total_transaction_costs,number_of_rebalances,number_of_fills,average_cash_allocation,average_gross_exposure\n";
    const auto& value = result.summary;
    summary << value.policy_name << ',' << value.total_return << ',' << value.equal_weight_benchmark_return << ','
            << value.spy_benchmark_return << ',' << value.excess_return << ',' << value.annualized_return << ','
            << value.volatility << ',' << value.sharpe << ',' << value.sortino << ',' << value.max_drawdown << ','
            << value.calmar << ',' << value.var_95 << ',' << value.expected_shortfall_95 << ',' << value.beta << ','
            << value.alpha << ',' << value.information_ratio << ',' << value.turnover << ','
            << value.total_transaction_costs << ',' << value.number_of_rebalances << ',' << value.number_of_fills << ','
            << value.average_cash_allocation << ',' << value.average_gross_exposure << '\n';

    verify_output(equity, config.results_dir + "/portfolio_equity_curve.csv");
    verify_output(positions, config.results_dir + "/portfolio_positions.csv");
    verify_output(orders, config.results_dir + "/portfolio_orders.csv");
    verify_output(fills, config.results_dir + "/portfolio_fills.csv");
    verify_output(rebalances, config.results_dir + "/portfolio_rebalances.csv");
    verify_output(weights, config.results_dir + "/portfolio_allocation_weights.csv");
    verify_output(costs, config.results_dir + "/portfolio_costs.csv");
    verify_output(summary, config.results_dir + "/portfolio_performance_summary.csv");
}

}  // namespace quant::io
