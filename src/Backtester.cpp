#include "Backtester.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
std::string sanitize(const std::string& value) {
    std::string out = value;
    for (char& ch : out) {
        if (ch == ' ' || ch == '-' || ch == '/') {
            ch = '_';
        }
    }
    return out;
}

void write_summary_row(std::ofstream& out, const PerformanceSummary& s) {
    out << s.ticker << ','
        << s.strategy << ','
        << s.total_return << ','
        << s.annualized_return << ','
        << s.volatility << ','
        << s.sharpe << ','
        << s.max_drawdown << ','
        << s.win_rate << ','
        << s.profit_factor << ','
        << s.num_trades << ','
        << s.average_trade_return << ','
        << s.transaction_cost_adjusted_return << '\n';
}
}

Backtester::Backtester(BacktestConfig config) : config_(std::move(config)) {}

PerformanceSummary Backtester::run(const Strategy& strategy_template) {
    ensure_results_dir();

    MarketData market_data;
    std::string filepath = config_.data_dir + "/" + config_.ticker + ".csv";
    if (!market_data.load_csv(config_.ticker, filepath)) {
        throw std::runtime_error("Could not load data for " + config_.ticker + " from " + filepath);
    }

    auto strategy = strategy_template.clone();
    Portfolio portfolio(config_.starting_capital);
    ExecutionHandler execution(config_.transaction_cost_rate, config_.slippage_rate);
    const auto& history = market_data.bars(config_.ticker);

    for (std::size_t i = 0; i < history.size(); ++i) {
        MarketEvent market_event = market_data.market_event(config_.ticker, i);
        SignalEvent signal = strategy->on_market_event(market_event, history);
        OrderEvent order = portfolio.generate_order(signal, market_event.close);

        if (order.quantity > 0) {
            FillEvent fill = execution.execute_order(order, market_event.close);
            portfolio.process_fill(fill, market_event.close);
        }
        portfolio.mark_to_market(market_event.date, market_event.close);
    }

    const std::string prefix = result_prefix(strategy->name());
    write_trades(config_.results_dir + "/" + prefix + "_trades.csv", portfolio.trades());
    write_equity_curve(config_.results_dir + "/" + prefix + "_equity_curve.csv", portfolio.equity_curve());

    PerformanceSummary summary = Metrics::calculate(
        config_.ticker,
        strategy->name(),
        portfolio.starting_capital(),
        portfolio.equity_curve(),
        portfolio.trades());

    write_summary(config_.results_dir + "/" + prefix + "_performance_summary.csv", summary);

    // Convenience outputs for the most recent run.
    write_trades(config_.results_dir + "/trades.csv", portfolio.trades());
    write_equity_curve(config_.results_dir + "/equity_curve.csv", portfolio.equity_curve());
    write_summary(config_.results_dir + "/performance_summary.csv", summary);

    return summary;
}

void Backtester::write_combined_summary(const std::string& filepath, const std::vector<PerformanceSummary>& summaries) {
    std::ofstream out(filepath);
    out << std::fixed << std::setprecision(6);
    out << "ticker,strategy,total_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,average_trade_return,transaction_cost_adjusted_return\n";
    for (const auto& summary : summaries) {
        write_summary_row(out, summary);
    }
}

void Backtester::ensure_results_dir() const {
    std::filesystem::create_directories(config_.results_dir);
}

void Backtester::write_trades(const std::string& filepath, const std::vector<Trade>& trades) const {
    std::ofstream out(filepath);
    out << std::fixed << std::setprecision(6);
    out << "date,ticker,strategy,action,price,quantity,cost,slippage,portfolio_value,realized_pnl,trade_return\n";
    for (const auto& trade : trades) {
        out << trade.date << ','
            << trade.ticker << ','
            << trade.strategy << ','
            << trade.action << ','
            << trade.price << ','
            << trade.quantity << ','
            << trade.cost << ','
            << trade.slippage << ','
            << trade.portfolio_value << ','
            << trade.realized_pnl << ','
            << trade.trade_return << '\n';
    }
}

void Backtester::write_equity_curve(const std::string& filepath, const std::vector<EquityPoint>& equity_curve) const {
    std::ofstream out(filepath);
    out << std::fixed << std::setprecision(6);
    out << "date,portfolio_value,cash,holdings,total_return,drawdown\n";
    for (const auto& point : equity_curve) {
        out << point.date << ','
            << point.portfolio_value << ','
            << point.cash << ','
            << point.holdings << ','
            << point.total_return << ','
            << point.drawdown << '\n';
    }
}

void Backtester::write_summary(const std::string& filepath, const PerformanceSummary& summary) const {
    std::ofstream out(filepath);
    out << std::fixed << std::setprecision(6);
    out << "ticker,strategy,total_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,average_trade_return,transaction_cost_adjusted_return\n";
    write_summary_row(out, summary);
}

std::string Backtester::result_prefix(const std::string& strategy_name) const {
    return sanitize(config_.ticker) + "_" + sanitize(strategy_name);
}

