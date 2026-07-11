#pragma once

#include "MarketData.h"

#include <map>
#include <string>
#include <vector>

namespace quant::market_data {

enum class CalendarMode { Union, LegacyIntersection };
enum class StaleMarkPolicy { LastKnown, Unavailable };
enum class MissingBarPolicy { Error, MarkUnavailable, UseLastKnown };
enum class ClosedAssetPolicy { Defer, SkipAsset, PartialRebalance };

struct CalendarPolicy {
    CalendarMode mode{CalendarMode::Union};
    StaleMarkPolicy stale_mark_policy{StaleMarkPolicy::LastKnown};
    MissingBarPolicy missing_bar_policy{MissingBarPolicy::Error};
    ClosedAssetPolicy closed_asset_policy{ClosedAssetPolicy::PartialRebalance};
    int max_stale_calendar_days{7};
};

struct TradabilityStatus {
    bool has_bar{false};
    bool tradable{false};
    bool market_closed{false};
    bool missing_unexpectedly{false};
    bool execution_allowed{false};
};

struct ValuationMark {
    bool available{false};
    bool current{false};
    double price{0.0};
    int stale_age_days{0};
    std::string source{"unavailable"};
    std::string source_date;
};

struct AssetSession {
    std::string ticker;
    std::string date;
    TradabilityStatus tradability;
    ValuationMark mark;
    const Bar* bar{nullptr};
    std::size_t bar_index{0};
};

struct TradingSession {
    std::string date;
    std::map<std::string, AssetSession> assets;
};

struct RebalanceSchedulePoint {
    std::string scheduled_date;
    std::size_t valuation_index{0};
};

class TradingCalendar {
public:
    static std::vector<TradingSession> build(
        const std::map<std::string, std::vector<Bar>>& history,
        const CalendarPolicy& policy);
    static std::vector<RebalanceSchedulePoint> weekly_first_on_or_after_monday(
        const std::vector<TradingSession>& sessions);
    static std::vector<RebalanceSchedulePoint> monthly_first_valuation(
        const std::vector<TradingSession>& sessions);
};

std::string to_string(CalendarMode value);
std::string to_string(StaleMarkPolicy value);
std::string to_string(MissingBarPolicy value);
std::string to_string(ClosedAssetPolicy value);

}  // namespace quant::market_data
