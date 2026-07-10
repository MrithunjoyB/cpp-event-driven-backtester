#include "ExecutionHandler.h"
#include "MarketData.h"
#include "Metrics.h"
#include "Portfolio.h"
#include "Strategy.h"
#include "Backtester.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>

namespace {
bool nearly_equal(double lhs, double rhs, double tolerance = 1e-6) {
    return std::fabs(lhs - rhs) <= tolerance;
}

std::vector<Bar> sample_bars() {
    return {
        {"2024-01-01", 100.0, 101.0, 99.0, 100.0, 1000},
        {"2024-01-02", 101.0, 103.0, 100.0, 102.0, 1000},
        {"2024-01-03", 102.0, 104.0, 101.0, 104.0, 1000},
        {"2024-01-04", 104.0, 105.0, 102.0, 103.0, 1000},
        {"2024-01-05", 103.0, 106.0, 102.0, 105.0, 1000}
    };
}
}

int main() {
    const auto bars = sample_bars();

    assert(nearly_equal(simple_moving_average(bars, 4, 3), (104.0 + 103.0 + 105.0) / 3.0));
    assert(nearly_equal(simple_moving_average(bars, 4, 10), 0.0));
    assert(nearly_equal(rsi(bars, 4, 3), 80.0));

    const auto returns = daily_returns({100.0, 110.0, 99.0});
    assert(returns.size() == 2);
    assert(nearly_equal(returns[0], 0.10));
    assert(nearly_equal(returns[1], -0.10));

    ExecutionHandler execution(0.001, 0.0005);
    OrderEvent buy_order{EventType::Order, "2024-01-05", "TEST", "Unit", OrderSide::Buy, 10};
    FillEvent fill = execution.execute_order(buy_order, 100.0);
    assert(fill.quantity == 10);
    assert(nearly_equal(fill.fill_price, 100.05));
    assert(nearly_equal(fill.transaction_cost, 1.0005));
    assert(nearly_equal(fill.slippage_cost, 0.5));

    OrderEvent sell_order{EventType::Order, "2024-01-05", "TEST", "Unit", OrderSide::Sell, 10};
    FillEvent sell_fill = execution.execute_order(sell_order, 100.0);
    assert(nearly_equal(sell_fill.fill_price, 99.95));
    assert(nearly_equal(sell_fill.transaction_cost, 0.9995));
    assert(nearly_equal(sell_fill.slippage_cost, 0.5));

    Portfolio portfolio(10000.0);
    FillEvent too_expensive = execution.execute_order(
        OrderEvent{EventType::Order, "2024-01-05", "TEST", "Unit", OrderSide::Buy, 100000}, 100.0);
    assert(!portfolio.process_fill(too_expensive, 100.0));
    assert(portfolio.position() == 0);
    assert(portfolio.process_fill(fill, 100.0));
    portfolio.mark_to_market("2024-01-05", 101.0);
    assert(portfolio.position() == 10);
    assert(portfolio.cash() < 10000.0);
    assert(!portfolio.equity_curve().empty());
    FillEvent invalid_sale = execution.execute_order(
        OrderEvent{EventType::Order, "2024-01-06", "TEST", "Unit", OrderSide::Sell, 11}, 100.0);
    assert(!portfolio.process_fill(invalid_sale, 100.0));
    assert(portfolio.position() == 10);

    std::vector<EquityPoint> equity = {
        {"2024-01-01", 10000.0, 10000.0, 0.0, 0.0, 0.0},
        {"2024-01-02", 10100.0, 10000.0, 100.0, 0.01, 0.0},
        {"2024-01-03", 9900.0, 9800.0, 100.0, -0.01, -0.019802},
        {"2024-01-04", 10300.0, 10200.0, 100.0, 0.03, 0.0}
    };
    std::vector<Trade> trades = {
        {"2024-01-02", "TEST", "Unit", "BUY", 100.0, 10, 1.0, 0.5, 10000.0, 0.0, 0.0},
        {"2024-01-04", "TEST", "Unit", "SELL", 103.0, 10, 1.0, 0.5, 10300.0, 28.5, 0.0285}
    };
    PerformanceSummary summary = Metrics::calculate("TEST", "Unit", "default", 10000.0, equity, trades, 0.02);
    assert(nearly_equal(summary.total_return, 0.03));
    assert(nearly_equal(summary.benchmark_return, 0.02));
    assert(nearly_equal(summary.excess_return, 0.01));
    assert(summary.num_trades == 2);
    assert(nearly_equal(summary.win_rate, 1.0));
    assert(nearly_equal(summary.max_drawdown, -0.019802, 1e-6));

    std::ofstream csv("NEXT.csv");
    csv << "Date,Open,High,Low,Close,Volume\n"
        << "2024-01-01,100,101,99,100,1000\n"
        << "2024-01-02,101,103,100,102,1000\n"
        << "2024-01-03,103,112,102,111,1000\n"
        << "2024-01-04,112,113,111,112,1000\n";
    csv.close();

    MarketData market_data;
    assert(market_data.load_csv("NEXT", "NEXT.csv"));
    class BuyOnSecondBar : public Strategy {
    public:
        std::string name() const override { return "BuyOnSecondBar"; }
        std::string parameters() const override { return "unit"; }
        SignalEvent on_market_event(const MarketEvent& event, const std::vector<Bar>&) override {
            return SignalEvent{EventType::Signal, event.date, event.ticker, name(), event.index == 1 ? SignalType::Buy : SignalType::Hold};
        }
        std::unique_ptr<Strategy> clone() const override { return std::make_unique<BuyOnSecondBar>(*this); }
    };

    BacktestConfig config;
    config.ticker = "NEXT";
    config.data_dir = ".";
    config.results_dir = "test_results";
    config.transaction_cost_rate = 0.0;
    config.slippage_rate = 0.0;
    BacktestResult result = Backtester(config).run_detailed(BuyOnSecondBar(), false);
    assert(!result.trades.empty());
    assert(result.trades.front().date == "2024-01-03");
    assert(nearly_equal(result.trades.front().price, 103.0));

    std::cout << "All engine tests passed.\n";
    return 0;
}
