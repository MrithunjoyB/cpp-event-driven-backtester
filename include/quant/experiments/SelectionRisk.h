#pragma once

#include "quant/analytics/StatisticalAnalysis.h"
#include "quant/config/ExperimentConfig.h"

#include <cstddef>
#include <string>
#include <vector>

namespace quant::experiments {

struct CandidateDefinition {
    std::string experiment_id;
    std::string family;
    std::string ticker;
    std::string parameters;
    std::string candidate_id;
    std::string identity_context;
};

struct CandidateObservation {
    int window_id{0};
    std::string date;
    std::string candidate_id;
    std::string family;
    std::string ticker;
    double diagnostic_value{1.0};
    double candidate_return{0.0};
    double benchmark_return{0.0};
    double active_return{0.0};
    bool selected{false};
};

struct CandidateWindowResult {
    int window_id{0};
    CandidateDefinition candidate;
    std::string train_start;
    std::string train_end;
    std::string test_start;
    std::string test_end;
    std::size_t train_observations{0};
    std::size_t test_observations{0};
    bool eligible{false};
    std::string rejection_reason;
    double training_objective{0.0};
    double training_return{0.0};
    double training_sharpe{0.0};
    int training_rank{0};
    int oos_rank{0};
    bool selected{false};
    double oos_return{0.0};
    double benchmark_return{0.0};
    double active_return{0.0};
    double sharpe{0.0};
    double sortino{0.0};
    double volatility{0.0};
    double max_drawdown{0.0};
    int trade_count{0};
    double turnover{0.0};
    double transaction_costs{0.0};
    double liquidation_costs{0.0};
};

struct AlignedCandidatePanel {
    std::vector<std::string> dates;
    std::vector<std::string> candidate_ids;
    std::vector<std::vector<double>> active_returns;
    std::vector<std::size_t> removed_dates;
};

class SelectionRiskAnalyzer {
public:
    static std::string canonical_candidate_id(const CandidateDefinition& candidate);
    static AlignedCandidatePanel align_common_dates(
        const std::vector<CandidateDefinition>& candidates,
        const std::vector<CandidateObservation>& observations,
        std::size_t minimum_observations);
    static analytics::MultipleTestingResult reality_check(
        const AlignedCandidatePanel& panel,
        const analytics::StatisticalConfig& config);
    static void run_experiment(const config::ExperimentConfig& experiment);
};

}  // namespace quant::experiments
