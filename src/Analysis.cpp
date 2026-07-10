#include "Analysis.h"

#include "MarketData.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;

std::unique_ptr<Strategy> make_strategy(const std::string& name, const std::string& parameter_set) {
    if (name == "MA_Cross") {
        int short_window = 20;
        int long_window = 50;
        std::sscanf(parameter_set.c_str(), "short=%d;long=%d", &short_window, &long_window);
        return std::make_unique<MovingAverageCrossoverStrategy>(short_window, long_window);
    }
    if (name == "RSI_Mean_Reversion") {
        int period = 14;
        double oversold = 30.0;
        double overbought = 70.0;
        std::sscanf(parameter_set.c_str(), "period=%d;oversold=%lf;overbought=%lf", &period, &oversold, &overbought);
        return std::make_unique<RSIMeanReversionStrategy>(period, oversold, overbought);
    }
    if (name == "MACD_Momentum") {
        int fast = 12;
        int slow = 26;
        int signal = 9;
        std::sscanf(parameter_set.c_str(), "fast=%d;slow=%d;signal=%d", &fast, &slow, &signal);
        return std::make_unique<MACDMomentumStrategy>(fast, slow, signal);
    }
    throw std::runtime_error("Unknown strategy: " + name);
}

void write_summary_csv(const std::string& filepath, const std::vector<PerformanceSummary>& rows) {
    Backtester::write_combined_summary(filepath, rows);
}

MarketData load_market_data(const BacktestConfig& config, const std::string& ticker) {
    MarketData data;
    std::string path = config.data_dir + "/" + ticker + ".csv";
    if (!data.load_csv(ticker, path)) {
        throw std::runtime_error("Could not load " + path);
    }
    return data;
}

std::vector<std::string> half_year_windows(const std::vector<Bar>& bars) {
    std::vector<std::string> dates;
    for (std::size_t i = 0; i < bars.size(); i += 126) {
        dates.push_back(bars[i].date);
    }
    return dates;
}

double objective(const PerformanceSummary& summary) {
    return summary.sharpe - std::abs(summary.max_drawdown) * 0.25;
}

std::string regime_for(const std::vector<Bar>& bars, const std::vector<double>& returns, std::size_t index, double vol_median) {
    if (index < 200 || index < 60) {
        return "unclassified";
    }
    double sma200 = simple_moving_average(bars, index, 200);
    double ret60 = (bars[index].close / bars[index - 60].close) - 1.0;
    double vol60 = rolling_volatility(returns, index - 1, 60) * std::sqrt(252.0);
    if (vol60 > vol_median) {
        return "high_volatility";
    }
    if (bars[index].close > sma200 && ret60 > 0.0) {
        return "bull";
    }
    if (bars[index].close < sma200 && ret60 < 0.0) {
        return "bear";
    }
    return "sideways";
}

double mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double stdev(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double avg = mean(values);
    double sum = 0.0;
    for (double value : values) {
        double diff = value - avg;
        sum += diff * diff;
    }
    return std::sqrt(sum / (values.size() - 1));
}

void write_regime_row(std::ofstream& out, const std::string& regime, const std::string& ticker, const std::string& strategy,
                      double total_return, double sharpe, double max_drawdown, int trade_count, double win_rate) {
    out << regime << ',' << ticker << ',' << strategy << ','
        << total_return << ',' << sharpe << ',' << max_drawdown << ','
        << trade_count << ',' << win_rate << '\n';
}

std::vector<double> closes_from(const std::vector<Bar>& bars) {
    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& bar : bars) {
        closes.push_back(bar.close);
    }
    return closes;
}

std::vector<double> optimized_rolling_sma(const std::vector<double>& values, int window) {
    std::vector<double> out(values.size(), 0.0);
    if (window <= 0 || values.size() < static_cast<std::size_t>(window)) {
        return out;
    }
    double rolling = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        rolling += values[i];
        if (i >= static_cast<std::size_t>(window)) {
            rolling -= values[i - window];
        }
        if (i + 1 >= static_cast<std::size_t>(window)) {
            out[i] = rolling / window;
        }
    }
    return out;
}
}

std::vector<std::string> Analysis::default_tickers() {
    return {"AAPL", "MSFT", "SPY", "TSLA", "BTC-USD"};
}

std::vector<StrategySpec> Analysis::default_strategy_specs() {
    std::vector<StrategySpec> specs;
    specs.push_back({"MA_Cross", "short=20;long=50", std::make_unique<MovingAverageCrossoverStrategy>(20, 50)});
    specs.push_back({"RSI_Mean_Reversion", "period=14;oversold=30;overbought=70", std::make_unique<RSIMeanReversionStrategy>(14, 30, 70)});
    specs.push_back({"MACD_Momentum", "fast=12;slow=26;signal=9", std::make_unique<MACDMomentumStrategy>(12, 26, 9)});
    return specs;
}

