#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace quant::analytics {

enum class BootstrapMethod { Iid, MovingBlock };

struct DatedReturn { std::string date; double value{0.0}; };
struct StatisticalConfig {
    BootstrapMethod method{BootstrapMethod::MovingBlock};
    unsigned int seed{42};
    int simulations{1000};
    int block_length{0};
    double confidence_level{0.95};
    double annualization_factor{252.0};
    int minimum_observations{30};
};
struct BootstrapMetricRow {
    int simulation{0}; double cumulative_return{0.0}; double annualized_return{0.0}; double volatility{0.0};
    double sharpe{0.0}; double sortino{0.0}; double max_drawdown{0.0}; double calmar{0.0};
    double terminal_wealth{0.0}; double active_return{0.0}; double information_ratio{0.0};
};
struct ConfidenceInterval { double mean{0.0}; double median{0.0}; double standard_deviation{0.0}; double lower{0.0}; double upper{0.0}; };
struct StatisticalResult {
    int schema_version{3}; std::string experiment_id; std::string input_series; std::string start_date; std::string end_date;
    std::string rng_engine{"mt19937"}; std::string rng_mapping{"portable_bounded_v1"}; int stochastic_methodology_version{2};
    std::string sampling_frequency{"union_calendar"}; std::string benchmark; std::string method; std::string annualization_method;
    unsigned int seed{0}; int simulations{0}; int block_length{0}; double confidence_level{0.95}; int candidate_count{1};
    int observation_count{0}; std::string assumptions; std::vector<std::string> warnings;
    std::vector<DatedReturn> input_returns; std::vector<DatedReturn> benchmark_returns;
    std::vector<BootstrapMetricRow> metrics; std::vector<std::vector<double>> sampled_paths;
    ConfidenceInterval cumulative_return_ci; ConfidenceInterval sharpe_ci;
    double probability_loss{0.0}; double probability_positive_active{0.0}; double probability_benchmark_underperformance{0.0};
    double probability_sharpe_positive{0.0}; double probability_sharpe_exceeds_benchmark{0.0};
};
struct MultipleTestingResult {
    std::string method{"centered_moving_block_reality_check"}; int candidate_count{0}; int eligible_count{0};
    double observed_best_mean{0.0}; double p_value{1.0}; unsigned int seed{0}; int simulations{0}; int block_length{0};
    std::vector<double> bootstrap_max_statistics;
    std::string rng_engine{"mt19937"}; std::string rng_mapping{"portable_bounded_v1"}; int stochastic_methodology_version{2};
};

class StatisticalAnalyzer {
public:
    static int suggested_block_length(std::size_t observations);
    static StatisticalResult bootstrap(const std::string& experiment_id, const std::string& input_series,
        const std::vector<DatedReturn>& returns, const std::vector<DatedReturn>& benchmark,
        const std::string& benchmark_name, const StatisticalConfig& config, bool full_sample_diagnostic = false);
    static MultipleTestingResult reality_check(const std::vector<std::vector<double>>& candidate_active_returns,
        const StatisticalConfig& config);
};

std::string to_string(BootstrapMethod method);

}  // namespace quant::analytics
