#include "ResearchMethodology.h"

#include "Strategy.h"
#include "quant/domain/Date.h"
#include "quant/domain/Errors.h"

#include <algorithm>
#include <cmath>

namespace {
double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 0) {
        return (values[middle - 1] + values[middle]) / 2.0;
    }
    return values[middle];
}

std::size_t first_at_or_after(const std::vector<Bar>& bars, const std::string& date) {
    const quant::Date target = quant::Date::parse(date);
    return static_cast<std::size_t>(std::lower_bound(
        bars.begin(), bars.end(), target,
        [](const Bar& bar, const quant::Date& value) { return quant::Date::parse(bar.date) < value; }) - bars.begin());
}
}

std::vector<RegimePoint> classify_causal_regimes(
    const std::vector<Bar>& bars,
    int volatility_window,
    std::size_t minimum_threshold_observations) {
    if (volatility_window <= 1) {
        throw quant::MethodologyError("Volatility window must be greater than one");
    }
    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& bar : bars) {
        closes.push_back(bar.close);
    }
    const std::vector<double> returns = daily_returns(closes);
    std::vector<double> historical_volatility;
    std::vector<RegimePoint> output;
    output.reserve(bars.size());

    for (std::size_t i = 0; i < bars.size(); ++i) {
        RegimePoint point;
        point.date = bars[i].date;
        point.information_cutoff = bars[i].date;
        const bool has_volatility = i >= static_cast<std::size_t>(volatility_window);
        if (has_volatility) {
            point.volatility_value = rolling_volatility(returns, i - 1, volatility_window) * std::sqrt(252.0);
        }
        const bool threshold_ready = historical_volatility.size() >= minimum_threshold_observations;
        const bool trend_ready = i >= 199 && i >= 60;
        if (has_volatility && threshold_ready && trend_ready) {
            point.volatility_threshold = median(historical_volatility);
            point.volatility_regime = point.volatility_value > point.volatility_threshold
                ? "high_volatility" : "low_volatility";
            const double sma200 = simple_moving_average(bars, i, 200);
            const double return60 = bars[i - 60].close > 0.0
                ? bars[i].close / bars[i - 60].close - 1.0 : 0.0;
            if (bars[i].close > sma200 && return60 > 0.0) {
                point.trend_regime = "bull";
            } else if (bars[i].close < sma200 && return60 < 0.0) {
                point.trend_regime = "bear";
            } else {
                point.trend_regime = "sideways";
            }
            point.regime = point.trend_regime + "/" + point.volatility_regime;
            point.available = true;
        }
        output.push_back(point);
        if (has_volatility) {
            historical_volatility.push_back(point.volatility_value);
        }
    }
    return output;
}

const RegimePoint* regime_for_execution(
    const std::vector<RegimePoint>& regimes,
    const std::string& execution_date) {
    const RegimePoint* selected = nullptr;
    const quant::Date execution = quant::Date::parse(execution_date);
    for (const auto& point : regimes) {
        if (quant::Date::parse(point.date) >= execution) {
            break;
        }
        if (point.available) {
            selected = &point;
        }
    }
    return selected;
}

const RegimePoint* regime_for_return_interval(
    const std::vector<RegimePoint>& regimes,
    const std::string& start_date,
    const std::string& end_date) {
    const quant::Date start = quant::Date::parse(start_date);
    const quant::Date end = quant::Date::parse(end_date);
    if (end <= start) {
        throw quant::MethodologyError("Return interval end must be after its start");
    }
    const RegimePoint* selected = nullptr;
    for (const auto& point : regimes) {
        if (quant::Date::parse(point.date) > start) {
            break;
        }
        if (point.available) {
            selected = &point;
        }
    }
    return selected;
}

std::string add_calendar_months(const std::string& value, int months) {
    return quant::Date::parse(value).add_months(months).to_string();
}

std::string add_calendar_years(const std::string& value, int years) {
    return quant::Date::parse(value).add_years(years).to_string();
}

std::string add_calendar_days(const std::string& value, int days) {
    return quant::Date::parse(value).add_days(days).to_string();
}

std::vector<CalendarWindow> build_calendar_windows(
    const std::vector<Bar>& bars,
    int training_years,
    int testing_months,
    int step_months) {
    if (bars.empty() || training_years <= 0 || testing_months <= 0 || step_months <= 0) {
        throw quant::MethodologyError("Calendar walk-forward durations must be positive");
    }
    std::vector<CalendarWindow> windows;
    quant::Date anchor = quant::Date::parse(bars.front().date);
    const quant::Date final_date = quant::Date::parse(bars.back().date);
    while (true) {
        const quant::Date train_boundary_date = anchor.add_years(training_years);
        const quant::Date test_boundary_date = train_boundary_date.add_months(testing_months);
        if (test_boundary_date > final_date) {
            break;
        }
        const std::string anchor_text = anchor.to_string();
        const std::string train_boundary = train_boundary_date.to_string();
        const std::string test_boundary = test_boundary_date.to_string();
        const std::size_t train_begin = first_at_or_after(bars, anchor_text);
        const std::size_t test_begin = first_at_or_after(bars, train_boundary);
        const std::size_t after_test = first_at_or_after(bars, test_boundary);
        if (train_begin < test_begin && test_begin < after_test) {
            windows.push_back(CalendarWindow{
                train_begin, test_begin - 1, test_begin, after_test - 1,
                bars[train_begin].date, bars[test_begin - 1].date,
                bars[test_begin].date, bars[after_test - 1].date,
                test_begin - train_begin, after_test - test_begin});
        }
        anchor = anchor.add_months(step_months);
    }
    return windows;
}
