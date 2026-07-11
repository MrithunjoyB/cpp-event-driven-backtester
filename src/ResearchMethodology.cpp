#include "ResearchMethodology.h"

#include "Strategy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
struct CivilDate {
    int year{0};
    int month{0};
    int day{0};
};

bool leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    return month == 2 && leap_year(year) ? 29 : days[month - 1];
}

CivilDate parse_date(const std::string& value) {
    CivilDate date;
    char tail = '\0';
    if (std::sscanf(value.c_str(), "%d-%d-%d%c", &date.year, &date.month, &date.day, &tail) != 3 ||
        date.year < 1 || date.month < 1 || date.month > 12 ||
        date.day < 1 || date.day > days_in_month(date.year, date.month)) {
        throw std::runtime_error("Invalid ISO date: " + value);
    }
    return date;
}

std::string format_date(const CivilDate& date) {
    char buffer[11];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", date.year, date.month, date.day);
    return buffer;
}

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
    return static_cast<std::size_t>(std::lower_bound(
        bars.begin(), bars.end(), date,
        [](const Bar& bar, const std::string& value) { return bar.date < value; }) - bars.begin());
}
}

std::vector<RegimePoint> classify_causal_regimes(
    const std::vector<Bar>& bars,
    int volatility_window,
    std::size_t minimum_threshold_observations) {
    if (volatility_window <= 1) {
        throw std::runtime_error("Volatility window must be greater than one");
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
    for (const auto& point : regimes) {
        if (point.date >= execution_date) {
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
    if (end_date <= start_date) {
        throw std::runtime_error("Return interval end must be after its start");
    }
    const RegimePoint* selected = nullptr;
    for (const auto& point : regimes) {
        if (point.date > start_date) {
            break;
        }
        if (point.available) {
            selected = &point;
        }
    }
    return selected;
}

std::string add_calendar_months(const std::string& value, int months) {
    CivilDate date = parse_date(value);
    const int total_months = date.year * 12 + (date.month - 1) + months;
    if (total_months < 12) {
        throw std::runtime_error("Calendar arithmetic produced an invalid year");
    }
    date.year = total_months / 12;
    date.month = total_months % 12 + 1;
    date.day = std::min(date.day, days_in_month(date.year, date.month));
    return format_date(date);
}

std::string add_calendar_years(const std::string& value, int years) {
    CivilDate date = parse_date(value);
    date.year += years;
    if (date.year < 1) {
        throw std::runtime_error("Calendar arithmetic produced an invalid year");
    }
    date.day = std::min(date.day, days_in_month(date.year, date.month));
    return format_date(date);
}

std::string add_calendar_days(const std::string& value, int days) {
    CivilDate date = parse_date(value);
    if (days < 0) {
        throw std::runtime_error("Negative calendar-day offsets are not supported");
    }
    for (int i = 0; i < days; ++i) {
        ++date.day;
        if (date.day > days_in_month(date.year, date.month)) {
            date.day = 1;
            ++date.month;
            if (date.month > 12) {
                date.month = 1;
                ++date.year;
            }
        }
    }
    return format_date(date);
}

std::vector<CalendarWindow> build_calendar_windows(
    const std::vector<Bar>& bars,
    int training_years,
    int testing_months,
    int step_months) {
    if (bars.empty() || training_years <= 0 || testing_months <= 0 || step_months <= 0) {
        throw std::runtime_error("Calendar walk-forward durations must be positive");
    }
    std::vector<CalendarWindow> windows;
    std::string anchor = bars.front().date;
    while (true) {
        const std::string train_boundary = add_calendar_years(anchor, training_years);
        const std::string test_boundary = add_calendar_months(train_boundary, testing_months);
        if (test_boundary > bars.back().date) {
            break;
        }
        const std::size_t train_begin = first_at_or_after(bars, anchor);
        const std::size_t test_begin = first_at_or_after(bars, train_boundary);
        const std::size_t after_test = first_at_or_after(bars, test_boundary);
        if (train_begin < test_begin && test_begin < after_test) {
            windows.push_back(CalendarWindow{
                train_begin, test_begin - 1, test_begin, after_test - 1,
                bars[train_begin].date, bars[test_begin - 1].date,
                bars[test_begin].date, bars[after_test - 1].date,
                test_begin - train_begin, after_test - test_begin});
        }
        anchor = add_calendar_months(anchor, step_months);
    }
    return windows;
}
