#pragma once

#include "Event.h"

#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Bar {
    Bar() = default;
    Bar(std::string date_value, double open_value, double high_value, double low_value, double close_value,
        long long volume_value, double adjusted_close_value = 0.0, double dividends_value = 0.0,
        double stock_splits_value = 0.0, bool has_adjusted_close_value = false)
        : date(std::move(date_value)), open(open_value), high(high_value), low(low_value), close(close_value),
          volume(volume_value), adjusted_close(adjusted_close_value), dividends(dividends_value),
          stock_splits(stock_splits_value), has_adjusted_close(has_adjusted_close_value) {}
    std::string date;
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    long long volume{0};
    double adjusted_close{0.0};
    double dividends{0.0};
    double stock_splits{0.0};
    bool has_adjusted_close{false};
};

class MarketData {
public:
    bool load_csv(const std::string& ticker, const std::string& filepath);
    bool has_ticker(const std::string& ticker) const;
    const std::vector<Bar>& bars(const std::string& ticker) const;
    std::vector<double> closes(const std::string& ticker) const;
    MarketEvent market_event(const std::string& ticker, std::size_t index) const;

private:
    std::unordered_map<std::string, std::vector<Bar>> data_;
    static bool parse_row(const std::string& line, Bar& bar, std::string& error);
    static bool validate_header(const std::string& line);
    static bool validate_bar(const Bar& bar, std::string& error);
};
