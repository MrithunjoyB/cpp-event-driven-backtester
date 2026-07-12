#include "quant/experiments/SelectionRisk.h"

#include "Analysis.h"
#include "Backtester.h"
#include "ResearchMethodology.h"
#include "quant/domain/Errors.h"
#include "quant/performance/DeterministicExecutor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

namespace quant::experiments {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kSchemaVersion = 3;
constexpr int kSimulations = 1000;
constexpr double kAnnualization = 252.0;

class BuyAndHoldStrategy final : public Strategy {
public:
    std::string name() const override { return "Buy_And_Hold_Benchmark"; }
    std::string parameters() const override { return "decision=first_close;execution=next_open"; }
    SignalEvent on_market_event(const MarketEvent& event, const std::vector<Bar>&) override {
        const auto signal = emitted_ ? SignalType::Hold : SignalType::Buy;
        emitted_ = true;
        return {EventType::Signal, event.date, event.ticker, name(), signal};
    }
    std::unique_ptr<Strategy> clone() const override { return std::make_unique<BuyAndHoldStrategy>(); }
private:
    bool emitted_{false};
};

double mean(const std::vector<double>& values) {
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
}

double stdev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double m = mean(values);
    double sum = 0.0;
    for (double value : values) sum += (value - m) * (value - m);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

double sortino(const std::vector<EquityPoint>& equity) {
    std::vector<double> returns;
    std::vector<double> downside;
    for (std::size_t i = 1; i < equity.size(); ++i) {
        if (equity[i - 1].portfolio_value <= 0.0) continue;
        const double value = equity[i].portfolio_value / equity[i - 1].portfolio_value - 1.0;
        returns.push_back(value);
        if (value < 0.0) downside.push_back(value);
    }
    const double deviation = stdev(downside) * std::sqrt(kAnnualization);
    return deviation > 0.0 ? mean(returns) * kAnnualization / deviation : 0.0;
}

double objective(const PerformanceSummary& summary, const config::ParameterSelectionConfig& selection) {
    if (summary.num_trades < selection.minimum_trades) return -std::numeric_limits<double>::infinity();
    if (selection.objective == "calmar")
        return summary.max_drawdown < 0.0 ? summary.annualized_return / std::abs(summary.max_drawdown) : 0.0;
    if (selection.objective == "excess_return") return summary.excess_return;
    if (selection.objective == "sharpe_maxdd")
        return summary.max_drawdown > -0.35 ? summary.sharpe : -std::numeric_limits<double>::infinity();
    return summary.sharpe;
}

std::vector<std::pair<std::string, double>> returns_from_equity(const std::vector<EquityPoint>& equity) {
    std::vector<std::pair<std::string, double>> result;
    result.reserve(equity.size());
    for (std::size_t i = 1; i < equity.size(); ++i) {
        if (equity[i - 1].portfolio_value <= 0.0) throw MethodologyError("Non-positive equity in candidate history");
        result.emplace_back(equity[i].date, equity[i].portfolio_value / equity[i - 1].portfolio_value - 1.0);
    }
    return result;
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

std::string metadata_key(const CandidateDefinition& candidate) {
    return "schema=3|experiment=" + candidate.experiment_id + "|family=" + candidate.family +
        "|ticker=" + candidate.ticker + "|parameters=" + candidate.parameters + "|" + candidate.identity_context;
}

std::string family_name(const std::string& strategy) {
    if (strategy == "all" || strategy == "All_Strategies") return "all";
    return strategy;
}

std::string git_hash() {
    std::array<char, 128> buffer{};
    std::string result;
    FILE* pipe = popen("git rev-parse HEAD 2>/dev/null", "r");
    if (pipe == nullptr) return "unknown";
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) result += buffer.data();
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result.empty() ? "unknown" : result;
}

std::vector<StrategySpec> specs_for(const std::string& strategy) {
    return family_name(strategy) == "all" ? Analysis::grid_strategy_specs() : Analysis::grid_strategy_specs(strategy);
}

void write_manifest(const config::ExperimentConfig& experiment, const std::string& directory,
                    std::size_t definitions, std::size_t observations, int block_length) {
    std::ofstream out(directory + "/selection_risk_manifest.json");
    out << "{\n"
        << "  \"schema_version\": 3,\n"
        << "  \"experiment_id\": \"" << experiment.name << "\",\n"
        << "  \"history_policy\": \"candidate_normalized_window_diagnostic\",\n"
        << "  \"selected_history_policy\": \"separate_continuous_capital_walk_forward\",\n"
        << "  \"benchmark\": \"" << experiment.benchmark.ticker << "\",\n"
        << "  \"benchmark_execution_policy\": \"first_close_decision_next_open_integer_shares_cost_matched\",\n"
        << "  \"candidate_count\": " << definitions << ",\n"
        << "  \"observation_rows\": " << observations << ",\n"
        << "  \"bootstrap_method\": \"moving_block_circular\",\n"
        << "  \"centering\": \"candidate_sample_mean\",\n"
        << "  \"null_hypothesis\": \"no_candidate_has_positive_expected_active_return\",\n"
        << "  \"seed\": " << experiment.bootstrap.random_seed << ",\n"
        << "  \"simulation_count\": " << kSimulations << ",\n"
        << "  \"block_length\": " << block_length << ",\n"
        << "  \"confidence_level\": 0.95,\n"
        << "  \"missing_date_treatment\": \"strict_common_date_intersection_no_imputation\",\n"
        << "  \"annualization_convention\": \"252_ticker_trading_observations\",\n"
        << "  \"cost_model\": \"commission_bps=" << experiment.execution.commission_bps
        << ";slippage_bps=" << experiment.execution.slippage_bps << "\",\n"
        << "  \"git_commit_hash\": \"" << git_hash() << "\"\n"
        << "}\n";
}

}  // namespace

