#include "Analysis.h"
#include "Backtester.h"
#include "PortfolioBacktester.h"
#include "Strategy.h"

#include <iomanip>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
std::string get_arg(int argc, char** argv, const std::string& key, const std::string& fallback) {
    for (int i = 1; i < argc - 1; ++i) {
        if (argv[i] == key) {
            return argv[i + 1];
        }
    }
    return fallback;
}

double get_double_arg(int argc, char** argv, const std::string& key, double fallback) {
    std::string value = get_arg(argc, argv, key, "");
    if (value.empty()) {
        return fallback;
    }
    return std::stod(value);
}

std::unique_ptr<Strategy> make_strategy(const std::string& name) {
    if (name == "ma_cross") {
        return std::make_unique<MovingAverageCrossoverStrategy>();
    }
    if (name == "rsi") {
        return std::make_unique<RSIMeanReversionStrategy>();
    }
    if (name == "macd") {
        return std::make_unique<MACDMomentumStrategy>();
    }
    throw std::runtime_error("Unknown strategy: " + name + ". Use ma_cross, rsi, or macd.");
}

void print_summary(const PerformanceSummary& s) {
    std::cout << std::left << std::setw(8) << s.ticker
              << std::setw(20) << s.strategy
              << std::right << std::setw(12) << s.total_return
              << std::setw(12) << s.sharpe
              << std::setw(14) << s.max_drawdown
              << std::setw(12) << s.win_rate
              << std::setw(8) << s.num_trades << '\n';
}

std::vector<std::string> selected_tickers(const std::string& ticker) {
    if (!ticker.empty()) {
        return {ticker};
    }
    return Analysis::default_tickers();
}

std::string git_hash() {
    std::array<char, 128> buffer{};
    std::string result;
    FILE* pipe = popen("git rev-parse HEAD 2>/dev/null", "r");
    if (!pipe) {
        return "unknown";
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result.empty() ? "unknown" : result;
}

std::string timestamp_utc() {
    std::time_t now = std::time(nullptr);
    std::tm* utc = std::gmtime(&now);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", utc);
    return buffer;
}

void write_run_metadata(const BacktestConfig& config, const std::string& mode, const std::string& ticker, const std::string& strategy) {
    std::ofstream out(config.results_dir + "/run_metadata.json");
    out << "{\n"
        << "  \"mode\": \"" << mode << "\",\n"
        << "  \"ticker_argument\": \"" << ticker << "\",\n"
        << "  \"strategy_argument\": \"" << strategy << "\",\n"
        << "  \"starting_capital\": " << config.starting_capital << ",\n"
        << "  \"commission_bps\": " << config.transaction_cost_rate * 10000.0 << ",\n"
        << "  \"slippage_bps\": " << config.slippage_rate * 10000.0 << ",\n"
        << "  \"data_start_date_argument\": \"" << config.start_date << "\",\n"
        << "  \"data_end_date_argument\": \"" << config.end_date << "\",\n"
        << "  \"execution_convention\": \"signals_after_bar_t_close_execute_at_bar_t_plus_1_open\",\n"
        << "  \"benchmark_ticker\": \"" << config.benchmark_ticker << "\",\n"
        << "  \"benchmark_execution_policy\": \"first_close_decision_next_open_integer_shares_5pct_cash_reserve\",\n"
        << "  \"benchmark_cost_policy\": \"strategy_costs_for_net_zero_costs_for_gross\",\n"
        << "  \"benchmark_excess_return_basis\": \"strategy_net_total_return_minus_benchmark_net_return\",\n"
        << "  \"regime_information_cutoff\": \"close_t_minus_1_for_open_t_and_start_of_return_interval\",\n"
        << "  \"volatility_threshold_method\": \"expanding_median_strictly_prior_volatility\",\n"
        << "  \"result_schema_version\": 2,\n"
        << "  \"run_timestamp_utc\": \"" << timestamp_utc() << "\",\n"
        << "  \"git_commit_hash\": \"" << git_hash() << "\"\n"
        << "}\n";
}
}

