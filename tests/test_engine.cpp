#include "ExecutionHandler.h"
#include "MarketData.h"
#include "Metrics.h"
#include "Portfolio.h"
#include "Strategy.h"

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
    assert(rsi(bars, 4, 3) > 0.0);

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

    Portfolio portfolio(10000.0);
    assert(portfolio.process_fill(fill, 100.0));
    portfolio.mark_to_market("2024-01-05", 101.0);
    assert(portfolio.position() == 10);
    assert(portfolio.cash() < 10000.0);
    assert(!portfolio.equity_curve().empty());

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
    PerformanceSummary summary = Metrics::calculate("TEST", "Unit", 10000.0, equity, trades);
    assert(nearly_equal(summary.total_return, 0.03));
    assert(summary.num_trades == 2);
    assert(nearly_equal(summary.win_rate, 1.0));
    assert(summary.max_drawdown < 0.0);

    std::cout << "All engine tests passed.\n";
    return 0;
}
