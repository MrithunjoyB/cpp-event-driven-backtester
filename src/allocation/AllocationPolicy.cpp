#include "AllocationPolicy.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace {
double sample_stdev(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double avg = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double sum = 0.0;
    for (double value : values) {
        double diff = value - avg;
        sum += diff * diff;
    }
    return std::sqrt(sum / (values.size() - 1));
}

double trailing_volatility(const std::vector<Bar>& bars, std::size_t decision_index, int lookback) {
    if (lookback <= 1 || decision_index < static_cast<std::size_t>(lookback)) {
        return 0.0;
    }
    const std::size_t window = static_cast<std::size_t>(lookback);
    std::vector<double> returns;
    for (std::size_t i = decision_index - window + 1; i <= decision_index; ++i) {
        if (i == 0 || bars[i - 1].close <= 0.0) {
            continue;
        }
        returns.push_back((bars[i].close / bars[i - 1].close) - 1.0);
    }
    return sample_stdev(returns) * std::sqrt(252.0);
}

double trailing_return(const std::vector<Bar>& bars, std::size_t decision_index, int lookback) {
    if (lookback <= 0 || decision_index < static_cast<std::size_t>(lookback)) {
        return -1e9;
    }
    const std::size_t window = static_cast<std::size_t>(lookback);
    double start = bars[decision_index - window].close;
    return start > 0.0 ? (bars[decision_index].close / start) - 1.0 : -1e9;
}
}

AllocationPolicy::AllocationPolicy(AllocationPolicyConfig config) : config_(config) {}

const AllocationPolicyConfig& AllocationPolicy::config() const {
    return config_;
}

std::string AllocationPolicy::name() const {
    return type_name(config_.type);
}

AllocationPolicyType AllocationPolicy::parse_type(const std::string& value) {
    if (value == "equal_weight" || value == "EqualWeight") {
        return AllocationPolicyType::EqualWeight;
    }
    if (value == "inverse_volatility" || value == "InverseVolatility") {
        return AllocationPolicyType::InverseVolatility;
    }
    if (value == "momentum_top_n" || value == "MomentumTopN") {
        return AllocationPolicyType::MomentumTopN;
    }
    throw std::runtime_error("Unknown allocation policy: " + value);
}

std::string AllocationPolicy::type_name(AllocationPolicyType type) {
    switch (type) {
        case AllocationPolicyType::EqualWeight:
            return "equal_weight";
        case AllocationPolicyType::InverseVolatility:
            return "inverse_volatility";
        case AllocationPolicyType::MomentumTopN:
            return "momentum_top_n";
    }
    return "unknown";
}

std::map<std::string, double> AllocationPolicy::target_weights(
    const std::vector<std::string>& tickers,
    const std::map<std::string, std::vector<Bar>>& history,
    const std::map<std::string, std::size_t>& decision_indices) const {
    std::map<std::string, double> raw;
    if (config_.type == AllocationPolicyType::EqualWeight) {
        raw = equal_weight(tickers);
    } else if (config_.type == AllocationPolicyType::InverseVolatility) {
        raw = inverse_volatility(tickers, history, decision_indices);
    } else {
        raw = momentum_top_n(tickers, history, decision_indices);
    }
    return enforce_constraints(raw, config_.max_weight, config_.cash_buffer);
}

std::map<std::string, double> AllocationPolicy::enforce_constraints(
    const std::map<std::string, double>& raw_weights,
    double max_weight,
    double cash_buffer) {
    std::map<std::string, double> weights;
    double budget = std::max(0.0, std::min(1.0, 1.0 - cash_buffer));
    double sum = 0.0;
    for (const auto& kv : raw_weights) {
        double w = std::max(0.0, kv.second);
        weights[kv.first] = w;
        sum += w;
    }
    if (sum <= 0.0 || budget <= 0.0) {
        for (auto& kv : weights) {
            kv.second = 0.0;
        }
        return weights;
    }
    for (auto& kv : weights) {
        kv.second = std::min(max_weight, kv.second / sum * budget);
    }

    // Redistribute leftover weight to uncapped assets without violating max weight.
    for (int pass = 0; pass < 8; ++pass) {
        double used = 0.0;
        std::vector<std::string> room;
        for (const auto& kv : weights) {
            used += kv.second;
            if (kv.second + 1e-12 < max_weight) {
                room.push_back(kv.first);
            }
        }
        double leftover = budget - used;
        if (leftover <= 1e-10 || room.empty()) {
            break;
        }
        double add = leftover / room.size();
        for (const auto& ticker : room) {
            weights[ticker] = std::min(max_weight, weights[ticker] + add);
        }
    }
    return weights;
}

std::map<std::string, double> AllocationPolicy::equal_weight(const std::vector<std::string>& tickers) const {
    std::map<std::string, double> raw;
    for (const auto& ticker : tickers) {
        raw[ticker] = 1.0;
    }
    return raw;
}

std::map<std::string, double> AllocationPolicy::inverse_volatility(
    const std::vector<std::string>& tickers,
    const std::map<std::string, std::vector<Bar>>& history,
    const std::map<std::string, std::size_t>& decision_indices) const {
    std::map<std::string, double> raw;
    for (const auto& ticker : tickers) {
        auto h = history.find(ticker);
        auto idx = decision_indices.find(ticker);
        if (h == history.end() || idx == decision_indices.end()) {
            raw[ticker] = 0.0;
            continue;
        }
        double vol = trailing_volatility(h->second, idx->second, config_.volatility_lookback);
        raw[ticker] = vol > 0.0 ? 1.0 / vol : 0.0;
    }
    double sum = 0.0;
    for (const auto& kv : raw) {
        sum += kv.second;
    }
    if (sum <= 0.0) {
        return equal_weight(tickers);
    }
    return raw;
}

std::map<std::string, double> AllocationPolicy::momentum_top_n(
    const std::vector<std::string>& tickers,
    const std::map<std::string, std::vector<Bar>>& history,
    const std::map<std::string, std::size_t>& decision_indices) const {
    std::vector<std::pair<std::string, double>> ranked;
    for (const auto& ticker : tickers) {
        auto h = history.find(ticker);
        auto idx = decision_indices.find(ticker);
        if (h == history.end() || idx == decision_indices.end()) {
            continue;
        }
        ranked.push_back({ticker, trailing_return(h->second, idx->second, config_.momentum_lookback)});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second == rhs.second) {
            return lhs.first < rhs.first;
        }
        return lhs.second > rhs.second;
    });
    std::map<std::string, double> raw;
    for (const auto& ticker : tickers) {
        raw[ticker] = 0.0;
    }
    const std::size_t requested = config_.top_n > 0 ? static_cast<std::size_t>(config_.top_n) : 0;
    const std::size_t selected = std::min(requested, ranked.size());
    for (std::size_t i = 0; i < selected; ++i) {
        if (ranked[i].second > -1e8) {
            raw[ranked[i].first] = 1.0;
        }
    }
    return raw;
}
