#pragma once

#include "MarketData.h"

#include <cstddef>
#include <string>
#include <vector>

struct RegimePoint {
    std::string date;
    std::string trend_regime{"unavailable"};
    std::string volatility_regime{"unavailable"};
    std::string regime{"unavailable"};
    double volatility_value{0.0};
    double volatility_threshold{0.0};
    std::string information_cutoff;
    bool available{false};
};

struct CalendarWindow {
    std::size_t train_begin_index{0};
    std::size_t train_end_index{0};
    std::size_t test_begin_index{0};
    std::size_t test_end_index{0};
    std::string train_start;
    std::string train_end;
    std::string test_start;
    std::string test_end;
    std::size_t train_observations{0};
    std::size_t test_observations{0};
};

std::vector<RegimePoint> classify_causal_regimes(
    const std::vector<Bar>& bars,
    int volatility_window = 20,
    std::size_t minimum_threshold_observations = 60);

const RegimePoint* regime_for_execution(
    const std::vector<RegimePoint>& regimes,
    const std::string& execution_date);

const RegimePoint* regime_for_return_interval(
    const std::vector<RegimePoint>& regimes,
    const std::string& start_date,
    const std::string& end_date);

std::string add_calendar_months(const std::string& date, int months);
std::string add_calendar_years(const std::string& date, int years);
std::string add_calendar_days(const std::string& date, int days);

std::vector<CalendarWindow> build_calendar_windows(
    const std::vector<Bar>& bars,
    int training_years = 3,
    int testing_months = 6,
    int step_months = 6);
