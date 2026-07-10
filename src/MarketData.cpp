#include "MarketData.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

bool MarketData::load_csv(const std::string& ticker, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::vector<Bar> rows;
    std::string line;
    bool first_line = true;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        if (first_line) {
            first_line = false;
            if (line.find("Date") != std::string::npos) {
                continue;
            }
        }

        Bar bar;
        if (parse_row(line, bar)) {
            rows.push_back(bar);
        }
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

bool MarketData::parse_row(const std::string& line, Bar& bar) {
    std::stringstream ss(line);
    std::string item;
    std::vector<std::string> cols;
    while (std::getline(ss, item, ',')) {
        cols.push_back(item);
    }
    if (cols.size() < 6) {
        return false;
    }

    try {
        bar.date = cols[0];
        bar.open = std::stod(cols[1]);
        bar.high = std::stod(cols[2]);
        bar.low = std::stod(cols[3]);
        bar.close = std::stod(cols[4]);
        bar.volume = static_cast<long long>(std::stod(cols[5]));
    } catch (...) {
        return false;
    }

    return !bar.date.empty() && bar.open > 0.0 && bar.high > 0.0 && bar.low > 0.0 && bar.close > 0.0 && bar.volume >= 0;
}