std::vector<StrategySpec> Analysis::grid_strategy_specs() {
    std::vector<StrategySpec> specs;
    for (int short_window : {5, 10, 20}) {
        for (int long_window : {50, 100, 200}) {
            if (short_window >= long_window) {
                continue;
            }
            specs.push_back({"MA_Cross", "short=" + std::to_string(short_window) + ";long=" + std::to_string(long_window),
                std::make_unique<MovingAverageCrossoverStrategy>(short_window, long_window)});
        }
    }
    for (int period : {7, 14, 21}) {
        for (const auto& thresholds : std::vector<std::pair<int, int>>{{20, 80}, {25, 75}, {30, 70}}) {
            specs.push_back({"RSI_Mean_Reversion",
                "period=" + std::to_string(period) + ";oversold=" + std::to_string(thresholds.first) + ";overbought=" + std::to_string(thresholds.second),
                std::make_unique<RSIMeanReversionStrategy>(period, thresholds.first, thresholds.second)});
        }
    }
    for (const auto& params : std::vector<std::tuple<int, int, int>>{{8, 17, 9}, {12, 26, 9}, {19, 39, 9}, {5, 35, 5}}) {
        int fast = std::get<0>(params);
        int slow = std::get<1>(params);
        int signal = std::get<2>(params);
        if (fast >= slow || signal <= 0) {
            continue;
        }
        specs.push_back({"MACD_Momentum",
            "fast=" + std::to_string(fast) + ";slow=" + std::to_string(slow) + ";signal=" + std::to_string(signal),
            std::make_unique<MACDMomentumStrategy>(fast, slow, signal)});
    }
    return specs;
}

std::vector<PerformanceSummary> Analysis::run_cross_asset(const BacktestConfig& base_config) {
    std::vector<PerformanceSummary> summaries;
    for (const auto& ticker : default_tickers()) {
        for (const auto& spec : default_strategy_specs()) {
            BacktestConfig config = base_config;
            config.ticker = ticker;
            Backtester tester(config);
            summaries.push_back(tester.run_detailed(*spec.instance, true).summary);
        }
    }
    write_summary_csv(base_config.results_dir + "/cross_asset_comparison.csv", summaries);
    return summaries;
}

std::vector<PerformanceSummary> Analysis::run_parameter_grid(const BacktestConfig& base_config, const std::vector<std::string>& tickers) {
    std::vector<PerformanceSummary> summaries;
    for (const auto& ticker : tickers) {
        for (const auto& spec : grid_strategy_specs()) {
            BacktestConfig config = base_config;
            config.ticker = ticker;
            Backtester tester(config);
            summaries.push_back(tester.run_detailed(*spec.instance, false).summary);
        }
    }
    write_summary_csv(base_config.results_dir + "/parameter_grid_results.csv", summaries);
    return summaries;
}

