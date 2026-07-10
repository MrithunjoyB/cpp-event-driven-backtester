#pragma once

#include "MarketData.h"

#include <map>
#include <string>
#include <vector>

enum class AllocationPolicyType {
    EqualWeight,
    InverseVolatility,
    MomentumTopN
};

struct AllocationPolicyConfig {
    AllocationPolicyType type{AllocationPolicyType::EqualWeight};
    double max_weight{0.40};
    double cash_buffer{0.02};
    double min_trade_value{25.0};
    int volatility_lookback{60};
    int momentum_lookback{126};
    int top_n{3};
};

class AllocationPolicy {
public:
    explicit AllocationPolicy(AllocationPolicyConfig config);

    const AllocationPolicyConfig& config() const;
    std::string name() const;
    std::map<std::string, double> target_weights(
        const std::vector<std::string>& tickers,
        const std::map<std::string, std::vector<Bar>>& history,
        const std::map<std::string, std::size_t>& decision_indices) const;

    static AllocationPolicyType parse_type(const std::string& value);
    static std::string type_name(AllocationPolicyType type);
    static std::map<std::string, double> enforce_constraints(
        const std::map<std::string, double>& raw_weights,
        double max_weight,
        double cash_buffer);

private:
    AllocationPolicyConfig config_;

    std::map<std::string, double> equal_weight(const std::vector<std::string>& tickers) const;
    std::map<std::string, double> inverse_volatility(
        const std::vector<std::string>& tickers,
        const std::map<std::string, std::vector<Bar>>& history,
        const std::map<std::string, std::size_t>& decision_indices) const;
    std::map<std::string, double> momentum_top_n(
        const std::vector<std::string>& tickers,
        const std::map<std::string, std::vector<Bar>>& history,
        const std::map<std::string, std::size_t>& decision_indices) const;
};
