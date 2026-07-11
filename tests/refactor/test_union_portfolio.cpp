#include "PortfolioBacktester.h"

#include <iostream>

namespace {
PortfolioBacktestConfig base() {
    PortfolioBacktestConfig config;
    config.tickers = {"MIXEQ", "MIXBTC"};
    config.benchmark_ticker = "MIXEQ";
    config.data_dir = "tests/fixtures/mixed";
    config.transaction_cost_rate = 0.0;
    config.slippage_rate = 0.0;
    config.allocation.max_weight = 0.5;
    config.allocation.cash_buffer = 0.0;
    config.allocation.min_trade_value = 0.0;
    return config;
}
}

int main() {
    auto union_config = base();
    union_config.calendar.mode = quant::market_data::CalendarMode::Union;
    union_config.result_schema_version = 3;
    const auto union_result = PortfolioBacktester(union_config).run();
    auto legacy_config = base();
    const auto legacy_result = PortfolioBacktester(legacy_config).run();
    if (union_result.equity_curve.size() != 8 || legacy_result.equity_curve.size() != 6) return 1;
    if (union_result.summary.weekend_observations != 2 || union_result.summary.observations_per_year < 300.0) return 1;
    if (union_result.summary.volatility == legacy_result.summary.volatility ||
        union_result.equity_curve[5].date != "2024-01-06" || union_result.equity_curve[5].drawdown >= 0.0) return 1;
    bool stale_equity_seen = false;
    for (const auto& mark : union_result.valuations) {
        if (mark.ticker == "MIXEQ" && mark.date == "2024-01-06")
            stale_equity_seen = !mark.tradable && mark.mark_source == "last_known_close" && mark.marked_value > 0.0;
    }
    if (!stale_equity_seen) return 1;
    for (const auto& fill : union_result.fills) {
        if (fill.ticker == "MIXEQ" && (fill.date == "2024-01-06" || fill.date == "2024-01-07")) return 1;
    }
    for (const auto& point : union_result.equity_curve) {
        if (point.portfolio_value < 0.0 || point.cash + point.total_holdings_value != point.portfolio_value) return 1;
    }
    auto deferred_config = base();
    deferred_config.tickers = {"DEFEQ", "DEFBTC"};
    deferred_config.benchmark_ticker = "DEFEQ";
    deferred_config.calendar.mode = quant::market_data::CalendarMode::Union;
    deferred_config.calendar.closed_asset_policy = quant::market_data::ClosedAssetPolicy::Defer;
    deferred_config.rebalance_frequency = RebalanceFrequency::Weekly;
    const auto deferred = PortfolioBacktester(deferred_config).run();
    if (deferred.rebalances.empty()) return 1;
    const auto& record = deferred.rebalances.front();
    if (record.scheduled_rebalance_date != "2024-01-06" || record.decision_date != "2024-01-05" ||
        record.execution_date != "2024-01-08") return 1;
    std::cout << "union_portfolio_tests passed\n";
    return 0;
}