void Analysis::run_walk_forward(const BacktestConfig& base_config, const std::vector<std::string>& tickers) {
    std::ofstream windows(base_config.results_dir + "/walk_forward_windows.csv");
    std::ofstream equity_out(base_config.results_dir + "/walk_forward_equity_curve.csv");
    std::ofstream history_out(base_config.results_dir + "/parameter_selection_history.csv");
    windows << std::fixed << std::setprecision(6);
    equity_out << std::fixed << std::setprecision(6);
    history_out << std::fixed << std::setprecision(6);
    windows << "ticker,strategy,train_start,train_end,test_start,test_end,parameter_set,in_sample_sharpe,in_sample_total_return,out_of_sample_total_return,out_of_sample_sharpe,out_of_sample_max_drawdown,out_of_sample_trades\n";
    equity_out << "ticker,strategy,parameter_set,window_id,date,portfolio_value,cash,holdings,total_return,drawdown\n";
    history_out << "ticker,strategy,window_id,train_start,train_end,selected_parameter_set,objective,train_sharpe,train_max_drawdown\n";

    int window_id = 0;
    for (const auto& ticker : tickers) {
        MarketData data = load_market_data(base_config, ticker);
        const auto& bars = data.bars(ticker);
        if (bars.size() < 756 + 126) {
            continue;
        }
        for (std::size_t train_start = 0; train_start + 756 + 126 <= bars.size(); train_start += 126) {
            std::size_t train_end = train_start + 755;
            std::size_t test_start = train_end + 1;
            std::size_t test_end = std::min(test_start + 125, bars.size() - 1);

            std::map<std::string, PerformanceSummary> best_by_strategy;
            for (const auto& spec : grid_strategy_specs()) {
                BacktestConfig train_config = base_config;
                train_config.ticker = ticker;
                train_config.start_date = bars[train_start].date;
                train_config.end_date = bars[train_end].date;
                Backtester train_tester(train_config);
                PerformanceSummary train_summary = train_tester.run_detailed(*spec.instance, false).summary;
                if (!best_by_strategy.count(spec.strategy) || objective(train_summary) > objective(best_by_strategy[spec.strategy])) {
                    best_by_strategy[spec.strategy] = train_summary;
                }
            }

            for (const auto& selected : best_by_strategy) {
                auto frozen = make_strategy(selected.second.strategy, selected.second.parameter_set);
                BacktestConfig test_config = base_config;
                test_config.ticker = ticker;
                test_config.start_date = bars[test_start].date;
                test_config.end_date = bars[test_end].date;
                Backtester test_tester(test_config);
                BacktestResult out_sample = test_tester.run_detailed(*frozen, false);

                windows << ticker << ',' << selected.second.strategy << ','
                    << bars[train_start].date << ',' << bars[train_end].date << ','
                    << bars[test_start].date << ',' << bars[test_end].date << ','
                    << selected.second.parameter_set << ','
                    << selected.second.sharpe << ',' << selected.second.total_return << ','
                    << out_sample.summary.total_return << ',' << out_sample.summary.sharpe << ','
                    << out_sample.summary.max_drawdown << ',' << out_sample.summary.num_trades << '\n';

                history_out << ticker << ',' << selected.second.strategy << ',' << window_id << ','
                    << bars[train_start].date << ',' << bars[train_end].date << ','
                    << selected.second.parameter_set << ',' << objective(selected.second) << ','
                    << selected.second.sharpe << ',' << selected.second.max_drawdown << '\n';

                for (const auto& point : out_sample.equity_curve) {
                    equity_out << ticker << ',' << selected.second.strategy << ',' << selected.second.parameter_set << ','
                        << window_id << ',' << point.date << ',' << point.portfolio_value << ','
                        << point.cash << ',' << point.holdings << ',' << point.total_return << ',' << point.drawdown << '\n';
                }
                ++window_id;
            }
        }
    }
}

void Analysis::run_transaction_cost_sensitivity(const BacktestConfig& base_config, const std::vector<std::string>& tickers) {
    std::ofstream out(base_config.results_dir + "/transaction_cost_sensitivity.csv");
    out << std::fixed << std::setprecision(6);
    out << "ticker,strategy,parameter_set,commission_bps,slippage_bps,net_return,sharpe,turnover,total_costs,degradation_vs_zero_cost,estimated_break_even_cost_bps\n";

    for (const auto& ticker : tickers) {
        for (const auto& spec : default_strategy_specs()) {
            BacktestConfig zero = base_config;
            zero.ticker = ticker;
            zero.transaction_cost_rate = 0.0;
            zero.slippage_rate = 0.0;
            double zero_return = Backtester(zero).run_detailed(*spec.instance, false).summary.total_return;
            for (int commission_bps : {0, 5, 10, 20}) {
                for (int slippage_bps : {0, 5, 10, 25}) {
                    BacktestConfig config = base_config;
                    config.ticker = ticker;
                    config.transaction_cost_rate = commission_bps / 10000.0;
                    config.slippage_rate = slippage_bps / 10000.0;
                    PerformanceSummary summary = Backtester(config).run_detailed(*spec.instance, false).summary;
                    double total_bps = commission_bps + slippage_bps;
                    double degradation = zero_return - summary.total_return;
                    double break_even = degradation > 0.0 && total_bps > 0.0 ? total_bps * (zero_return / degradation) : 0.0;
                    out << ticker << ',' << summary.strategy << ',' << summary.parameter_set << ','
                        << commission_bps << ',' << slippage_bps << ','
                        << summary.total_return << ',' << summary.sharpe << ',' << summary.turnover << ','
                        << summary.total_transaction_costs << ',' << degradation << ',' << break_even << '\n';
                }
            }
        }
    }
}

