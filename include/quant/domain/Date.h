#pragma once

#include <string>

namespace quant {

class Date {
public:
    static Date parse(const std::string& value);

    Date add_days(int days) const;
    Date add_months(int months) const;
    Date add_years(int years) const;
    std::string to_string() const;
    int days_until(const Date& other) const;
    int iso_weekday() const;

    int year() const { return year_; }
    int month() const { return month_; }
    int day() const { return day_; }

    friend bool operator==(const Date& lhs, const Date& rhs);
    friend bool operator<(const Date& lhs, const Date& rhs);
    friend bool operator!=(const Date& lhs, const Date& rhs) { return !(lhs == rhs); }
    friend bool operator>(const Date& lhs, const Date& rhs) { return rhs < lhs; }
    friend bool operator<=(const Date& lhs, const Date& rhs) { return !(rhs < lhs); }
    friend bool operator>=(const Date& lhs, const Date& rhs) { return !(lhs < rhs); }

private:
    Date(int year, int month, int day) : year_(year), month_(month), day_(day) {}

    int year_;
    int month_;
    int day_;
};

}  // namespace quant
