#include "MarketData.h"
#include "PortfolioBacktester.h"

#include <cmath>
#include <iostream>

namespace {
PortfolioBacktestConfig config(quant::market_data::AdjustmentPolicy policy) {
    PortfolioBacktestConfig value;
    value.tickers = {"ACT"};
    value.data_dir = "tests/fixtures/actions";
    value.calendar.mode = quant::market_data::CalendarMode::Union;
    value.calendar.max_stale_calendar_days = 3;
    value.allocation.max_weight = 1.0;
    value.allocation.cash_buffer = 0.0;
    value.allocation.min_trade_value = 0.0;
    value.transaction_cost_rate = 0.0;
    value.slippage_rate = 0.0;
    value.adjustment_policy = policy;
    value.result_schema_version = 3;
    return value;
}
}

int main() {
    MarketData invalid;
    if (invalid.load_csv("BAD", "tests/fixtures/actions/BAD.csv")) return 1;
    const auto raw = PortfolioBacktester(config(quant::market_data::AdjustmentPolicy::RawPrice)).run();
    if (raw.corporate_actions.size() != 3) return 1;
    const auto& split = raw.corporate_actions[0];
    const auto& dividend = raw.corporate_actions[1];
    const auto& reverse = raw.corporate_actions[2];
    if (split.action_type != "stock_split" || std::abs(split.quantity_after - split.quantity_before * 2.0) > 1e-9) return 1;
    if (dividend.action_type != "cash_dividend" || dividend.cash_effect <= 0.0) return 1;
    if (std::abs(reverse.quantity_after - reverse.quantity_before * 0.5) > 1e-9) return 1;
    const auto adjusted = PortfolioBacktester(config(quant::market_data::AdjustmentPolicy::TotalReturnAdjusted)).run();
    if (adjusted.corporate_actions[1].cash_effect != 0.0) return 1;
    if (adjusted.corporate_actions[0].quantity_before != adjusted.corporate_actions[0].quantity_after) return 1;
    std::cout << "corporate_action_tests passed\n";
    return 0;
}
