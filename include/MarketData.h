#pragma once

#include "Event.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Bar {
    std::string date;
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    long long volume{0};
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
