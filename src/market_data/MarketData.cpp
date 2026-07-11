#include "MarketData.h"
#include "quant/domain/Date.h"

#include <fstream>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

bool MarketData::load_csv(const std::string& ticker, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::vector<Bar> rows;
    std::unordered_set<std::string> seen_dates;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        if (line_number == 1) {
            if (!validate_header(line)) {
                return false;
            }
            continue;
        }

        Bar bar;
        std::string error;
        if (!parse_row(line, bar, error)) {
            return false;
        }
        if (!validate_bar(bar, error)) {
            return false;
        }
        if (!rows.empty() && quant::Date::parse(bar.date) <= quant::Date::parse(rows.back().date)) {
            return false;
        }
        if (seen_dates.count(bar.date) > 0) {
            return false;
        }
        seen_dates.insert(bar.date);
        rows.push_back(bar);
    }

    data_[ticker] = rows;
    return !rows.empty();
}

bool MarketData::has_ticker(const std::string& ticker) const {
    return data_.find(ticker) != data_.end();
}

const std::vector<Bar>& MarketData::bars(const std::string& ticker) const {
    auto it = data_.find(ticker);
    if (it == data_.end()) {
        throw std::runtime_error("Ticker data not loaded: " + ticker);
    }
    return it->second;
}

std::vector<double> MarketData::closes(const std::string& ticker) const {
    std::vector<double> output;
    const auto& series = bars(ticker);
    output.reserve(series.size());
    for (const auto& bar : series) {
        output.push_back(bar.close);
    }
    return output;
}

MarketEvent MarketData::market_event(const std::string& ticker, std::size_t index) const {
    const auto& bar = bars(ticker).at(index);
    return MarketEvent{EventType::Market, bar.date, ticker, bar.open, bar.high, bar.low, bar.close, bar.volume, index};
}

bool MarketData::parse_row(const std::string& line, Bar& bar, std::string& error) {
    std::stringstream ss(line);
    std::string item;
    std::vector<std::string> cols;
    while (std::getline(ss, item, ',')) {
        cols.push_back(item);
    }
    if (cols.size() != 6) {
        error = "Expected exactly 6 columns";
        return false;
    }
    for (const auto& col : cols) {
        if (col.empty()) {
            error = "Missing value";
            return false;
        }
    }

    try {
        bar.date = cols[0];
        bar.open = std::stod(cols[1]);
        bar.high = std::stod(cols[2]);
        bar.low = std::stod(cols[3]);
        bar.close = std::stod(cols[4]);
        bar.volume = static_cast<long long>(std::stod(cols[5]));
    } catch (...) {
        error = "Could not parse numeric value";
        return false;
    }

    return true;
}

bool MarketData::validate_header(const std::string& line) {
    return line == "Date,Open,High,Low,Close,Volume";
}

bool MarketData::validate_bar(const Bar& bar, std::string& error) {
    if (bar.date.empty()) {
        error = "Missing date";
        return false;
    }
    try {
        (void)quant::Date::parse(bar.date);
    } catch (const std::exception&) {
        error = "Date must be a valid YYYY-MM-DD value";
        return false;
    }
    if (!std::isfinite(bar.open) || !std::isfinite(bar.high) || !std::isfinite(bar.low) || !std::isfinite(bar.close)) {
        error = "Non-finite price";
        return false;
    }
    if (bar.open <= 0.0 || bar.high <= 0.0 || bar.low <= 0.0 || bar.close <= 0.0) {
        error = "Prices must be positive";
        return false;
    }
    if (bar.volume < 0) {
        error = "Volume cannot be negative";
        return false;
    }
    if (bar.high < bar.low) {
        error = "High below low";
        return false;
    }
    if (bar.open < bar.low || bar.open > bar.high || bar.close < bar.low || bar.close > bar.high) {
        error = "Open/close outside high-low range";
        return false;
    }
    return true;
}