int main(int argc, char** argv) {
    try {
        std::string ticker = get_arg(argc, argv, "--ticker", "");
        std::string strategy_name = get_arg(argc, argv, "--strategy", "");
        std::string mode = get_arg(argc, argv, "--mode", "compare");
        std::string config_path = get_arg(argc, argv, "--config", "");
        std::string start_date = get_arg(argc, argv, "--start", "");
        std::string end_date = get_arg(argc, argv, "--end", "");
        double capital = get_double_arg(argc, argv, "--capital", 100000.0);
        double transaction_cost = get_double_arg(argc, argv, "--transaction-cost", 0.001);
        double slippage = get_double_arg(argc, argv, "--slippage", 0.0005);
        std::string benchmark = get_arg(argc, argv, "--benchmark", "same_asset");

        BacktestConfig base_config;
        base_config.starting_capital = capital;
        base_config.transaction_cost_rate = transaction_cost;
        base_config.slippage_rate = slippage;
        base_config.start_date = start_date;
        base_config.end_date = end_date;
        base_config.benchmark_ticker = benchmark;

        std::vector<PerformanceSummary> summaries;
        std::cout << std::fixed << std::setprecision(4);
        std::cout << std::left << std::setw(8) << "Ticker"
                  << std::setw(20) << "Strategy"
                  << std::right << std::setw(12) << "Return"
                  << std::setw(12) << "Sharpe"
                  << std::setw(14) << "MaxDD"
                  << std::setw(12) << "WinRate"
                  << std::setw(8) << "Trades" << '\n';

        if (!config_path.empty()) {
            ExperimentConfig experiment = Analysis::load_experiment_config(config_path);
            base_config.starting_capital = experiment.starting_capital;
            base_config.transaction_cost_rate = experiment.commission_bps / 10000.0;
            base_config.slippage_rate = experiment.slippage_bps / 10000.0;
            base_config.benchmark_ticker = experiment.benchmark;
            base_config.results_dir = experiment.allocation_policy.empty()
                ? experiment.output_dir : experiment.portfolio_output_dir;
            if (!experiment.allocation_policy.empty()) {
                PortfolioBacktestConfig portfolio_config;
                portfolio_config.tickers = experiment.tickers;
                portfolio_config.starting_capital = experiment.starting_capital;
                portfolio_config.transaction_cost_rate = experiment.commission_bps / 10000.0;
                portfolio_config.slippage_rate = experiment.slippage_bps / 10000.0;
                portfolio_config.results_dir = experiment.portfolio_output_dir;
                portfolio_config.benchmark_ticker = experiment.benchmark;
                portfolio_config.rebalance_frequency = PortfolioBacktester::parse_frequency(experiment.rebalance_frequency);
                portfolio_config.allocation.type = AllocationPolicy::parse_type(experiment.allocation_policy);
                portfolio_config.allocation.max_weight = experiment.max_weight;
                portfolio_config.allocation.cash_buffer = experiment.cash_buffer;
                portfolio_config.allocation.min_trade_value = experiment.min_trade_value;
                portfolio_config.allocation.volatility_lookback = experiment.volatility_lookback;
                portfolio_config.allocation.momentum_lookback = experiment.momentum_lookback;
                portfolio_config.allocation.top_n = experiment.top_n;
                PortfolioBacktestResult result = PortfolioBacktester(portfolio_config).run(true);
                std::cout << "Shared-cash portfolio experiment written to " << portfolio_config.results_dir
                          << " with return " << result.summary.total_return << "\n";
            } else {
                Analysis::run_research_experiment(experiment);
                Analysis::run_bootstrap_research(experiment);
                std::cout << "Research experiment written to " << experiment.output_dir << "\n";
            }
        } else if (mode == "single" || (!ticker.empty() && !strategy_name.empty())) {
            BacktestConfig config = base_config;
            config.ticker = ticker.empty() ? "AAPL" : ticker;
            Backtester backtester(config);
            auto strategy = make_strategy(strategy_name);
            auto summary = backtester.run(*strategy);
            summaries.push_back(summary);
            print_summary(summary);
            Backtester::write_combined_summary("results/strategy_comparison.csv", summaries);
        } else if (mode == "compare") {
            std::vector<std::string> strategies = {"ma_cross", "rsi", "macd"};
            for (const auto& t : selected_tickers(ticker)) {
                for (const auto& s : strategies) {
                    BacktestConfig config = base_config;
                    config.ticker = t;
                    Backtester backtester(config);
                    auto strategy = make_strategy(s);
                    auto summary = backtester.run(*strategy);
                    summaries.push_back(summary);
                    print_summary(summary);
                }
            }
            Backtester::write_combined_summary("results/strategy_comparison.csv", summaries);
        } else if (mode == "grid") {
            summaries = Analysis::run_parameter_grid(base_config, selected_tickers(ticker));
        } else if (mode == "cross-asset") {
            summaries = Analysis::run_cross_asset(base_config);
        } else if (mode == "walk-forward") {
            Analysis::run_walk_forward(base_config, selected_tickers(ticker));
        } else if (mode == "cost") {
            Analysis::run_transaction_cost_sensitivity(base_config, selected_tickers(ticker));
        } else if (mode == "regime") {
            Analysis::run_regime_evaluation(base_config, selected_tickers(ticker));
        } else if (mode == "benchmark") {
            auto timings = Analysis::run_performance_benchmarks(base_config);
            for (const auto& timing : timings) {
                std::cout << timing.benchmark << ": " << timing.milliseconds << " ms\n";
            }
        } else if (mode == "all") {
            summaries = Analysis::run_cross_asset(base_config);
            Analysis::run_parameter_grid(base_config, Analysis::default_tickers());
            Analysis::run_walk_forward(base_config, Analysis::default_tickers());
            Analysis::run_transaction_cost_sensitivity(base_config, Analysis::default_tickers());
            Analysis::run_regime_evaluation(base_config, Analysis::default_tickers());
            auto timings = Analysis::run_performance_benchmarks(base_config);
            for (const auto& timing : timings) {
                std::cout << timing.benchmark << ": " << timing.milliseconds << " ms\n";
            }
        } else {
            throw std::runtime_error("Unknown mode: " + mode);
        }

        write_run_metadata(base_config, mode, ticker, strategy_name);
        for (const auto& summary : summaries) {
            print_summary(summary);
        }
        std::cout << "\nResults written to results/.\n";
    } catch (const std::exception& ex) {
        std::cerr << "Backtest failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
