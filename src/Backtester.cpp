#include "Backtester.h"

#include <limits>
#include <optional>
#include <stdexcept>

namespace {
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
    return run_detailed(strategy_template).summary;
}

BacktestResult Backtester::run_detailed(const Strategy& strategy_template) {
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
        if (in_window(history[i], config_)) {
            if (first_index == std::numeric_limits<std::size_t>::max()) {
                first_index = i;
            }
            last_index = i;
        }
    }

    for (std::size_t i = 0; i < history.size(); ++i) {
        if (!in_window(history[i], config_)) {
            continue;
        }
        MarketEvent market_event = market_data.market_event(config_.ticker, i);

        if (pending_order && pending_order->quantity > 0) {
            pending_order->date = market_event.date;
            FillEvent fill = execution.execute_order(*pending_order, market_event.open);
            portfolio.process_fill(fill, market_event.open);
            pending_order.reset();
        }

        SignalEvent signal = strategy->on_market_event(market_event, history);
        OrderEvent order = portfolio.generate_order(signal, market_event.close);

        if (order.quantity > 0 && i < last_index) {
            pending_order = order;
        }
        if (config_.liquidate_at_end && i == last_index && portfolio.position() > 0) {
            SignalEvent liquidation_signal{EventType::Signal, market_event.date, config_.ticker, strategy->name(), SignalType::Sell};
            OrderEvent liquidation_order = portfolio.generate_order(liquidation_signal, market_event.close);
            FillEvent liquidation_fill = execution.execute_order(liquidation_order, market_event.close);
            portfolio.process_fill(liquidation_fill, market_event.close);
        }
        portfolio.mark_to_market(market_event.date, market_event.close);
    }

    BenchmarkResult bench = first_index == std::numeric_limits<std::size_t>::max()
        ? BenchmarkResult{}
        : benchmark_return(history[first_index].date, history[last_index].date);

    PerformanceSummary summary = Metrics::calculate(
        config_.ticker,
        strategy->name(),
        strategy->parameters(),
        portfolio.starting_capital(),
        portfolio.equity_curve(),
        portfolio.trades(),
        bench.gross_return,
        bench.net_return,
        bench.ticker,
        bench.execution_policy,
        bench.cost_policy);

    return BacktestResult{summary, portfolio.trades(), portfolio.equity_curve()};
}

BenchmarkResult Backtester::benchmark_return(const std::string& start_date, const std::string& end_date) const {
    const std::string ticker = config_.benchmark_ticker == "same_asset" ? config_.ticker : config_.benchmark_ticker;
    if (ticker.empty()) {
        throw std::runtime_error("Benchmark must be 'same_asset' or a ticker symbol");
    }
    MarketData benchmark_data;
    const std::string path = config_.data_dir + "/" + ticker + ".csv";
    if (!benchmark_data.load_csv(ticker, path)) {
        throw std::runtime_error("Could not load configured benchmark " + ticker + " from " + path);
    }
    std::vector<Bar> bars;
    for (const auto& bar : benchmark_data.bars(ticker)) {
        if (bar.date >= start_date && bar.date <= end_date) {
            bars.push_back(bar);
        }
    }
    if (bars.size() < 2) {
        throw std::runtime_error("Configured benchmark " + ticker + " has fewer than two observations in " + start_date + " to " + end_date);
    }
    auto simulate = [&](double commission, double slippage) {
        Portfolio portfolio(config_.starting_capital);
        ExecutionHandler execution(commission, slippage);
        portfolio.mark_to_market(bars.front().date, bars.front().close);
        SignalEvent signal{EventType::Signal, bars.front().date, ticker, "Buy_And_Hold_Benchmark", SignalType::Buy};
        OrderEvent order = portfolio.generate_order(signal, bars.front().close);
        order.date = bars[1].date;
        FillEvent fill = execution.execute_order(order, bars[1].open);
        portfolio.process_fill(fill, bars[1].open);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            if (config_.liquidate_at_end && i + 1 == bars.size() && portfolio.position() > 0) {
                SignalEvent sell_signal{EventType::Signal, bars[i].date, ticker, "Buy_And_Hold_Benchmark", SignalType::Sell};
                OrderEvent sell_order = portfolio.generate_order(sell_signal, bars[i].close);
                FillEvent sell_fill = execution.execute_order(sell_order, bars[i].close);
                portfolio.process_fill(sell_fill, bars[i].close);
            }
            portfolio.mark_to_market(bars[i].date, bars[i].close);
        }
        return portfolio.current_value() / config_.starting_capital - 1.0;
    };
    return BenchmarkResult{
        simulate(0.0, 0.0),
        simulate(config_.transaction_cost_rate, config_.slippage_rate),
        ticker,
        "first_close_decision_next_open_integer_shares_5pct_cash_reserve",
        "strategy_costs_for_net_zero_costs_for_gross"};
}
