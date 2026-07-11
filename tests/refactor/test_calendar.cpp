#include "quant/market_data/TradingCalendar.h"

#include <iostream>
#include <map>

namespace {
Bar bar(const std::string& date, double close) { return {date, close, close, close, close, 100}; }
}

int main() {
    std::map<std::string, std::vector<Bar>> history{
        {"EQ", {bar("2024-01-05", 100.0), bar("2024-01-08", 101.0)}},
        {"BTC", {bar("2024-01-05", 200.0), bar("2024-01-06", 180.0), bar("2024-01-07", 170.0), bar("2024-01-08", 210.0)}}};
    quant::market_data::CalendarPolicy policy;
    policy.mode = quant::market_data::CalendarMode::Union;
    policy.max_stale_calendar_days = 2;
    const auto union_sessions = quant::market_data::TradingCalendar::build(history, policy);
    if (union_sessions.size() != 4) return 1;
    const auto& saturday_equity = union_sessions[1].assets.at("EQ");
    if (saturday_equity.tradability.tradable || !saturday_equity.mark.available || saturday_equity.mark.stale_age_days != 1) return 1;
    if (!union_sessions[1].assets.at("BTC").tradability.execution_allowed) return 1;
    policy.mode = quant::market_data::CalendarMode::LegacyIntersection;
    const auto intersection = quant::market_data::TradingCalendar::build(history, policy);
    if (intersection.size() != 2 || intersection.front().date != "2024-01-05") return 1;
    policy.mode = quant::market_data::CalendarMode::Union;
    policy.max_stale_calendar_days = 0;
    const auto expired = quant::market_data::TradingCalendar::build(history, policy);
    if (expired[1].assets.at("EQ").mark.available) return 1;
    const auto weekly = quant::market_data::TradingCalendar::weekly_first_on_or_after_monday(union_sessions);
    const auto monthly = quant::market_data::TradingCalendar::monthly_first_valuation(union_sessions);
    if (weekly.empty() || monthly.size() != 1 || monthly.front().valuation_index != 1) return 1;
    std::cout << "calendar_tests passed\n";
    return 0;
}
