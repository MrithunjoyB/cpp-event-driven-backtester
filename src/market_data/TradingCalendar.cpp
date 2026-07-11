#include "quant/market_data/TradingCalendar.h"

#include "quant/domain/Date.h"
#include "quant/domain/Errors.h"

#include <algorithm>
#include <iterator>
#include <set>

namespace quant::market_data {
namespace {
using BarIndex = std::map<std::string, std::map<std::string, std::size_t>>;

BarIndex index_history(const std::map<std::string, std::vector<Bar>>& history) {
    BarIndex index;
    for (const auto& asset : history) {
        for (std::size_t i = 0; i < asset.second.size(); ++i) index[asset.first][asset.second[i].date] = i;
    }
    return index;
}

std::vector<std::string> timeline(const std::map<std::string, std::vector<Bar>>& history, CalendarMode mode) {
    std::set<std::string> dates;
    bool first = true;
    for (const auto& asset : history) {
        std::set<std::string> current;
        for (const auto& bar : asset.second) current.insert(bar.date);
        if (mode == CalendarMode::Union) {
            dates.insert(current.begin(), current.end());
        } else if (first) {
            dates = std::move(current);
        } else {
            std::set<std::string> intersection;
            std::set_intersection(dates.begin(), dates.end(), current.begin(), current.end(),
                                  std::inserter(intersection, intersection.begin()));
            dates = std::move(intersection);
        }
        first = false;
    }
    return {dates.begin(), dates.end()};
}
}

std::vector<TradingSession> TradingCalendar::build(
    const std::map<std::string, std::vector<Bar>>& history,
    const CalendarPolicy& policy) {
    if (history.empty()) throw DataError("Cannot construct a calendar for an empty asset universe");
    if (policy.max_stale_calendar_days < 0) throw ConfigurationError("max_stale_calendar_days cannot be negative");
    const auto index = index_history(history);
    const auto dates = timeline(history, policy.mode);
    std::map<std::string, std::pair<std::string, double>> last_marks;
    std::vector<TradingSession> sessions;
    sessions.reserve(dates.size());
    for (const auto& date : dates) {
        TradingSession session;
        session.date = date;
        for (const auto& asset : history) {
            AssetSession state;
            state.ticker = asset.first;
            state.date = date;
            const auto found = index.at(asset.first).find(date);
            if (found != index.at(asset.first).end()) {
                state.bar_index = found->second;
                state.bar = &asset.second[found->second];
                state.tradability = {true, true, false, false, true};
                state.mark = {true, true, state.bar->close, 0, "current_close", date};
                last_marks[asset.first] = {date, state.bar->close};
            } else {
                state.tradability = {false, false, true, false, false};
                const auto previous = last_marks.find(asset.first);
                if (policy.stale_mark_policy == StaleMarkPolicy::LastKnown && previous != last_marks.end()) {
                    const int age = quant::Date::parse(previous->second.first).days_until(quant::Date::parse(date));
                    if (age <= policy.max_stale_calendar_days) {
                        state.mark = {true, false, previous->second.second, age, "last_known_close", previous->second.first};
                    }
                }
            }
            session.assets.emplace(asset.first, state);
        }
        sessions.push_back(std::move(session));
    }
    return sessions;
}

std::vector<RebalanceSchedulePoint> TradingCalendar::weekly_first_on_or_after_monday(
    const std::vector<TradingSession>& sessions) {
    std::vector<RebalanceSchedulePoint> output;
    std::string week_start;
    for (std::size_t i = 1; i < sessions.size(); ++i) {
        const auto date = quant::Date::parse(sessions[i].date);
        const std::string monday = date.add_days(0).to_string();
        const std::string key = date.add_days(0).to_string().substr(0, 4) + ":" +
            date.add_days(0).add_days(7 - date.iso_weekday()).to_string();
        if (key != week_start) {
            output.push_back({monday, i});
            week_start = key;
        }
    }
    return output;
}

std::vector<RebalanceSchedulePoint> TradingCalendar::monthly_first_valuation(
    const std::vector<TradingSession>& sessions) {
    std::vector<RebalanceSchedulePoint> output;
    std::string month;
    for (std::size_t i = 1; i < sessions.size(); ++i) {
        const std::string key = sessions[i].date.substr(0, 7);
        if (key != month) {
            output.push_back({sessions[i].date.substr(0, 7) + "-01", i});
            month = key;
        }
    }
    return output;
}

std::string to_string(CalendarMode value) { return value == CalendarMode::Union ? "union" : "intersection_legacy"; }
std::string to_string(StaleMarkPolicy value) { return value == StaleMarkPolicy::LastKnown ? "last_known" : "unavailable"; }
std::string to_string(MissingBarPolicy value) {
    if (value == MissingBarPolicy::Error) return "error";
    return value == MissingBarPolicy::MarkUnavailable ? "mark_unavailable" : "use_last_known";
}
std::string to_string(ClosedAssetPolicy value) {
    if (value == ClosedAssetPolicy::Defer) return "defer";
    return value == ClosedAssetPolicy::SkipAsset ? "skip_asset" : "partial_rebalance";
}

}  // namespace quant::market_data
