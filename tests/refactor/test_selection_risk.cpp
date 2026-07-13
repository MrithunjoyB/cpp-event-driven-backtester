#include "quant/experiments/SelectionRisk.h"
#include "quant/domain/Errors.h"
#include "Analysis.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <set>

int main() {
    using namespace quant::experiments;
    int cases = 0;
    auto check = [&](bool condition, const char* message) {
        ++cases;
        if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
    };

    CandidateDefinition a{"fixture", "MA_Cross", "SYN_EQ_A", "short=5;long=50", {}, "benchmark=SYN_BENCH|cost=15bps|window=3y_6m"};
    a.candidate_id = SelectionRiskAnalyzer::canonical_candidate_id(a);
    CandidateDefinition repeated = a;
    repeated.candidate_id.clear();
    repeated.candidate_id = SelectionRiskAnalyzer::canonical_candidate_id(repeated);
    check(a.candidate_id == repeated.candidate_id, "candidate IDs are stable");
    CandidateDefinition b{"fixture", "MA_Cross", "SYN_EQ_A", "short=10;long=50", {}, "benchmark=SYN_BENCH|cost=15bps|window=3y_6m"};
    b.candidate_id = SelectionRiskAnalyzer::canonical_candidate_id(b);
    check(a.candidate_id != b.candidate_id, "parameters produce unique candidate IDs");
    CandidateDefinition other_ticker = a; other_ticker.ticker = "SYN_EQ_B"; other_ticker.candidate_id.clear();
    other_ticker.candidate_id = SelectionRiskAnalyzer::canonical_candidate_id(other_ticker);
    check(a.candidate_id != other_ticker.candidate_id, "ticker is part of candidate identity");
    CandidateDefinition other_experiment = a; other_experiment.experiment_id = "other"; other_experiment.candidate_id.clear();
    other_experiment.candidate_id = SelectionRiskAnalyzer::canonical_candidate_id(other_experiment);
    check(a.candidate_id != other_experiment.candidate_id, "experiment is part of candidate identity");

    std::vector<CandidateObservation> observations;
    for (int day = 1; day <= 8; ++day) {
        const std::string date = "2024-01-0" + std::to_string(day);
        observations.push_back({0, date, a.candidate_id, a.family, a.ticker, 1.0, 0.01, 0.002, 0.008, true});
        if (day >= 2) observations.push_back({0, date, b.candidate_id, b.family, b.ticker, 1.0, 0.005, 0.002, 0.003, false});
    }
    const auto panel = SelectionRiskAnalyzer::align_common_dates({a, b}, observations, 5);
    check(panel.dates.size() == 7, "common-date intersection");
    check(panel.dates.front() == "2024-01-02", "missing date is removed");
    check(panel.candidate_ids.size() == 2, "candidate-count reconciliation");
    check(panel.active_returns.size() == 2 && panel.active_returns[0].size() == 7, "aligned panel shape");
    check(panel.removed_dates[0] == 1 && panel.removed_dates[1] == 0, "per-candidate removals recorded");
    check(std::abs(panel.active_returns[0][0] - 0.008) < 1e-12, "active return retained exactly");

    bool duplicate_rejected = false;
    auto duplicate = observations; duplicate.push_back(observations.front());
    try { (void)SelectionRiskAnalyzer::align_common_dates({a, b}, duplicate, 5); }
    catch (const quant::DataError&) { duplicate_rejected = true; }
    check(duplicate_rejected, "duplicate candidate/date rejected");
    bool nonfinite_rejected = false;
    auto nonfinite = observations; nonfinite.front().active_return = std::numeric_limits<double>::quiet_NaN();
    try { (void)SelectionRiskAnalyzer::align_common_dates({a, b}, nonfinite, 5); }
    catch (const quant::DataError&) { nonfinite_rejected = true; }
    check(nonfinite_rejected, "non-finite return rejected");
    bool minimum_rejected = false;
    try { (void)SelectionRiskAnalyzer::align_common_dates({a, b}, observations, 8); }
    catch (const quant::MethodologyError&) { minimum_rejected = true; }
    check(minimum_rejected, "minimum common sample enforced");
    bool empty_rejected = false;
    try { (void)SelectionRiskAnalyzer::align_common_dates({}, observations, 1); }
    catch (const quant::MethodologyError&) { empty_rejected = true; }
    check(empty_rejected, "empty panel rejected");

    quant::analytics::StatisticalConfig config;
    config.seed = 91; config.simulations = 200; config.block_length = 2; config.minimum_observations = 5;
    const auto first = SelectionRiskAnalyzer::reality_check(panel, config);
    const auto second = SelectionRiskAnalyzer::reality_check(panel, config);
    check(first.p_value == second.p_value, "fixed-seed p-value reproducibility");
    check(first.bootstrap_max_statistics == second.bootstrap_max_statistics, "fixed-seed distribution reproducibility");
    check(first.candidate_count == 2, "reality check candidate count");
    check(first.block_length == 2, "explicit block length retained");
    check(first.bootstrap_max_statistics.size() == 200, "bootstrap distribution complete");
    check(first.p_value >= 0.0 && first.p_value <= 1.0, "adjusted p-value bounded");
    check(first.observed_best_mean > 0.0, "observed max mean calculated");

    std::vector<CandidateObservation> identical;
    for (int day = 1; day <= 8; ++day) {
        const std::string date = "2024-02-0" + std::to_string(day);
        identical.push_back({0, date, a.candidate_id, a.family, a.ticker, 1.0, 0.002, 0.002, 0.0, true});
        identical.push_back({0, date, b.candidate_id, b.family, b.ticker, 1.0, 0.002, 0.002, 0.0, false});
    }
    const auto null_panel = SelectionRiskAnalyzer::align_common_dates({a, b}, identical, 5);
    const auto null_result = SelectionRiskAnalyzer::reality_check(null_panel, config);
    check(null_result.observed_best_mean == 0.0, "benchmark-identical fixture statistic");
    check(null_result.p_value == 1.0, "benchmark-identical fixture not significant");

    std::set<std::string> ids{a.candidate_id, b.candidate_id, other_ticker.candidate_id, other_experiment.candidate_id};
    check(ids.size() == 4, "candidate enumeration remains unique");
    check(a.parameters == "short=5;long=50", "canonical parameter serialization retained");
    check(panel.dates == std::vector<std::string>({"2024-01-02", "2024-01-03", "2024-01-04", "2024-01-05", "2024-01-06", "2024-01-07", "2024-01-08"}), "chronology retained");
    check(first.method == "centered_moving_block_reality_check", "primary method labelled");

    check(Analysis::grid_strategy_specs("MA_Cross").size() == 16, "complete MA enumeration");
    check(Analysis::grid_strategy_specs("RSI_Mean_Reversion").size() == 12, "complete RSI enumeration");
    check(Analysis::grid_strategy_specs("MACD_Momentum").size() == 4, "complete MACD enumeration");
    check(Analysis::grid_strategy_specs("Volatility_Breakout").size() == 9, "complete breakout enumeration");
    check(Analysis::grid_strategy_specs().size() == 41, "complete cross-family enumeration");
    check(Analysis::grid_strategy_specs("invalid").empty(), "invalid family rejected by enumeration");

    CandidateDefinition changed_context = a;
    changed_context.identity_context = "benchmark=SYN_BENCH|cost=45bps|window=3y_6m";
    changed_context.candidate_id.clear(); changed_context.candidate_id = SelectionRiskAnalyzer::canonical_candidate_id(changed_context);
    check(changed_context.candidate_id != a.candidate_id, "cost context changes candidate ID");
    CandidateDefinition changed_family = a;
    changed_family.family = "RSI_Mean_Reversion"; changed_family.candidate_id.clear();
    changed_family.candidate_id = SelectionRiskAnalyzer::canonical_candidate_id(changed_family);
    check(changed_family.candidate_id != a.candidate_id, "family changes candidate ID");

    quant::analytics::StatisticalConfig automatic = config;
    automatic.block_length = 0;
    const auto automatic_result = SelectionRiskAnalyzer::reality_check(panel, automatic);
    check(automatic_result.block_length == 2, "cube-root automatic block length");
    bool bad_block_rejected = false;
    auto bad_block = config; bad_block.block_length = 20;
    try { (void)SelectionRiskAnalyzer::reality_check(panel, bad_block); }
    catch (const quant::ConfigurationError&) { bad_block_rejected = true; }
    check(bad_block_rejected, "oversized block rejected");

    quant::analytics::StatisticalConfig long_config = config;
    long_config.simulations = 499; long_config.block_length = 5; long_config.minimum_observations = 30;
    std::vector<std::vector<double>> superior(2, std::vector<double>(100, 0.0));
    for (std::size_t i = 0; i < 100; ++i) { superior[0][i] = 0.01; superior[1][i] = 0.002; }
    const auto superior_result = quant::analytics::StatisticalAnalyzer::reality_check(superior, long_config);
    check(superior_result.p_value <= 0.01, "clearly superior candidate fixture");
    std::vector<std::vector<double>> inferior(2, std::vector<double>(100, -0.01));
    const auto inferior_result = quant::analytics::StatisticalAnalyzer::reality_check(inferior, long_config);
    check(inferior_result.p_value > 0.5, "all-inferior candidate fixture");
    std::vector<std::vector<double>> correlated(2, std::vector<double>(100, 0.0));
    for (std::size_t i = 0; i < 100; ++i) { const double value = i % 2 == 0 ? 0.01 : -0.009; correlated[0][i] = value; correlated[1][i] = value * 0.99; }
    const auto correlated_result = quant::analytics::StatisticalAnalyzer::reality_check(correlated, long_config);
    check(correlated_result.candidate_count == 2, "correlated candidates retained");
    check(correlated_result.bootstrap_max_statistics.size() == 499, "correlated fixture distribution complete");
    bool unequal_rejected = false;
    try { (void)quant::analytics::StatisticalAnalyzer::reality_check({std::vector<double>(30), std::vector<double>(31)}, long_config); }
    catch (const quant::MethodologyError&) { unequal_rejected = true; }
    check(unequal_rejected, "unequal candidate lengths rejected");
    check(panel.dates.back() == "2024-01-08", "common panel end date exact");
    check(first.seed == 91 && first.simulations == 200, "bootstrap settings retained");

    std::cout << cases << " candidate selection-risk cases passed\n";
    return 0;
}
