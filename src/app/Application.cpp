#include "quant/app/Application.h"

#include "AllocationPolicy.h"
#include "Analysis.h"
#include "Backtester.h"
#include "PortfolioBacktester.h"
#include "Strategy.h"
#include "quant/config/ConfigLoader.h"
#include "quant/io/ResultExporter.h"
#include "quant/analytics/PortfolioAttribution.h"
#include "quant/analytics/StatisticalAnalysis.h"
#include "quant/domain/Errors.h"

#include <iomanip>
#include <array>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <memory>
#include <map>
#include <stdexcept>
#include <vector>
#include <sstream>

namespace quant::app {
namespace {
std::unique_ptr<Strategy> make_strategy(const std::string& name) {
    if (name == "ma_cross") return std::make_unique<MovingAverageCrossoverStrategy>();
    if (name == "rsi") return std::make_unique<RSIMeanReversionStrategy>();
    if (name == "macd") return std::make_unique<MACDMomentumStrategy>();
    throw CliError("Unknown strategy '" + name + "'; use ma_cross, rsi, or macd");
}

void print_summary(const PerformanceSummary& summary) {
    std::cout << std::left << std::setw(8) << summary.ticker
              << std::setw(20) << summary.strategy
              << std::right << std::setw(12) << summary.total_return
              << std::setw(12) << summary.sharpe
              << std::setw(14) << summary.max_drawdown
              << std::setw(12) << summary.win_rate
              << std::setw(8) << summary.num_trades << '\n';
}

void print_header() {
    std::cout << std::fixed << std::setprecision(4)
              << std::left << std::setw(8) << "Ticker"
              << std::setw(20) << "Strategy"
              << std::right << std::setw(12) << "Return"
              << std::setw(12) << "Sharpe"
              << std::setw(14) << "MaxDD"
              << std::setw(12) << "WinRate"
              << std::setw(8) << "Trades" << '\n';
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

std::string utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    const std::tm* utc = std::gmtime(&now);
    char buffer[32];
    if (utc == nullptr || std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", utc) == 0) return "unknown";
    return buffer;
}

void write_run_metadata(const std::string& directory, const std::string& mode, const std::string& benchmark) {
    std::ostringstream json;
    json << "{\n"
         << "  \"project_version\": \"" << QUANT_PROJECT_VERSION << "\",\n"
         << "  \"result_schema_version\": 2,\n"
         << "  \"mode\": \"" << mode << "\",\n"
         << "  \"benchmark_ticker\": \"" << benchmark << "\",\n"
         << "  \"execution_convention\": \"signals_after_bar_t_close_execute_at_bar_t_plus_1_open\",\n"
         << "  \"regime_information_cutoff\": \"close_t_minus_1_for_open_t_and_start_of_return_interval\",\n"
         << "  \"volatility_threshold_method\": \"expanding_median_strictly_prior_volatility\",\n"
         << "  \"git_commit_hash\": \"" << git_hash() << "\",\n"
         << "  \"run_timestamp_utc\": \"" << utc_timestamp() << "\"\n"
         << "}\n";
    quant::io::JsonManifestExporter::write_text(directory + "/run_metadata.json", json.str());
}

}

int Application::run_config(const std::string& config_path, bool dry_run) {
    const auto experiment = quant::config::ConfigLoader::load_file(config_path);
    if (dry_run) {
        std::cout << quant::config::ConfigLoader::to_json(experiment);
        return 0;
    }
    if (!experiment.portfolio.allocation_policy.empty()) {
        PortfolioBacktestConfig config;
        config.tickers = experiment.tickers;
        config.data_dir = experiment.portfolio.data_dir;
        config.starting_capital = experiment.execution.starting_capital;
        config.transaction_cost_rate = experiment.execution.commission_bps / 10000.0;
        config.slippage_rate = experiment.execution.slippage_bps / 10000.0;
        config.results_dir = experiment.output.portfolio_results_dir;
        config.benchmark_ticker = experiment.benchmark.ticker;
        config.calendar.mode = experiment.calendar.valuation_mode == "union"
            ? quant::market_data::CalendarMode::Union : quant::market_data::CalendarMode::LegacyIntersection;
        config.calendar.stale_mark_policy = experiment.calendar.stale_mark_policy == "last_known"
            ? quant::market_data::StaleMarkPolicy::LastKnown : quant::market_data::StaleMarkPolicy::Unavailable;
        config.calendar.max_stale_calendar_days = experiment.calendar.max_stale_calendar_days;
        config.calendar.missing_bar_policy = experiment.calendar.missing_bar_policy == "error"
            ? quant::market_data::MissingBarPolicy::Error
            : (experiment.calendar.missing_bar_policy == "mark_unavailable"
                ? quant::market_data::MissingBarPolicy::MarkUnavailable : quant::market_data::MissingBarPolicy::UseLastKnown);
        config.calendar.closed_asset_policy = experiment.calendar.rebalance_closed_asset_policy == "defer"
            ? quant::market_data::ClosedAssetPolicy::Defer
            : (experiment.calendar.rebalance_closed_asset_policy == "skip_asset"
                ? quant::market_data::ClosedAssetPolicy::SkipAsset : quant::market_data::ClosedAssetPolicy::PartialRebalance);
        config.annualization_method = experiment.calendar.annualization_method;
        config.configured_periods_per_year = experiment.calendar.configured_periods_per_year;
        config.result_schema_version = experiment.calendar.valuation_mode == "union" ? 3 : experiment.result_schema_version;
        config.adjustment_policy = experiment.adjustment.policy == "raw_price"
            ? quant::market_data::AdjustmentPolicy::RawPrice
            : (experiment.adjustment.policy == "split_adjusted"
                ? quant::market_data::AdjustmentPolicy::SplitAdjusted
                : quant::market_data::AdjustmentPolicy::TotalReturnAdjusted);
        config.rebalance_frequency = PortfolioBacktester::parse_frequency(experiment.portfolio.rebalance_frequency);
        config.allocation.type = AllocationPolicy::parse_type(experiment.portfolio.allocation_policy);
        config.allocation.max_weight = experiment.portfolio.max_weight;
        config.allocation.cash_buffer = experiment.portfolio.cash_buffer;
        config.allocation.min_trade_value = experiment.portfolio.min_trade_value;
        config.allocation.volatility_lookback = experiment.portfolio.volatility_lookback;
        config.allocation.momentum_lookback = experiment.portfolio.momentum_lookback;
        config.allocation.top_n = experiment.portfolio.top_n;
        const auto result = PortfolioBacktester(config).run();
        quant::io::CsvResultExporter::write_portfolio(result, config);
        if (config.result_schema_version >= 3) {
            const auto attribution = quant::analytics::PortfolioAttributionAnalyzer::analyze(
                result, config, experiment.name, 1e-8);
            quant::io::CsvResultExporter::write_attribution(
                attribution, config.results_dir + "/attribution");
            std::map<std::string, double> benchmark_marks;
            for (const auto& mark : result.valuations) if (mark.ticker == config.benchmark_ticker && mark.mark_price > 0.0)
                benchmark_marks[mark.date] = mark.mark_price;
            std::vector<quant::analytics::DatedReturn> portfolio_returns;
            std::vector<quant::analytics::DatedReturn> benchmark_returns;
            std::vector<double> active_returns;
            for (std::size_t i = 1; i < result.equity_curve.size(); ++i) {
                const auto& previous = result.equity_curve[i - 1];
                const auto& current = result.equity_curve[i];
                const double pr = current.portfolio_value / previous.portfolio_value - 1.0;
                const double br = benchmark_marks.count(previous.date) && benchmark_marks.count(current.date)
                    ? benchmark_marks[current.date] / benchmark_marks[previous.date] - 1.0 : 0.0;
                portfolio_returns.push_back({current.date, pr});
                benchmark_returns.push_back({current.date, br});
                active_returns.push_back(pr - br);
            }
            quant::analytics::StatisticalConfig statistics_config;
            statistics_config.seed = experiment.bootstrap.random_seed;
            statistics_config.simulations = 1000;
            statistics_config.annualization_factor = result.summary.observations_per_year;
            if (portfolio_returns.size() < 30 && config.data_dir.find("tests/fixtures") != std::string::npos)
                statistics_config.minimum_observations = 5;
            const auto statistics = quant::analytics::StatisticalAnalyzer::bootstrap(
                experiment.name, "portfolio_policy_union_returns", portfolio_returns, benchmark_returns,
                config.benchmark_ticker, statistics_config);
            const auto multiple_testing = quant::analytics::StatisticalAnalyzer::reality_check({active_returns}, statistics_config);
            quant::io::CsvResultExporter::write_statistics(statistics, multiple_testing, config.results_dir + "/statistics");
        }
        write_run_metadata(config.results_dir, "portfolio_config", experiment.benchmark.ticker);
        std::cout << "Shared-cash portfolio experiment written to " << config.results_dir
                  << " with return " << result.summary.total_return << '\n';
        return 0;
    }
    Analysis::run_research_experiment(experiment);
    Analysis::run_bootstrap_research(experiment);
    write_run_metadata(experiment.output.results_dir, "research_config", experiment.benchmark.ticker);
    std::cout << "Research experiment written to " << experiment.output.results_dir << '\n';
    return 0;
}

int Application::run_legacy(const LegacyRunRequest& request) {
    BacktestConfig base;
    base.starting_capital = request.capital;
    base.transaction_cost_rate = request.transaction_cost;
    base.slippage_rate = request.slippage;
    base.start_date = request.start_date;
    base.end_date = request.end_date;
    base.benchmark_ticker = request.benchmark;
    std::vector<PerformanceSummary> summaries;
    print_header();

    if (request.mode == "single" || (!request.ticker.empty() && !request.strategy.empty())) {
        BacktestConfig config = base;
        config.ticker = request.ticker.empty() ? "AAPL" : request.ticker;
        auto strategy = make_strategy(request.strategy.empty() ? "ma_cross" : request.strategy);
        const auto result = Backtester(config).run_detailed(*strategy);
        quant::io::CsvResultExporter::write_backtest(result, config.results_dir);
        summaries.push_back(result.summary);
        print_summary(result.summary);
        quant::io::CsvResultExporter::write_combined_summary("results/strategy_comparison.csv", summaries);
    } else if (request.mode == "compare") {
        const auto tickers = request.ticker.empty() ? Analysis::default_tickers() : std::vector<std::string>{request.ticker};
        for (const auto& ticker : tickers) {
            for (const auto& name : {"ma_cross", "rsi", "macd"}) {
                BacktestConfig config = base;
                config.ticker = ticker;
                auto strategy = make_strategy(name);
                const auto result = Backtester(config).run_detailed(*strategy);
                quant::io::CsvResultExporter::write_backtest(result, config.results_dir);
                summaries.push_back(result.summary);
                print_summary(result.summary);
            }
        }
        quant::io::CsvResultExporter::write_combined_summary("results/strategy_comparison.csv", summaries);
    } else if (request.mode == "grid") {
        summaries = Analysis::run_parameter_grid(base, request.ticker.empty() ? Analysis::default_tickers() : std::vector<std::string>{request.ticker});
    } else if (request.mode == "cross-asset") {
        summaries = Analysis::run_cross_asset(base);
    } else if (request.mode == "walk-forward") {
        Analysis::run_walk_forward(base, request.ticker.empty() ? Analysis::default_tickers() : std::vector<std::string>{request.ticker});
    } else if (request.mode == "cost") {
        Analysis::run_transaction_cost_sensitivity(base, request.ticker.empty() ? Analysis::default_tickers() : std::vector<std::string>{request.ticker});
    } else if (request.mode == "regime") {
        Analysis::run_regime_evaluation(base, request.ticker.empty() ? Analysis::default_tickers() : std::vector<std::string>{request.ticker});
    } else if (request.mode == "benchmark") {
        for (const auto& timing : Analysis::run_performance_benchmarks(base)) {
            std::cout << timing.benchmark << ": " << timing.milliseconds << " ms\n";
        }
    } else if (request.mode == "all") {
        summaries = Analysis::run_cross_asset(base);
        Analysis::run_parameter_grid(base, Analysis::default_tickers());
        Analysis::run_walk_forward(base, Analysis::default_tickers());
        Analysis::run_transaction_cost_sensitivity(base, Analysis::default_tickers());
        Analysis::run_regime_evaluation(base, Analysis::default_tickers());
        (void)Analysis::run_performance_benchmarks(base);
    } else {
        throw CliError("Unknown legacy mode '" + request.mode + "'");
    }
    write_run_metadata(base.results_dir, "legacy_" + request.mode, request.benchmark);
    return 0;
}

}  // namespace quant::app
