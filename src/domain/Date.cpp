#include "quant/domain/Date.h"

#include "quant/domain/Errors.h"

#include <algorithm>
#include <cstdio>
#include <tuple>
#include <ctime>

namespace quant {
namespace {
bool is_leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    return month == 2 && is_leap_year(year) ? 29 : days[month - 1];
}
}

Date Date::parse(const std::string& value) {
    int year = 0;
    int month = 0;
    int day = 0;
    char tail = '\0';
    if (std::sscanf(value.c_str(), "%d-%d-%d%c", &year, &month, &day, &tail) != 3 ||
        value.size() != 10 || year < 1 || month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month)) {
        throw DataError("Invalid ISO date: " + value);
    }
    return Date(year, month, day);
}

Date Date::add_days(int days) const {
    if (days < 0) {
        throw MethodologyError("Negative calendar-day offsets are not supported");
    }
    Date output = *this;
    for (int i = 0; i < days; ++i) {
        ++output.day_;
        if (output.day_ > days_in_month(output.year_, output.month_)) {
            output.day_ = 1;
            ++output.month_;
            if (output.month_ > 12) {
                output.month_ = 1;
                ++output.year_;
            }
        }
    }
    return output;
}

Date Date::add_months(int months) const {
    const int total_months = year_ * 12 + month_ - 1 + months;
    if (total_months < 12) {
        throw MethodologyError("Calendar arithmetic produced an invalid year");
    }
    const int year = total_months / 12;
    const int month = total_months % 12 + 1;
    return Date(year, month, std::min(day_, days_in_month(year, month)));
}

Date Date::add_years(int years) const {
    const int year = year_ + years;
    if (year < 1) {
        throw MethodologyError("Calendar arithmetic produced an invalid year");
    }
    return Date(year, month_, std::min(day_, days_in_month(year, month_)));
}

std::string Date::to_string() const {
    char buffer[11];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year_, month_, day_);
    return buffer;
}

int Date::days_until(const Date& other) const {
    std::tm lhs{};
    lhs.tm_year = year_ - 1900;
    lhs.tm_mon = month_ - 1;
    lhs.tm_mday = day_;
    lhs.tm_hour = 12;
    std::tm rhs{};
    rhs.tm_year = other.year_ - 1900;
    rhs.tm_mon = other.month_ - 1;
    rhs.tm_mday = other.day_;
    rhs.tm_hour = 12;
    const std::time_t lhs_time = std::mktime(&lhs);
    const std::time_t rhs_time = std::mktime(&rhs);
    if (lhs_time == static_cast<std::time_t>(-1) || rhs_time == static_cast<std::time_t>(-1)) {
        throw MethodologyError("Calendar-day difference is outside the supported civil-time range");
    }
    return static_cast<int>(std::difftime(rhs_time, lhs_time) / (60.0 * 60.0 * 24.0));
}

int Date::iso_weekday() const {
    std::tm value{};
    value.tm_year = year_ - 1900;
    value.tm_mon = month_ - 1;
    value.tm_mday = day_;
    value.tm_hour = 12;
    if (std::mktime(&value) == static_cast<std::time_t>(-1)) {
        throw MethodologyError("Weekday calculation is outside the supported civil-time range");
    }
    return value.tm_wday == 0 ? 7 : value.tm_wday;
}

bool operator==(const Date& lhs, const Date& rhs) {
    return std::tie(lhs.year_, lhs.month_, lhs.day_) == std::tie(rhs.year_, rhs.month_, rhs.day_);
}

bool operator<(const Date& lhs, const Date& rhs) {
    return std::tie(lhs.year_, lhs.month_, lhs.day_) < std::tie(rhs.year_, rhs.month_, rhs.day_);
}

}  // namespace quant
