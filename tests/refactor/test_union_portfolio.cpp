#include "PortfolioBacktester.h"

#include <cmath>
#include <cstdint>
#include <cstring>
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

std::uint64_t double_bits(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double width");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
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
    PortfolioBacktestConfig deterministic;
    deterministic.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH", "SYN_EQ_C", "SYN_CRYPTO"};
    deterministic.benchmark_ticker = "SYN_BENCH";
    deterministic.data_dir = "data/synthetic";
    deterministic.calendar.mode = quant::market_data::CalendarMode::Union;
    deterministic.result_schema_version = 3;
    const auto deterministic_result = PortfolioBacktester(deterministic).run();
    if (deterministic_result.equity_curve.size() < 3 ||
        deterministic_result.equity_curve[2].date != "2019-01-03") return 1;

    constexpr double quantities[] = {201.0, 249.0, 100.0, 440.0, 3.0};
    constexpr double marks[] = {93.318433, 75.371400, 185.618712, 42.206817, 4677.051804};
    double fused_holdings = 0.0;
    double separately_rounded_holdings = 0.0;
    for (std::size_t i = 0; i < 5; ++i) {
        fused_holdings = std::fma(quantities[i], marks[i], fused_holdings);
        volatile double marked_value = quantities[i] * marks[i];
        separately_rounded_holdings += marked_value;
    }
    if (fused_holdings == separately_rounded_holdings ||
        double_bits(fused_holdings) != UINT64_C(0x40f5a70827d566cf) ||
        double_bits(deterministic_result.equity_curve[2].total_holdings_value) != double_bits(fused_holdings)) return 1;
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
