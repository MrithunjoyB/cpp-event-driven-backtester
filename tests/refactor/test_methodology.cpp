#include "ResearchMethodology.h"

#include <iostream>
#include <set>

namespace {
std::vector<Bar> bars(std::size_t count, bool seven_day) {
    std::vector<Bar> output;
    std::string date = "2020-01-06";
    double close = 100.0;
    for (std::size_t i = 0; i < count; ++i) {
        close *= 1.0 + (static_cast<int>(i % 9) - 3) * 0.0005;
        output.push_back({date, close, close * 1.01, close * 0.99, close, 1000});
        date = add_calendar_days(date, !seven_day && i % 5 == 4 ? 3 : 1);
    }
    return output;
}
}

int main() {
    const auto equity = build_calendar_windows(bars(1200, false), 3, 6, 6);
    const auto bitcoin = build_calendar_windows(bars(1600, true), 3, 6, 6);
    if (equity.empty() || bitcoin.empty()) return 1;
    if (equity.front().train_end >= equity.front().test_start) return 1;
    auto prefix = bars(340, true);
    auto extended = prefix;
    std::string date = add_calendar_days(extended.back().date, 1);
    for (int i = 0; i < 30; ++i) {
        double close = extended.back().close * (i % 2 == 0 ? 1.3 : 0.75);
        extended.push_back({date, close, close * 1.01, close * 0.99, close, 1000});
        date = add_calendar_days(date, 1);
    }
    const auto before = classify_causal_regimes(prefix, 20, 40);
    const auto after = classify_causal_regimes(extended, 20, 40);
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i].regime != after[i].regime || before[i].volatility_threshold != after[i].volatility_threshold) return 1;
    }
    const auto* execution = regime_for_execution(before, prefix[300].date);
    const auto* interval = regime_for_return_interval(before, prefix[299].date, prefix[300].date);
    if (execution == nullptr || interval == nullptr || execution->date != prefix[299].date || interval->date != prefix[299].date) return 1;
    std::cout << "methodology_tests passed\n";
    return 0;
}