std::string SelectionRiskAnalyzer::canonical_candidate_id(const CandidateDefinition& candidate) {
    const std::string value = metadata_key(candidate);
    std::uint64_t hash = 14695981039346656037ULL;
    for (char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return "c_" + hex64(hash);
}

AlignedCandidatePanel SelectionRiskAnalyzer::align_common_dates(
    const std::vector<CandidateDefinition>& candidates,
    const std::vector<CandidateObservation>& observations,
    std::size_t minimum_observations) {
    if (candidates.empty()) throw MethodologyError("Candidate panel is empty");
    std::map<std::string, std::map<std::string, double>> by_candidate;
    for (const auto& candidate : candidates) by_candidate[candidate.candidate_id] = {};
    for (const auto& observation : observations) {
        if (!std::isfinite(observation.active_return)) throw DataError("Non-finite candidate active return");
        auto candidate = by_candidate.find(observation.candidate_id);
        if (candidate == by_candidate.end()) continue;
        if (!candidate->second.emplace(observation.date, observation.active_return).second)
            throw DataError("Duplicate candidate/date row: " + observation.candidate_id + "/" + observation.date);
    }
    std::set<std::string> common;
    bool first = true;
    for (const auto& item : by_candidate) {
        std::set<std::string> dates;
        for (const auto& value : item.second) dates.insert(value.first);
        if (first) { common = dates; first = false; }
        else {
            std::set<std::string> intersection;
            std::set_intersection(common.begin(), common.end(), dates.begin(), dates.end(),
                                  std::inserter(intersection, intersection.begin()));
            common = std::move(intersection);
        }
    }
    if (common.size() < minimum_observations) throw MethodologyError("Insufficient common candidate OOS observations");
    AlignedCandidatePanel panel;
    panel.dates.assign(common.begin(), common.end());
    for (const auto& candidate : candidates) {
        panel.candidate_ids.push_back(candidate.candidate_id);
        std::vector<double> values;
        values.reserve(panel.dates.size());
        const auto& source = by_candidate.at(candidate.candidate_id);
        for (const auto& date : panel.dates) values.push_back(source.at(date));
        panel.active_returns.push_back(std::move(values));
        panel.removed_dates.push_back(source.size() - panel.dates.size());
    }
    return panel;
}

analytics::MultipleTestingResult SelectionRiskAnalyzer::reality_check(
    const AlignedCandidatePanel& panel, const analytics::StatisticalConfig& config) {
    if (panel.candidate_ids.size() != panel.active_returns.size())
        throw MethodologyError("Candidate-count mismatch in aligned panel");
    return analytics::StatisticalAnalyzer::reality_check(panel.active_returns, config);
}

void SelectionRiskAnalyzer::run_experiment(const config::ExperimentConfig& experiment) {
    const auto performance_start = Clock::now();
    const std::string directory = experiment.output.results_dir + "/selection_risk";
    std::filesystem::create_directories(directory);
    std::vector<CandidateDefinition> definitions;
    std::vector<CandidateWindowResult> windows;
    std::vector<CandidateObservation> observations;

    auto mutable_market_data = std::make_shared<MarketData>();
    std::set<std::string> required_tickers(experiment.tickers.begin(), experiment.tickers.end());
    if (experiment.benchmark.ticker != "same_asset") required_tickers.insert(experiment.benchmark.ticker);
    for (const auto& ticker : required_tickers) {
        if (!mutable_market_data->load_csv(ticker, experiment.portfolio.data_dir + "/" + ticker + ".csv"))
            throw DataError("Unable to load immutable market data for " + ticker);
    }
    std::shared_ptr<const MarketData> immutable_market_data = mutable_market_data;
    const auto data_load_complete = Clock::now();
    const quant::performance::DeterministicExecutor executor(
        experiment.execution_control.mode, experiment.execution_control.threads);

    for (const auto& ticker : experiment.tickers) {
        const auto& ticker_bars = immutable_market_data->bars(ticker);
        auto specs = specs_for(experiment.strategy);
        std::vector<CandidateDefinition> ticker_definitions;
        for (const auto& spec : specs) {
            CandidateDefinition definition{experiment.name, spec.strategy, ticker, spec.parameter_set, {}, {}};
            std::ostringstream identity;
            identity << "benchmark=" << experiment.benchmark.ticker
                     << "|commission_bps=" << experiment.execution.commission_bps
                     << "|slippage_bps=" << experiment.execution.slippage_bps
                     << "|window_mode=" << experiment.walk_forward.window_mode
                     << "|train_years=" << experiment.walk_forward.train_years
                     << "|test_months=" << experiment.walk_forward.test_months
                     << "|step_months=" << experiment.walk_forward.step_months
                     << "|data_dir=" << experiment.portfolio.data_dir
                     << "|data_range=" << ticker_bars.front().date << ':' << ticker_bars.back().date;
            definition.identity_context = identity.str();
            definition.candidate_id = canonical_candidate_id(definition);
            ticker_definitions.push_back(definition);
            definitions.push_back(definition);
        }
        const auto calendar_windows = build_calendar_windows(
            ticker_bars, experiment.walk_forward.train_years,
            experiment.walk_forward.test_months, experiment.walk_forward.step_months);
        int window_id = 0;
        for (const auto& window : calendar_windows) {
            struct Ranked { std::size_t index; BacktestResult result; double score; };
            auto make_benchmark = [&](const std::string& start, const std::string& end, bool liquidate) {
                BacktestConfig config;
                config.ticker = experiment.benchmark.ticker == "same_asset" ? ticker : experiment.benchmark.ticker;
                config.data_dir = experiment.portfolio.data_dir; config.start_date = start; config.end_date = end;
                config.starting_capital = experiment.execution.starting_capital;
                config.transaction_cost_rate = experiment.execution.commission_bps / 10000.0;
                config.slippage_rate = experiment.execution.slippage_bps / 10000.0;
                config.benchmark_ticker = config.ticker; config.liquidate_at_end = liquidate;
                config.immutable_market_data = immutable_market_data;
                BuyAndHoldStrategy strategy;
                return Backtester(config).run_detailed(strategy);
            };
            const auto training_benchmark_path = make_benchmark(window.train_start, window.train_end, false);
            auto training_benchmark = std::make_shared<const BenchmarkResult>(BenchmarkResult{
                training_benchmark_path.summary.benchmark_gross_return,
                training_benchmark_path.summary.total_return,
                training_benchmark_path.summary.ticker,
                training_benchmark_path.summary.benchmark_execution_policy,
                training_benchmark_path.summary.benchmark_cost_policy});
            auto ranked = executor.map(specs.size(), [&](std::size_t i) {
                BacktestConfig config;
                config.ticker = ticker;
                config.data_dir = experiment.portfolio.data_dir;
                config.start_date = window.train_start;
                config.end_date = window.train_end;
                config.starting_capital = experiment.execution.starting_capital;
                config.transaction_cost_rate = experiment.execution.commission_bps / 10000.0;
                config.slippage_rate = experiment.execution.slippage_bps / 10000.0;
                config.benchmark_ticker = experiment.benchmark.ticker;
                config.immutable_market_data = immutable_market_data;
                config.benchmark_override = training_benchmark;
                auto result = Backtester(config).run_detailed(*specs[i].instance);
                const double score = objective(result.summary, experiment.parameter_selection);
                return Ranked{i, std::move(result), score};
            });
            std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.index < b.index;
            });
            const auto benchmark = make_benchmark(window.test_start, window.test_end, true);
            auto test_benchmark = std::make_shared<const BenchmarkResult>(BenchmarkResult{
                benchmark.summary.benchmark_gross_return, benchmark.summary.total_return, benchmark.summary.ticker,
                benchmark.summary.benchmark_execution_policy, benchmark.summary.benchmark_cost_policy});
            const auto benchmark_returns = returns_from_equity(benchmark.equity_curve);
            std::map<std::string, double> benchmark_by_date;
            for (const auto& value : benchmark_returns) benchmark_by_date.emplace(value.first, value.second);
            struct CandidateEvaluation { CandidateWindowResult row; std::vector<CandidateObservation> observations; };
            auto evaluations = executor.map(ranked.size(), [&](std::size_t rank) {
                const auto& train = ranked[rank];
                const bool eligible = std::isfinite(train.score);
                const std::string& family = ticker_definitions[train.index].family;
                int family_rank = 1;
                bool selected = eligible;
                for (const auto& other : ranked) {
                    if (ticker_definitions[other.index].family != family) continue;
                    if (other.index == train.index) break;
                    ++family_rank;
                    if (std::isfinite(other.score)) selected = false;
                }
                CandidateWindowResult row;
                row.window_id = window_id;
                row.candidate = ticker_definitions[train.index];
                row.train_start = window.train_start; row.train_end = window.train_end;
                row.test_start = window.test_start; row.test_end = window.test_end;
                row.train_observations = window.train_observations; row.test_observations = window.test_observations;
                row.eligible = eligible;
                row.rejection_reason = eligible ? "" : "minimum_trade_requirement_or_invalid_objective";
                row.training_objective = eligible ? train.score : 0.0;
                row.training_return = train.result.summary.total_return;
                row.training_sharpe = train.result.summary.sharpe;
                row.training_rank = family_rank;
                row.selected = selected;
                std::vector<CandidateObservation> evaluation_observations;
                if (eligible) {
                    BacktestConfig test;
                    test.ticker = ticker; test.data_dir = experiment.portfolio.data_dir;
                    test.start_date = window.test_start; test.end_date = window.test_end;
                    test.starting_capital = experiment.execution.starting_capital;
                    test.transaction_cost_rate = experiment.execution.commission_bps / 10000.0;
                    test.slippage_rate = experiment.execution.slippage_bps / 10000.0;
                    test.benchmark_ticker = experiment.benchmark.ticker; test.liquidate_at_end = true;
                    test.immutable_market_data = immutable_market_data;
                    test.benchmark_override = test_benchmark;
                    auto strategy = specs[train.index].instance->clone();
                    const auto result = Backtester(test).run_detailed(*strategy);
                    const auto candidate_returns = returns_from_equity(result.equity_curve);
                    double diagnostic_value = 1.0;
                    for (const auto& value : candidate_returns) {
                        const auto match = benchmark_by_date.find(value.first);
                        if (match == benchmark_by_date.end()) continue;
                        diagnostic_value *= 1.0 + value.second;
                        evaluation_observations.push_back({window_id, value.first, row.candidate.candidate_id,
                            row.candidate.family, ticker, diagnostic_value, value.second, match->second,
                            value.second - match->second, selected});
                    }
                    row.oos_return = result.summary.total_return;
                    row.benchmark_return = benchmark.summary.total_return;
                    row.active_return = row.oos_return - row.benchmark_return;
                    row.sharpe = result.summary.sharpe; row.sortino = sortino(result.equity_curve);
                    row.volatility = result.summary.volatility; row.max_drawdown = result.summary.max_drawdown;
                    row.trade_count = result.summary.num_trades; row.turnover = result.summary.turnover;
                    row.transaction_costs = result.summary.total_transaction_costs;
                    for (const auto& trade : result.trades)
                        if (trade.date == window.test_end && trade.action == "SELL")
                            row.liquidation_costs += trade.cost + trade.slippage;
                }
                return CandidateEvaluation{std::move(row), std::move(evaluation_observations)};
            });
            for (auto& evaluation : evaluations) {
                windows.push_back(std::move(evaluation.row));
                observations.insert(observations.end(),
                    std::make_move_iterator(evaluation.observations.begin()),
                    std::make_move_iterator(evaluation.observations.end()));
            }
            ++window_id;
        }
    }
    const auto candidate_evaluation_complete = Clock::now();

    analytics::StatisticalConfig regime_statistical;
    regime_statistical.method = analytics::BootstrapMethod::MovingBlock;
    regime_statistical.seed = experiment.bootstrap.random_seed;
    regime_statistical.simulations = kSimulations;
    regime_statistical.minimum_observations = 30;
    regime_statistical.annualization_factor = kAnnualization;
    auto regime_out = std::ofstream(directory + "/regime_selection_risk.csv");
    regime_out << "schema_version,experiment_id,ticker,strategy_family,regime,eligible_candidates,common_observations,observed_best_mean_active_return,adjusted_p_value,seed,simulations,block_length,method\n";
    for (const auto& ticker : experiment.tickers) {
        std::map<std::string, std::string> regime_by_date;
        for (const auto& point : classify_causal_regimes(immutable_market_data->bars(ticker))) if (point.available) regime_by_date[point.date] = point.regime;
        std::set<std::string> families;
        for (const auto& definition : definitions) if (definition.ticker == ticker) families.insert(definition.family);
        for (const auto& family : families) {
            std::vector<CandidateDefinition> eligible;
            for (const auto& definition : definitions) if (definition.ticker == ticker && definition.family == family &&
                std::any_of(windows.begin(), windows.end(), [&](const CandidateWindowResult& row) { return row.candidate.candidate_id == definition.candidate_id && row.eligible; })) eligible.push_back(definition);
            for (const auto& regime : {"bull/high_volatility", "bull/low_volatility", "bear/high_volatility", "bear/low_volatility", "sideways/high_volatility", "sideways/low_volatility"}) {
                std::vector<CandidateObservation> filtered;
                for (const auto& observation : observations) {
                    const auto match = regime_by_date.find(observation.date);
                    if (observation.ticker == ticker && observation.family == family && match != regime_by_date.end() && match->second == regime)
                        filtered.push_back(observation);
                }
                try {
                    const auto panel = align_common_dates(eligible, filtered, 30);
                    const auto result = reality_check(panel, regime_statistical);
                    regime_out << kSchemaVersion << ',' << experiment.name << ',' << ticker << ',' << family << ',' << regime << ',' << eligible.size() << ',' << panel.dates.size() << ',' << result.observed_best_mean << ',' << result.p_value << ',' << result.seed << ',' << result.simulations << ',' << result.block_length << ',' << result.method << '\n';
                } catch (const MethodologyError&) {
                    regime_out << kSchemaVersion << ',' << experiment.name << ',' << ticker << ',' << family << ',' << regime << ',' << eligible.size() << ",0,0,1," << regime_statistical.seed << ',' << regime_statistical.simulations << ",0,insufficient_common_observations\n";
                }
            }
        }
    }
    const auto regime_analysis_complete = Clock::now();

    for (auto& row : windows) {
        if (!row.eligible) continue;
        int rank = 1;
        for (const auto& other : windows) {
            if (!other.eligible || other.window_id != row.window_id || other.candidate.ticker != row.candidate.ticker ||
                other.candidate.family != row.candidate.family) continue;
            if (other.active_return > row.active_return ||
                (other.active_return == row.active_return && other.candidate.candidate_id < row.candidate.candidate_id)) ++rank;
        }
        row.oos_rank = rank;
    }

    auto deployable = std::ofstream(directory + "/selected_deployable_oos_returns.csv");
    deployable << "schema_version,experiment_id,ticker,strategy_family,window_id,date,candidate_id,parameter_serialization,portfolio_value,daily_return,cumulative_oos_return,continuity_type,history_type\n";
    for (const auto& ticker : experiment.tickers) {
        std::set<std::string> families;
        for (const auto& row : windows) if (row.candidate.ticker == ticker && row.selected) families.insert(row.candidate.family);
        for (const auto& family : families) {
            double capital = experiment.execution.starting_capital;
            std::vector<const CandidateWindowResult*> selected_rows;
            for (const auto& row : windows) if (row.candidate.ticker == ticker && row.candidate.family == family && row.selected) selected_rows.push_back(&row);
            std::sort(selected_rows.begin(), selected_rows.end(), [](const auto* left, const auto* right) { return left->window_id < right->window_id; });
            for (const auto* selected : selected_rows) {
                auto family_specs = specs_for(family);
                const auto spec = std::find_if(family_specs.begin(), family_specs.end(), [&](const StrategySpec& value) { return value.parameter_set == selected->candidate.parameters; });
                if (spec == family_specs.end()) throw MethodologyError("Selected candidate definition cannot be reconstructed");
                BacktestConfig test;
                test.ticker = ticker; test.data_dir = experiment.portfolio.data_dir;
                test.start_date = selected->test_start; test.end_date = selected->test_end;
                test.starting_capital = capital; test.transaction_cost_rate = experiment.execution.commission_bps / 10000.0;
                test.slippage_rate = experiment.execution.slippage_bps / 10000.0;
                test.benchmark_ticker = experiment.benchmark.ticker; test.liquidate_at_end = true;
                test.immutable_market_data = immutable_market_data;
                const auto result = Backtester(test).run_detailed(*spec->instance);
                for (std::size_t i = 1; i < result.equity_curve.size(); ++i) {
                    const auto& previous = result.equity_curve[i - 1]; const auto& current = result.equity_curve[i];
                    const double daily = current.portfolio_value / previous.portfolio_value - 1.0;
                    deployable << kSchemaVersion << ',' << experiment.name << ',' << ticker << ',' << family << ',' << selected->window_id << ',' << current.date << ',' << selected->candidate.candidate_id << ',' << selected->candidate.parameters << ',' << current.portfolio_value << ',' << daily << ',' << current.portfolio_value / experiment.execution.starting_capital - 1.0 << ",continuous_capital,deployable_selected_strategy\n";
                }
                if (!result.equity_curve.empty()) capital = result.equity_curve.back().portfolio_value;
            }
        }
    }

    auto definitions_out = std::ofstream(directory + "/candidate_definitions.csv");
    definitions_out << "schema_version,experiment_id,strategy_family,ticker,candidate_id,parameter_serialization,identity_context,benchmark,commission_bps,slippage_bps,history_type\n";
    for (const auto& value : definitions) definitions_out << kSchemaVersion << ',' << value.experiment_id << ',' << value.family << ',' << value.ticker << ',' << value.candidate_id << ',' << value.parameters << ',' << value.identity_context << ',' << experiment.benchmark.ticker << ',' << experiment.execution.commission_bps << ',' << experiment.execution.slippage_bps << ",counterfactual_normalized_diagnostic\n";

    auto eligibility = std::ofstream(directory + "/candidate_eligibility.csv");
    auto metrics = std::ofstream(directory + "/candidate_window_metrics.csv");
    eligibility << "schema_version,experiment_id,strategy_family,ticker,window_id,candidate_id,parameter_serialization,train_start,train_end,test_start,test_end,eligible,rejection_reason,selected,training_rank,training_objective\n";
    metrics << "schema_version,experiment_id,strategy_family,ticker,window_id,candidate_id,parameter_serialization,train_start,train_end,test_start,test_end,train_observations,test_observations,eligible,selected,training_objective,training_rank,oos_rank,oos_return,benchmark_return,active_return,sharpe,sortino,volatility,max_drawdown,trade_count,turnover,transaction_costs,liquidation_costs,history_type\n";
    for (const auto& value : windows) {
        eligibility << kSchemaVersion << ',' << experiment.name << ',' << value.candidate.family << ',' << value.candidate.ticker << ',' << value.window_id << ',' << value.candidate.candidate_id << ',' << value.candidate.parameters << ',' << value.train_start << ',' << value.train_end << ',' << value.test_start << ',' << value.test_end << ',' << value.eligible << ',' << value.rejection_reason << ',' << value.selected << ',' << value.training_rank << ',' << value.training_objective << '\n';
        metrics << kSchemaVersion << ',' << experiment.name << ',' << value.candidate.family << ',' << value.candidate.ticker << ',' << value.window_id << ',' << value.candidate.candidate_id << ',' << value.candidate.parameters << ',' << value.train_start << ',' << value.train_end << ',' << value.test_start << ',' << value.test_end << ',' << value.train_observations << ',' << value.test_observations << ',' << value.eligible << ',' << value.selected << ',' << value.training_objective << ',' << value.training_rank << ',' << value.oos_rank << ',' << value.oos_return << ',' << value.benchmark_return << ',' << value.active_return << ',' << value.sharpe << ',' << value.sortino << ',' << value.volatility << ',' << value.max_drawdown << ',' << value.trade_count << ',' << value.turnover << ',' << value.transaction_costs << ',' << value.liquidation_costs << ",counterfactual_normalized_diagnostic\n";
    }

    auto returns = std::ofstream(directory + "/candidate_oos_returns.csv");
    returns << "schema_version,experiment_id,strategy_family,ticker,window_id,date,candidate_id,diagnostic_value,daily_return,benchmark_return,active_return,selected,tradability_status,valuation_status,annualization_convention,cost_model,continuity_type,history_type\n";
    for (const auto& value : observations) returns << kSchemaVersion << ',' << experiment.name << ',' << value.family << ',' << value.ticker << ',' << value.window_id << ',' << value.date << ',' << value.candidate_id << ',' << value.diagnostic_value << ',' << value.candidate_return << ',' << value.benchmark_return << ',' << value.active_return << ',' << value.selected << ",tradable,observed,252_ticker_trading_observations,commission_bps=" << experiment.execution.commission_bps << ";slippage_bps=" << experiment.execution.slippage_bps << ",normalized_window,counterfactual_diagnostic\n";
    returns.close();
    std::filesystem::copy_file(directory + "/candidate_oos_returns.csv", directory + "/candidate_active_returns.csv", std::filesystem::copy_options::overwrite_existing);

    analytics::StatisticalConfig statistical;
    statistical.method = analytics::BootstrapMethod::MovingBlock;
    statistical.seed = experiment.bootstrap.random_seed;
    statistical.simulations = kSimulations;
    statistical.minimum_observations = 30;
    statistical.annualization_factor = kAnnualization;
    auto family_out = std::ofstream(directory + "/family_selection_risk.csv");
    auto cross_out = std::ofstream(directory + "/cross_family_selection_risk.csv");
    auto bootstrap_out = std::ofstream(directory + "/multiple_testing_bootstrap_distribution.csv");
    auto multiple_out = std::ofstream(directory + "/multiple_testing_summary.csv");
    const std::string risk_header = "schema_version,experiment_id,ticker,strategy_family,configured_candidates,eligible_candidates,common_observations,common_start,common_end,observed_best_mean_active_return,adjusted_p_value,seed,simulations,block_length,method,null_hypothesis,centering\n";
    family_out << risk_header; cross_out << risk_header; multiple_out << risk_header;
    bootstrap_out << "schema_version,experiment_id,ticker,strategy_family,simulation,max_centered_mean_active_return,seed,block_length,bootstrap_method\n";
    int manifest_block = 0;
    for (const auto& ticker : experiment.tickers) {
        std::set<std::string> families;
        for (const auto& value : definitions) if (value.ticker == ticker) families.insert(value.family);
        for (const auto& family : families) {
            std::vector<CandidateDefinition> eligible;
            for (const auto& definition : definitions) if (definition.ticker == ticker && definition.family == family) {
                const bool all_windows = std::any_of(windows.begin(), windows.end(), [&](const CandidateWindowResult& row) { return row.candidate.candidate_id == definition.candidate_id && row.eligible; });
                if (all_windows) eligible.push_back(definition);
            }
            if (eligible.empty()) continue;
            const auto panel = align_common_dates(eligible, observations, 30);
            const auto result = reality_check(panel, statistical);
            manifest_block = result.block_length;
            family_out << kSchemaVersion << ',' << experiment.name << ',' << ticker << ',' << family << ',' << specs_for(family).size() << ',' << eligible.size() << ',' << panel.dates.size() << ',' << panel.dates.front() << ',' << panel.dates.back() << ',' << result.observed_best_mean << ',' << result.p_value << ',' << result.seed << ',' << result.simulations << ',' << result.block_length << ',' << result.method << ",no_candidate_has_positive_expected_active_return,candidate_sample_mean\n";
            multiple_out << kSchemaVersion << ',' << experiment.name << ',' << ticker << ',' << family << ',' << specs_for(family).size() << ',' << eligible.size() << ',' << panel.dates.size() << ',' << panel.dates.front() << ',' << panel.dates.back() << ',' << result.observed_best_mean << ',' << result.p_value << ',' << result.seed << ',' << result.simulations << ',' << result.block_length << ',' << result.method << ",no_candidate_has_positive_expected_active_return,candidate_sample_mean\n";
            for (std::size_t simulation = 0; simulation < result.bootstrap_max_statistics.size(); ++simulation)
                bootstrap_out << kSchemaVersion << ',' << experiment.name << ',' << ticker << ',' << family << ',' << simulation << ',' << result.bootstrap_max_statistics[simulation] << ',' << result.seed << ',' << result.block_length << ',' << result.method << '\n';
        }
        if (families.size() > 1) {
            std::vector<CandidateDefinition> eligible;
            for (const auto& definition : definitions) if (definition.ticker == ticker) {
                const bool any = std::any_of(windows.begin(), windows.end(), [&](const CandidateWindowResult& row) { return row.candidate.candidate_id == definition.candidate_id && row.eligible; });
                if (any) eligible.push_back(definition);
            }
            const auto panel = align_common_dates(eligible, observations, 30);
            const auto result = reality_check(panel, statistical);
            const auto configured = static_cast<std::size_t>(std::count_if(definitions.begin(), definitions.end(),
                [&](const CandidateDefinition& definition) { return definition.ticker == ticker; }));
            cross_out << kSchemaVersion << ',' << experiment.name << ',' << ticker << ",all," << configured << ',' << eligible.size() << ',' << panel.dates.size() << ',' << panel.dates.front() << ',' << panel.dates.back() << ',' << result.observed_best_mean << ',' << result.p_value << ',' << result.seed << ',' << result.simulations << ',' << result.block_length << ',' << result.method << ",no_candidate_has_positive_expected_active_return,candidate_sample_mean\n";
            for (std::size_t simulation = 0; simulation < result.bootstrap_max_statistics.size(); ++simulation)
                bootstrap_out << kSchemaVersion << ',' << experiment.name << ',' << ticker << ",all," << simulation << ',' << result.bootstrap_max_statistics[simulation] << ',' << result.seed << ',' << result.block_length << ',' << result.method << '\n';
        }
    }
    if (family_name(experiment.strategy) != "all") {
        cross_out.close();
        cross_out = std::ofstream(directory + "/cross_family_selection_risk.csv", std::ios::trunc);
        cross_out << "schema_version,experiment_id,status,note\n" << kSchemaVersion << ',' << experiment.name
                  << ",not_applicable,single_family_experiment\n";
    }

    auto selection = std::ofstream(directory + "/candidate_selection_history.csv");
    selection << "schema_version,experiment_id,ticker,strategy_family,window_id,candidate_id,parameter_serialization,training_rank,training_objective,oos_active_return\n";
    for (const auto& row : windows) if (row.selected)
        selection << kSchemaVersion << ',' << experiment.name << ',' << row.candidate.ticker << ',' << row.candidate.family << ',' << row.window_id << ',' << row.candidate.candidate_id << ',' << row.candidate.parameters << ',' << row.training_rank << ',' << row.training_objective << ',' << row.active_return << '\n';

    auto frequency = std::ofstream(directory + "/candidate_parameter_frequency.csv");
    auto ranks = std::ofstream(directory + "/candidate_rank_stability.csv");
    auto degradation = std::ofstream(directory + "/is_oos_degradation.csv");
    frequency << "schema_version,experiment_id,ticker,strategy_family,candidate_id,parameter_serialization,eligible_windows,selected_windows,selection_frequency,positive_oos_window_frequency,benchmark_beating_window_frequency\n";
    ranks << "schema_version,experiment_id,ticker,strategy_family,candidate_id,average_training_rank,training_rank_stddev,average_oos_rank,oos_rank_stddev,is_oos_spearman_rank_correlation,average_oos_active_return,median_oos_active_return,worst_oos_active_return,best_oos_active_return\n";
    degradation << "schema_version,experiment_id,ticker,strategy_family,candidate_id,mean_is_return,mean_oos_active_return,is_to_oos_return_degradation,mean_is_sharpe,mean_oos_sharpe,is_to_oos_sharpe_degradation\n";
    for (const auto& definition : definitions) {
        std::vector<double> train_ranks, oos_ranks, oos_active, is_objective, oos_sharpe;
        int selected_count = 0, positive = 0, beating = 0;
        for (const auto& row : windows) if (row.candidate.candidate_id == definition.candidate_id && row.eligible) {
            train_ranks.push_back(static_cast<double>(row.training_rank)); oos_ranks.push_back(static_cast<double>(row.oos_rank)); oos_active.push_back(row.active_return);
            is_objective.push_back(row.training_return); oos_sharpe.push_back(row.sharpe);
            selected_count += row.selected ? 1 : 0; positive += row.oos_return > 0.0 ? 1 : 0; beating += row.active_return > 0.0 ? 1 : 0;
        }
        if (train_ranks.empty()) continue;
        auto sorted = oos_active; std::sort(sorted.begin(), sorted.end());
        const double denominator = static_cast<double>(train_ranks.size());
        frequency << kSchemaVersion << ',' << experiment.name << ',' << definition.ticker << ',' << definition.family << ',' << definition.candidate_id << ',' << definition.parameters << ',' << train_ranks.size() << ',' << selected_count << ',' << selected_count / denominator << ',' << positive / denominator << ',' << beating / denominator << '\n';
        double covariance = 0.0;
        for (std::size_t i = 0; i < train_ranks.size(); ++i) covariance += (train_ranks[i] - mean(train_ranks)) * (oos_ranks[i] - mean(oos_ranks));
        const double correlation = train_ranks.size() > 1 && stdev(train_ranks) > 0.0 && stdev(oos_ranks) > 0.0
            ? covariance / (static_cast<double>(train_ranks.size() - 1) * stdev(train_ranks) * stdev(oos_ranks)) : 0.0;
        ranks << kSchemaVersion << ',' << experiment.name << ',' << definition.ticker << ',' << definition.family << ',' << definition.candidate_id << ',' << mean(train_ranks) << ',' << stdev(train_ranks) << ',' << mean(oos_ranks) << ',' << stdev(oos_ranks) << ',' << correlation << ',' << mean(oos_active) << ',' << sorted[sorted.size() / 2] << ',' << sorted.front() << ',' << sorted.back() << '\n';
        std::vector<double> is_sharpe;
        for (const auto& row : windows) if (row.candidate.candidate_id == definition.candidate_id && row.eligible) is_sharpe.push_back(row.training_sharpe);
        degradation << kSchemaVersion << ',' << experiment.name << ',' << definition.ticker << ',' << definition.family << ',' << definition.candidate_id << ',' << mean(is_objective) << ',' << mean(oos_active) << ',' << mean(oos_active) - mean(is_objective) << ',' << mean(is_sharpe) << ',' << mean(oos_sharpe) << ',' << mean(oos_sharpe) - mean(is_sharpe) << '\n';
    }

    auto parameter_values = std::ofstream(directory + "/parameter_value_frequency.csv");
    parameter_values << "schema_version,experiment_id,ticker,strategy_family,parameter_name,parameter_value,eligible_windows,selected_windows,selection_frequency\n";
    std::map<std::tuple<std::string, std::string, std::string, std::string>, std::pair<int, int>> parameter_counts;
    for (const auto& row : windows) if (row.eligible) {
        std::istringstream parts(row.candidate.parameters); std::string part;
        while (std::getline(parts, part, ';')) {
            const auto separator = part.find('='); if (separator == std::string::npos) continue;
            auto& count = parameter_counts[{row.candidate.ticker, row.candidate.family, part.substr(0, separator), part.substr(separator + 1)}];
            ++count.first; count.second += row.selected ? 1 : 0;
        }
    }
    for (const auto& value : parameter_counts) parameter_values << kSchemaVersion << ',' << experiment.name << ',' << std::get<0>(value.first) << ',' << std::get<1>(value.first) << ',' << std::get<2>(value.first) << ',' << std::get<3>(value.first) << ',' << value.second.first << ',' << value.second.second << ',' << static_cast<double>(value.second.second) / value.second.first << '\n';

    auto family_frequency = std::ofstream(directory + "/family_selection_frequency.csv");
    family_frequency << "schema_version,experiment_id,ticker,strategy_family,selected_windows,total_family_selections,family_selection_share\n";
    std::map<std::pair<std::string, std::string>, int> family_counts; std::map<std::string, int> ticker_counts;
    for (const auto& row : windows) if (row.selected) { ++family_counts[{row.candidate.ticker, row.candidate.family}]; ++ticker_counts[row.candidate.ticker]; }
    for (const auto& value : family_counts) family_frequency << kSchemaVersion << ',' << experiment.name << ',' << value.first.first << ',' << value.first.second << ',' << value.second << ',' << ticker_counts[value.first.first] << ',' << static_cast<double>(value.second) / ticker_counts[value.first.first] << '\n';

    auto transitions = std::ofstream(directory + "/parameter_transition_matrix.csv");
    transitions << "schema_version,experiment_id,ticker,strategy_family,from_candidate_id,to_candidate_id,transition_count\n";
    for (const auto& ticker : experiment.tickers) {
        std::map<std::pair<std::string, std::string>, int> counts;
        for (const auto& family : std::set<std::string>{"MA_Cross", "RSI_Mean_Reversion", "MACD_Momentum", "Volatility_Breakout"}) {
            std::vector<const CandidateWindowResult*> selected;
            for (const auto& row : windows) if (row.candidate.ticker == ticker && row.candidate.family == family && row.selected) selected.push_back(&row);
            std::sort(selected.begin(), selected.end(), [](const auto* a, const auto* b) { return a->window_id < b->window_id; });
            for (std::size_t i = 1; i < selected.size(); ++i) ++counts[{selected[i - 1]->candidate.candidate_id, selected[i]->candidate.candidate_id}];
            for (const auto& value : counts) transitions << kSchemaVersion << ',' << experiment.name << ',' << ticker << ',' << family << ',' << value.first.first << ',' << value.first.second << ',' << value.second << '\n';
            counts.clear();
        }
    }

    auto neighbourhood = std::ofstream(directory + "/neighbourhood_sensitivity.csv");
    neighbourhood << "schema_version,experiment_id,ticker,strategy_family,window_id,selected_candidate_id,neighbour_candidate_id,neighbour_definition,oos_active_return_difference,oos_sharpe_difference,oos_drawdown_difference,training_rank_difference,isolated_optimum\n";
    for (const auto& selected_row : windows) if (selected_row.selected) {
        auto family_specs = specs_for(selected_row.candidate.family);
        auto selected_position = std::find_if(family_specs.begin(), family_specs.end(), [&](const StrategySpec& spec) { return spec.parameter_set == selected_row.candidate.parameters; });
        if (selected_position == family_specs.end()) continue;
        const auto index = static_cast<std::size_t>(std::distance(family_specs.begin(), selected_position));
        for (std::size_t neighbor_index : {index > 0 ? index - 1 : index, index + 1 < family_specs.size() ? index + 1 : index}) {
            if (neighbor_index == index) continue;
            const auto neighbor = std::find_if(windows.begin(), windows.end(), [&](const CandidateWindowResult& row) {
                return row.window_id == selected_row.window_id && row.candidate.ticker == selected_row.candidate.ticker &&
                    row.candidate.family == selected_row.candidate.family && row.candidate.parameters == family_specs[neighbor_index].parameter_set;
            });
            if (neighbor == windows.end() || !neighbor->eligible) continue;
            const double active_difference = selected_row.active_return - neighbor->active_return;
            neighbourhood << kSchemaVersion << ',' << experiment.name << ',' << selected_row.candidate.ticker << ',' << selected_row.candidate.family << ',' << selected_row.window_id << ',' << selected_row.candidate.candidate_id << ',' << neighbor->candidate.candidate_id << ",adjacent_canonical_grid_index," << active_difference << ',' << selected_row.sharpe - neighbor->sharpe << ',' << selected_row.max_drawdown - neighbor->max_drawdown << ',' << neighbor->training_rank - selected_row.training_rank << ',' << (std::abs(active_difference) > 0.10 ? 1 : 0) << '\n';
        }
    }
    auto warnings = std::ofstream(directory + "/selection_risk_warnings.csv");
    warnings << "schema_version,experiment_id,severity,warning\n" << kSchemaVersion << ',' << experiment.name << ",warning,regime_subsets_with_fewer_than_30_common_observations_are_reported_as_insufficient\n";
    write_manifest(experiment, directory, definitions.size(), observations.size(), manifest_block);
    std::ofstream execution_metadata(directory + "/parallel_execution_metadata.json");
    execution_metadata << "{\n  \"execution_mode\": \"" << executor.mode() << "\",\n"
                       << "  \"effective_threads\": " << executor.threads() << ",\n"
                       << "  \"parallel_scope\": \"independent_candidate_training_and_oos_simulation\",\n"
                       << "  \"deterministic_collection\": true\n}\n";
    const auto performance_end = Clock::now();
    const auto milliseconds = [](Clock::time_point begin, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    };
    std::ofstream counters(directory + "/performance_counters.csv");
    counters << "stage,milliseconds\n"
             << "data_loading," << milliseconds(performance_start, data_load_complete) << '\n'
             << "candidate_and_benchmark_evaluation," << milliseconds(data_load_complete, candidate_evaluation_complete) << '\n'
             << "regime_analysis," << milliseconds(candidate_evaluation_complete, regime_analysis_complete) << '\n'
             << "alignment_bootstrap_export_and_selected_continuity," << milliseconds(regime_analysis_complete, performance_end) << '\n'
             << "total," << milliseconds(performance_start, performance_end) << '\n';
}

}  // namespace quant::experiments
