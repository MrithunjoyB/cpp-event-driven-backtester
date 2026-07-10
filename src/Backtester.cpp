#include "Backtester.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
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
        << s.parameter_set << ','
        << s.total_return << ','
        << s.benchmark_return << ','
        << s.excess_return << ','
        << s.annualized_return << ','
        << s.volatility << ','
        << s.sharpe << ','
        << s.max_drawdown << ','
        << s.win_rate << ','
        << s.profit_factor << ','
        << s.num_trades << ','
        << s.turnover << ','
        << s.total_transaction_costs << ','
        << s.average_trade_return << ','
        << s.transaction_cost_adjusted_return << '\n';
}

bool in_window(const Bar& bar, const BacktestConfig& config) {
    if (!config.start_date.empty() && bar.date < config.start_date) {
        return false;
    }
    if (!config.end_date.empty() && bar.date > config.end_date) {
        return false;
    }
    return true;
}
}

Backtester::Backtester(BacktestConfig config) : config_(std::move(config)) {}

PerformanceSummary Backtester::run(const Strategy& strategy_template) {
    return run_detailed(strategy_template, true).summary;
}

BacktestResult Backtester::run_detailed(const Strategy& strategy_template, bool write_outputs) {
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

    std::optional<OrderEvent> pending_order;
    std::size_t first_index = std::numeric_limits<std::size_t>::max();
    std::size_t last_index = 0;

    for (std::size_t i = 0; i < history.size(); ++i) {
        if (!in_window(history[i], config_)) {
            continue;
        }
        if (first_index == std::numeric_limits<std::size_t>::max()) {
            first_index = i;
        }
        last_index = i;
        MarketEvent market_event = market_data.market_event(config_.ticker, i);

        if (pending_order && pending_order->quantity > 0) {
            pending_order->date = market_event.date;
            FillEvent fill = execution.execute_order(*pending_order, market_event.open);
            portfolio.process_fill(fill, market_event.open);
            pending_order.reset();
        }

        SignalEvent signal = strategy->on_market_event(market_event, history);
        OrderEvent order = portfolio.generate_order(signal, market_event.close);

        if (order.quantity > 0 && i + 1 < history.size()) {
            pending_order = order;
        }
        portfolio.mark_to_market(market_event.date, market_event.close);
    }

    double bench = first_index == std::numeric_limits<std::size_t>::max()
        ? 0.0
        : benchmark_return(history, first_index, last_index);

    PerformanceSummary summary = Metrics::calculate(
        config_.ticker,
        strategy->name(),
        strategy->parameters(),
        portfolio.starting_capital(),
        portfolio.equity_curve(),
        portfolio.trades(),
        bench);

    if (write_outputs) {
        const std::string prefix = result_prefix(strategy->name());
        write_trades(config_.results_dir + "/" + prefix + "_trades.csv", portfolio.trades());
        write_equity_curve(config_.results_dir + "/" + prefix + "_equity_curve.csv", portfolio.equity_curve());
        write_summary(config_.results_dir + "/" + prefix + "_performance_summary.csv", summary);

        // Convenience outputs for the most recent run.
        write_trades(config_.results_dir + "/trades.csv", portfolio.trades());
        write_equity_curve(config_.results_dir + "/equity_curve.csv", portfolio.equity_curve());
        write_summary(config_.results_dir + "/performance_summary.csv", summary);
    }

    return BacktestResult{summary, portfolio.trades(), portfolio.equity_curve()};
}

void Backtester::write_combined_summary(const std::string& filepath, const std::vector<PerformanceSummary>& summaries) {
    std::ofstream out(filepath);
    out << std::fixed << std::setprecision(6);
    out << "ticker,strategy,parameter_set,total_return,benchmark_return,excess_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,turnover,total_transaction_costs,average_trade_return,transaction_cost_adjusted_return\n";
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
    out << "ticker,strategy,parameter_set,total_return,benchmark_return,excess_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,turnover,total_transaction_costs,average_trade_return,transaction_cost_adjusted_return\n";
    write_summary_row(out, summary);
}

std::string Backtester::result_prefix(const std::string& strategy_name) const {
    return sanitize(config_.ticker) + "_" + sanitize(strategy_name);
}

double Backtester::benchmark_return(const std::vector<Bar>& history, std::size_t start, std::size_t end) const {
    if (history.empty() || start >= history.size() || end >= history.size() || start >= end || history[start].close <= 0.0) {
        return 0.0;
    }
    return (history[end].close / history[start].close) - 1.0;
}
