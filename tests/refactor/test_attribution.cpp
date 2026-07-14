#include "PortfolioBacktester.h"
#include "quant/analytics/PortfolioAttribution.h"
#include "quant/io/ResultExporter.h"
#include "quant/domain/Errors.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int cases = 0;
int assertions = 0;
std::uint64_t double_bits(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double width");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
void check(bool condition, const std::string& name) {
    ++cases; ++assertions;
    if (!condition) throw std::runtime_error("FAILED: " + name);
}
PortfolioBacktestConfig config(std::vector<std::string> tickers, std::string benchmark) {
    PortfolioBacktestConfig value;
    value.tickers = std::move(tickers);
    value.benchmark_ticker = std::move(benchmark);
    value.calendar.mode = quant::market_data::CalendarMode::Union;
    value.result_schema_version = 3;
    return value;
}
PortfolioBacktestResult simple_result(const std::vector<double>& values) {
    PortfolioBacktestResult result;
    const std::vector<std::string> dates{"2024-01-01", "2024-01-02", "2024-01-03"};
    double peak = values.front();
    for (std::size_t i = 0; i < values.size(); ++i) {
        peak = std::max(peak, values[i]);
        result.equity_curve.push_back({dates[i], values[i], 0.0, values[i], values[i] / values.front() - 1.0,
            values[i] / peak - 1.0, 1.0});
        result.valuations.push_back({dates[i], "ONE", true, true, values[i], "current_close", 0, 1.0, values[i], 1.0});
    }
    result.summary.policy_name = "fixture";
    result.summary.observations_per_year = 365.25;
    return result;
}
}