void Analysis::run_regime_evaluation(const BacktestConfig& base_config, const std::vector<std::string>& tickers) {
    std::ofstream out(base_config.results_dir + "/regime_evaluation.csv");
    out << std::fixed << std::setprecision(6);
    out << "regime,ticker,strategy,return,sharpe,drawdown,trade_count,win_rate\n";

    for (const auto& ticker : tickers) {
        MarketData data = load_market_data(base_config, ticker);
        const auto& bars = data.bars(ticker);
        const auto closes = closes_from(bars);
        const auto returns = daily_returns(closes);
        std::vector<double> vol_samples;
        for (std::size_t i = 60; i < returns.size(); ++i) {
            vol_samples.push_back(rolling_volatility(returns, i, 60) * std::sqrt(252.0));
        }
        std::sort(vol_samples.begin(), vol_samples.end());
        double median_vol = vol_samples.empty() ? 0.0 : vol_samples[vol_samples.size() / 2];

        std::map<std::string, std::string> date_regime;
        for (std::size_t i = 0; i < bars.size(); ++i) {
            date_regime[bars[i].date] = regime_for(bars, returns, i, median_vol);
        }

        for (const auto& spec : default_strategy_specs()) {
            BacktestConfig config = base_config;
            config.ticker = ticker;
            BacktestResult result = Backtester(config).run_detailed(*spec.instance, false);

            for (const auto& regime : std::vector<std::string>{"bull", "bear", "sideways", "high_volatility"}) {
                std::vector<double> regime_returns;
                double peak = 1.0;
                double equity = 1.0;
                double max_drawdown = 0.0;
                for (std::size_t i = 1; i < result.equity_curve.size(); ++i) {
                    const auto& point = result.equity_curve[i];
                    if (date_regime[point.date] != regime) {
                        continue;
                    }
                    double prev_value = result.equity_curve[i - 1].portfolio_value;
                    double ret = prev_value > 0.0 ? (point.portfolio_value / prev_value) - 1.0 : 0.0;
                    regime_returns.push_back(ret);
                    equity *= 1.0 + ret;
                    peak = std::max(peak, equity);
                    max_drawdown = std::min(max_drawdown, (equity / peak) - 1.0);
                }
                int trade_count = 0;
                int winners = 0;
                int closed = 0;
                for (const auto& trade : result.trades) {
                    if (date_regime[trade.date] != regime) {
                        continue;
                    }
                    ++trade_count;
                    if (trade.action == "SELL") {
                        ++closed;
                        if (trade.realized_pnl > 0.0) {
                            ++winners;
                        }
                    }
                }
                double sharpe = stdev(regime_returns) > 0.0 ? (mean(regime_returns) * 252.0) / (stdev(regime_returns) * std::sqrt(252.0)) : 0.0;
                double win_rate = closed > 0 ? static_cast<double>(winners) / closed : 0.0;
                write_regime_row(out, regime, ticker, spec.strategy, equity - 1.0, sharpe, max_drawdown, trade_count, win_rate);
            }
        }
    }
}

std::vector<BenchmarkTiming> Analysis::run_performance_benchmarks(const BacktestConfig& base_config) {
    std::vector<BenchmarkTiming> timings;
    MarketData data = load_market_data(base_config, "AAPL");
    const auto& bars = data.bars("AAPL");
    const auto closes = closes_from(bars);

    auto measure = [&](const std::string& name, auto fn) {
        auto start = Clock::now();
        std::size_t observations = fn();
        auto end = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        timings.push_back({name, ms, observations});
    };

    measure("naive_rolling_sma", [&]() {
        double sink = 0.0;
        for (std::size_t i = 0; i < bars.size(); ++i) {
            sink += simple_moving_average(bars, i, 50);
        }
        return static_cast<std::size_t>(sink > 0.0 ? bars.size() : 0);
    });

    measure("optimized_rolling_sma", [&]() {
        auto values = optimized_rolling_sma(closes, 50);
        return values.size();
    });

    measure("single_backtest", [&]() {
        BacktestConfig config = base_config;
        config.ticker = "AAPL";
        auto strategy = MovingAverageCrossoverStrategy(20, 50);
        Backtester(config).run_detailed(strategy, false);
        return bars.size();
    });

    measure("full_parameter_sweep_AAPL", [&]() {
        BacktestConfig config = base_config;
        config.ticker = "AAPL";
        std::size_t runs = 0;
        for (const auto& spec : grid_strategy_specs()) {
            Backtester(config).run_detailed(*spec.instance, false);
            ++runs;
        }
        return runs;
    });

    write_benchmark_timings(base_config.results_dir + "/benchmark_timings.csv", timings);
    return timings;
}

void Analysis::write_benchmark_timings(const std::string& filepath, const std::vector<BenchmarkTiming>& timings) {
    std::ofstream out(filepath);
    out << std::fixed << std::setprecision(6);
    out << "benchmark,milliseconds,observations\n";
    for (const auto& timing : timings) {
        out << timing.benchmark << ',' << timing.milliseconds << ',' << timing.observations << '\n';
    }
}