int main() {
    try {
        const auto one = quant::analytics::PortfolioAttributionAnalyzer::analyze(
            simple_result({100.0, 105.0, 110.0}), config({"ONE"}, "ONE"), "one", 1e-10);
        check(std::abs(one.total_residual) < 1e-10, "one asset residual");
        check(std::abs(one.assets[0].market_pnl - 5.0) < 1e-10, "pure appreciation");
        check(one.assets[0].trade_cash_flow == 0.0, "no trade flow");

        auto mixed_config = config({"MIXEQ", "MIXBTC"}, "MIXEQ");
        mixed_config.data_dir = "tests/fixtures/mixed";
        mixed_config.transaction_cost_rate = 0.001;
        mixed_config.slippage_rate = 0.0005;
        mixed_config.allocation.max_weight = 0.5;
        mixed_config.allocation.cash_buffer = 0.0;
        mixed_config.allocation.min_trade_value = 0.0;
        const auto mixed_portfolio = PortfolioBacktester(mixed_config).run();
        const auto mixed = quant::analytics::PortfolioAttributionAnalyzer::analyze(mixed_portfolio, mixed_config, "mixed", 1e-8);
        check(mixed.summary.size() >= 10, "two asset summary");
        check(std::abs(mixed.total_residual) < 1e-4, "two asset reconciliation");
        check(!mixed_portfolio.fills.empty(), "intra period buys");
        check(mixed.assets.front().trade_cash_flow != 0.0 || mixed.assets[1].trade_cash_flow != 0.0, "trade-aware flow");
        check(mixed_portfolio.fills.front().side == "BUY", "buy attribution");
        check(mixed.summary.end() != std::find_if(mixed.summary.begin(), mixed.summary.end(), [](const auto& row) { return row.component == "COMMISSION" && row.contribution < 0.0; }), "commission decomposition");
        check(mixed.summary.end() != std::find_if(mixed.summary.begin(), mixed.summary.end(), [](const auto& row) { return row.component == "SLIPPAGE" && row.contribution < 0.0; }), "slippage decomposition");
        check(mixed.cash.front().cash_return == 0.0, "cash return zero");
        check(mixed.cash.front().benchmark_drag <= 0.0 || mixed.cash.front().benchmark_drag >= 0.0, "cash drag finite");
        check(!mixed.rebalances.empty(), "rebalance attribution");
        check(mixed.rebalances.front().gross_trade_value > 0.0, "rebalance gross value");
        check(mixed.assets.end() != std::find_if(mixed.assets.begin(), mixed.assets.end(), [](const auto& row) { return row.stale_mark; }), "stale mark attribution");
        check(mixed.risk.size() == 2, "risk assets");
        double risk_sum = 0.0;
        for (const auto& row : mixed.risk) risk_sum += row.percentage_contribution;
        check(std::abs(risk_sum - 1.0) < 1e-6, "volatility contribution sum");
        check(std::isfinite(mixed.risk.front().beta_contribution), "benchmark beta contribution");
        check(!mixed.calendar_years.empty(), "calendar year aggregation");
        check(mixed.summary.end() != std::find_if(mixed.summary.begin(), mixed.summary.end(), [](const auto& row) { return row.component == "MIXBTC"; }), "BTC contribution");
        check(mixed.summary.end() == std::find_if(mixed.summary.begin(), mixed.summary.end(), [](const auto& row) { return row.component == "SYN_EQ_C"; }), "SYN_EQ_C absent from fixture");

        auto deterministic_config = config(
            {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH", "SYN_EQ_C", "SYN_CRYPTO"}, "SYN_BENCH");
        deterministic_config.data_dir = "data/synthetic";
        const auto deterministic_portfolio = PortfolioBacktester(deterministic_config).run();
        const auto deterministic_attribution = quant::analytics::PortfolioAttributionAnalyzer::analyze(
            deterministic_portfolio, deterministic_config, "deterministic", 1e-8);
        const auto rebalance = std::find_if(deterministic_attribution.rebalances.begin(),
            deterministic_attribution.rebalances.end(),
            [](const auto& row) { return row.rebalance_id == 34; });
        check(rebalance != deterministic_attribution.rebalances.end(), "deterministic rebalance fixture");
        double fused_gross = 0.0;
        double separately_rounded_gross = 0.0;
        for (const auto& fill : deterministic_portfolio.fills) {
            if (fill.rebalance_id != 34) continue;
            fused_gross = std::fma(static_cast<double>(fill.quantity), fill.price, fused_gross);
            volatile double fill_value = static_cast<double>(fill.quantity) * fill.price;
            separately_rounded_gross += fill_value;
        }
        check(fused_gross != separately_rounded_gross, "rebalance fixture exposes contraction difference");
        check(double_bits(fused_gross) == UINT64_C(0x40cdc0a31ec2a353), "unexpected fused rebalance gross bits");
        check(double_bits(rebalance->gross_trade_value) == double_bits(fused_gross),
            "rebalance gross is not deterministic fused result");

        auto action_config = config({"ACT"}, "ACT");
        action_config.data_dir = "tests/fixtures/actions";
        action_config.transaction_cost_rate = 0.0;
        action_config.slippage_rate = 0.0;
        action_config.allocation.max_weight = 1.0;
        action_config.allocation.cash_buffer = 0.0;
        action_config.allocation.min_trade_value = 0.0;
        const auto action_portfolio = PortfolioBacktester(action_config).run();
        const auto action_result = quant::analytics::PortfolioAttributionAnalyzer::analyze(action_portfolio, action_config, "actions", 1e-8);
        check(action_result.corporate_actions.size() == 3, "corporate action records");
        check(std::abs(action_result.corporate_actions[0].economic_contribution) < 1e-8, "split neutrality");
        check(action_result.corporate_actions[1].cash_effect > 0.0, "dividend contribution");
        check(std::abs(action_result.total_residual) < 1e-4, "action reconciliation");

        const auto drawdown = quant::analytics::PortfolioAttributionAnalyzer::analyze(
            simple_result({100.0, 80.0, 110.0}), config({"ONE"}, "ONE"), "drawdown", 1e-10);
        check(!drawdown.drawdowns.empty() && std::abs(drawdown.drawdowns.front().depth + 0.2) < 1e-10, "drawdown reconciliation");
        check(drawdown.drawdowns.front().recovery_date == "2024-01-03", "recovery detection");

        const std::string output = "test_results/attribution_export";
        std::filesystem::remove_all(output);
        quant::io::CsvResultExporter::write_attribution(mixed, output);
        std::ifstream csv(output + "/daily_asset_attribution.csv");
        std::string header;
        std::getline(csv, header);
        check(header.find("net_contribution") != std::string::npos, "exporter schema");

        auto corrupted = simple_result({100.0, 105.0, 110.0});
        corrupted.equity_curve.back().portfolio_value += 1.0;
        bool rejected = false;
        try { (void)quant::analytics::PortfolioAttributionAnalyzer::analyze(corrupted, config({"ONE"}, "ONE"), "bad", 1e-10); }
        catch (const quant::MethodologyError&) { rejected = true; }
        check(rejected, "residual tolerance rejection");
        std::cout << cases << " attribution cases passed with " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
