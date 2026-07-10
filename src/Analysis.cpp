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
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;

double mean(const std::vector<double>& values);
double stdev(const std::vector<double>& values);

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
    if (name == "Volatility_Breakout") {
        int lookback = 20;
        double multiplier = 1.5;
        std::sscanf(parameter_set.c_str(), "lookback=%d;multiplier=%lf", &lookback, &multiplier);
        return std::make_unique<VolatilityBreakoutStrategy>(lookback, multiplier);
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

double sortino_from_equity(const std::vector<EquityPoint>& equity) {
    std::vector<double> downside;
    std::vector<double> returns;
    for (std::size_t i = 1; i < equity.size(); ++i) {
        double prev = equity[i - 1].portfolio_value;
        if (prev <= 0.0) {
            continue;
        }
        double ret = (equity[i].portfolio_value / prev) - 1.0;
        returns.push_back(ret);
        if (ret < 0.0) {
            downside.push_back(ret);
        }
    }
    double avg = mean(returns) * 252.0;
    double down_dev = stdev(downside) * std::sqrt(252.0);
    return down_dev > 0.0 ? avg / down_dev : 0.0;
}

double calmar_from_summary(const PerformanceSummary& summary) {
    return summary.max_drawdown < 0.0 ? summary.annualized_return / std::abs(summary.max_drawdown) : 0.0;
}

double objective(const PerformanceSummary& summary, const std::string& objective_name, int minimum_trades) {
    if (summary.num_trades < minimum_trades) {
        return -1e9;
    }
    if (objective_name == "calmar") {
        return calmar_from_summary(summary);
    }
    if (objective_name == "excess_return") {
        return summary.excess_return;
    }
    if (objective_name == "sharpe_maxdd") {
        return summary.max_drawdown > -0.35 ? summary.sharpe : -1e9;
    }
    return summary.sharpe;
}

double objective(const PerformanceSummary& summary) {
    return objective(summary, "sharpe_min_trades", 3);
}

std::string regime_for(const std::vector<Bar>& bars, const std::vector<double>& returns, std::size_t index, double vol_median) {
    if (index < 200 || index < 60) {
        return "unclassified";
    }
    double sma200 = simple_moving_average(bars, index, 200);
    double ret60 = (bars[index].close / bars[index - 60].close) - 1.0;
    double vol20 = rolling_volatility(returns, index - 1, 20) * std::sqrt(252.0);
    std::string vol_state = vol20 > vol_median ? "high_volatility" : "low_volatility";
    if (bars[index].close > sma200 && ret60 > 0.0) {
        return "bull/" + vol_state;
    }
    if (bars[index].close < sma200 && ret60 < 0.0) {
        return "bear/" + vol_state;
    }
    return "sideways/" + vol_state;
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
                      double total_return, double sharpe, double max_drawdown, int trade_count, double win_rate,
                      double profit_factor, double benchmark_relative_performance, double average_exposure) {
    out << regime << ',' << ticker << ',' << strategy << ','
        << total_return << ',' << sharpe << ',' << max_drawdown << ','
        << trade_count << ',' << win_rate << ',' << profit_factor << ','
        << benchmark_relative_performance << ',' << average_exposure << '\n';
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

std::string read_file(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        throw std::runtime_error("Could not open config: " + filepath);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string json_string(const std::string& text, const std::string& key, const std::string& fallback) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    return std::regex_search(text, match, pattern) ? match[1].str() : fallback;
}

double json_number(const std::string& text, const std::string& key, double fallback) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    return std::regex_search(text, match, pattern) ? std::stod(match[1].str()) : fallback;
}

std::vector<std::string> json_string_array(const std::string& text, const std::string& key, const std::vector<std::string>& fallback) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return fallback;
    }
    std::vector<std::string> out;
    std::regex item("\"([^\"]*)\"");
    std::string body = match[1].str();
    for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it) {
        out.push_back((*it)[1].str());
    }
    return out.empty() ? fallback : out;
}

std::vector<double> returns_from_equity(const std::vector<EquityPoint>& equity) {
    std::vector<double> returns;
    for (std::size_t i = 1; i < equity.size(); ++i) {
        double prev = equity[i - 1].portfolio_value;
        if (prev > 0.0) {
            returns.push_back((equity[i].portfolio_value / prev) - 1.0);
        }
    }
    return returns;
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    std::size_t index = static_cast<std::size_t>(std::min<double>(values.size() - 1, std::floor(p * (values.size() - 1))));
    return values[index];
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
    for (const auto& name : std::vector<std::string>{"MA_Cross", "RSI_Mean_Reversion", "MACD_Momentum", "Volatility_Breakout"}) {
        auto partial = grid_strategy_specs(name);
        for (auto& spec : partial) {
            specs.push_back(std::move(spec));
        }
    }
    return specs;
}

std::vector<StrategySpec> Analysis::grid_strategy_specs(const std::string& strategy_name) {
    std::vector<StrategySpec> specs;
    if (strategy_name == "MA_Cross" || strategy_name == "ma_cross") {
    for (int short_window : {5, 10, 20, 30}) {
        for (int long_window : {50, 100, 150, 200}) {
            if (short_window >= long_window) {
                continue;
            }
            specs.push_back({"MA_Cross", "short=" + std::to_string(short_window) + ";long=" + std::to_string(long_window),
                std::make_unique<MovingAverageCrossoverStrategy>(short_window, long_window)});
        }
    }
    }
    if (strategy_name == "RSI_Mean_Reversion" || strategy_name == "rsi") {
    for (int period : {7, 14, 21}) {
        for (const auto& thresholds : std::vector<std::pair<int, int>>{{20, 80}, {25, 75}, {30, 70}, {35, 65}}) {
            specs.push_back({"RSI_Mean_Reversion",
                "period=" + std::to_string(period) + ";oversold=" + std::to_string(thresholds.first) + ";overbought=" + std::to_string(thresholds.second),
                std::make_unique<RSIMeanReversionStrategy>(period, thresholds.first, thresholds.second)});
        }
    }
    }
    if (strategy_name == "MACD_Momentum" || strategy_name == "macd") {
    for (const auto& params : std::vector<std::tuple<int, int, int>>{{8, 21, 5}, {12, 26, 9}, {16, 32, 9}, {20, 40, 10}}) {
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
    }
    if (strategy_name == "Volatility_Breakout" || strategy_name == "volatility_breakout") {
        for (int lookback : {10, 20, 40}) {
            for (double multiplier : {1.0, 1.5, 2.0}) {
                std::ostringstream p;
                p << "lookback=" << lookback << ";multiplier=" << multiplier;
                specs.push_back({"Volatility_Breakout", p.str(),
                    std::make_unique<VolatilityBreakoutStrategy>(lookback, multiplier)});
            }
        }
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
    write_summary_csv(base_config.results_dir + "/strategy_comparison.csv", summaries);
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
    std::ofstream surface(base_config.results_dir + "/transaction_cost_surface.csv");
    std::ofstream break_even_out(base_config.results_dir + "/break_even_costs.csv");
    out << std::fixed << std::setprecision(6);
    surface << std::fixed << std::setprecision(6);
    break_even_out << std::fixed << std::setprecision(6);
    const std::string header = "ticker,strategy,parameter_set,commission_bps,slippage_bps,net_return,sharpe,turnover,total_costs,degradation_vs_zero_cost,estimated_break_even_cost_bps\n";
    out << header;
    surface << header;
    break_even_out << "ticker,strategy,parameter_set,estimated_break_even_all_in_bps,method\n";

    for (const auto& ticker : tickers) {
        for (const auto& spec : default_strategy_specs()) {
            BacktestConfig zero = base_config;
            zero.ticker = ticker;
            zero.transaction_cost_rate = 0.0;
            zero.slippage_rate = 0.0;
            double zero_return = Backtester(zero).run_detailed(*spec.instance, false).summary.total_return;
            double best_break_even = 0.0;
            for (int commission_bps : {0, 5, 10, 20, 40}) {
                for (int slippage_bps : {0, 5, 10, 25, 50}) {
                    BacktestConfig config = base_config;
                    config.ticker = ticker;
                    config.transaction_cost_rate = commission_bps / 10000.0;
                    config.slippage_rate = slippage_bps / 10000.0;
                    PerformanceSummary summary = Backtester(config).run_detailed(*spec.instance, false).summary;
                    double total_bps = commission_bps + slippage_bps;
                    double degradation = zero_return - summary.total_return;
                    double break_even = degradation > 0.0 && total_bps > 0.0 ? total_bps * (zero_return / degradation) : 0.0;
                    if (break_even > 0.0) {
                        best_break_even = best_break_even == 0.0 ? break_even : std::min(best_break_even, break_even);
                    }
                    std::ostringstream row;
                    row << std::fixed << std::setprecision(6);
                    row << ticker << ',' << summary.strategy << ',' << summary.parameter_set << ','
                        << commission_bps << ',' << slippage_bps << ','
                        << summary.total_return << ',' << summary.sharpe << ',' << summary.turnover << ','
                        << summary.total_transaction_costs << ',' << degradation << ',' << break_even << '\n';
                    out << row.str();
                    surface << row.str();
                }
            }
            break_even_out << ticker << ',' << spec.strategy << ',' << spec.parameter_set << ',';
            if (best_break_even > 0.0) {
                break_even_out << best_break_even << ",linear_cost_grid_interpolation\n";
            } else {
                break_even_out << "not_estimated,zero_or_negative_zero_cost_return\n";
            }
        }
    }
}

void Analysis::run_regime_evaluation(const BacktestConfig& base_config, const std::vector<std::string>& tickers) {
    std::ofstream out(base_config.results_dir + "/regime_evaluation.csv");
    std::ofstream performance(base_config.results_dir + "/strategy_regime_performance.csv");
    std::ofstream assignments(base_config.results_dir + "/regime_assignments.csv");
    out << std::fixed << std::setprecision(6);
    performance << std::fixed << std::setprecision(6);
    assignments << std::fixed << std::setprecision(6);
    const std::string header = "regime,ticker,strategy,return,sharpe,drawdown,trade_count,win_rate,profit_factor,benchmark_relative_performance,average_exposure\n";
    out << header;
    performance << header;
    assignments << "date,ticker,regime\n";
    const std::vector<std::string> regimes = {
        "bull/low_volatility", "bull/high_volatility",
        "bear/low_volatility", "bear/high_volatility",
        "sideways/low_volatility", "sideways/high_volatility"
    };

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
            assignments << bars[i].date << ',' << ticker << ',' << date_regime[bars[i].date] << '\n';
        }

        for (const auto& spec : default_strategy_specs()) {
            BacktestConfig config = base_config;
            config.ticker = ticker;
            BacktestResult result = Backtester(config).run_detailed(*spec.instance, false);

            for (const auto& regime : regimes) {
                std::vector<double> regime_returns;
                std::vector<double> benchmark_returns;
                double peak = 1.0;
                double equity = 1.0;
                double benchmark_equity = 1.0;
                double max_drawdown = 0.0;
                int exposure_days = 0;
                int eligible_days = 0;
                for (std::size_t i = 1; i < result.equity_curve.size(); ++i) {
                    const auto& point = result.equity_curve[i];
                    if (date_regime[point.date] != regime) {
                        continue;
                    }
                    double prev_value = result.equity_curve[i - 1].portfolio_value;
                    double ret = prev_value > 0.0 ? (point.portfolio_value / prev_value) - 1.0 : 0.0;
                    regime_returns.push_back(ret);
                    equity *= 1.0 + ret;
                    auto bar_it = std::find_if(bars.begin(), bars.end(), [&](const Bar& bar) { return bar.date == point.date; });
                    if (bar_it != bars.end() && bar_it != bars.begin()) {
                        const auto& prev_bar = *(bar_it - 1);
                        double benchmark_ret = prev_bar.close > 0.0 ? (bar_it->close / prev_bar.close) - 1.0 : 0.0;
                        benchmark_returns.push_back(benchmark_ret);
                        benchmark_equity *= 1.0 + benchmark_ret;
                    }
                    peak = std::max(peak, equity);
                    max_drawdown = std::min(max_drawdown, (equity / peak) - 1.0);
                    ++eligible_days;
                    if (point.holdings > 0.0) {
                        ++exposure_days;
                    }
                }
                int trade_count = 0;
                int winners = 0;
                int closed = 0;
                double gross_profit = 0.0;
                double gross_loss = 0.0;
                for (const auto& trade : result.trades) {
                    if (date_regime[trade.date] != regime) {
                        continue;
                    }
                    ++trade_count;
                    if (trade.action == "SELL") {
                        ++closed;
                        if (trade.realized_pnl > 0.0) {
                            ++winners;
                            gross_profit += trade.realized_pnl;
                        } else {
                            gross_loss += std::abs(trade.realized_pnl);
                        }
                    }
                }
                double sharpe = stdev(regime_returns) > 0.0 ? (mean(regime_returns) * 252.0) / (stdev(regime_returns) * std::sqrt(252.0)) : 0.0;
                double win_rate = closed > 0 ? static_cast<double>(winners) / closed : 0.0;
                double profit_factor = gross_loss > 0.0 ? gross_profit / gross_loss : (gross_profit > 0.0 ? gross_profit : 0.0);
                double benchmark_relative = (equity - 1.0) - (benchmark_equity - 1.0);
                double average_exposure = eligible_days > 0 ? static_cast<double>(exposure_days) / eligible_days : 0.0;
                write_regime_row(out, regime, ticker, spec.strategy, equity - 1.0, sharpe, max_drawdown, trade_count, win_rate,
                                 profit_factor, benchmark_relative, average_exposure);
                write_regime_row(performance, regime, ticker, spec.strategy, equity - 1.0, sharpe, max_drawdown, trade_count, win_rate,
                                 profit_factor, benchmark_relative, average_exposure);
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
        for (int repeat = 0; repeat < 250; ++repeat) {
            for (std::size_t i = 0; i < bars.size(); ++i) {
                sink += simple_moving_average(bars, i, 50);
            }
        }
        return static_cast<std::size_t>(sink > 0.0 ? bars.size() * 250 : 0);
    });

    measure("optimized_rolling_sma", [&]() {
        std::size_t observations = 0;
        volatile double checksum = 0.0;
        for (int repeat = 0; repeat < 250; ++repeat) {
            auto values = optimized_rolling_sma(closes, 50);
            checksum += values.empty() ? 0.0 : values.back();
            observations += values.size();
        }
        return checksum > 0.0 ? observations : 0;
    });

    measure("single_backtest", [&]() {
        BacktestConfig config = base_config;
        config.ticker = "AAPL";
        auto strategy = MovingAverageCrossoverStrategy(20, 50);
        for (int repeat = 0; repeat < 10; ++repeat) {
            Backtester(config).run_detailed(strategy, false);
        }
        return bars.size() * 10;
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

ExperimentConfig Analysis::load_experiment_config(const std::string& filepath) {
    std::string text = read_file(filepath);
    ExperimentConfig config;
    config.experiment_name = json_string(text, "experiment_name", config.experiment_name);
    config.tickers = json_string_array(text, "ticker_universe", default_tickers());
    config.strategy = json_string(text, "strategy", config.strategy);
    config.starting_capital = json_number(text, "starting_capital", config.starting_capital);
    config.commission_bps = json_number(text, "commission_bps", config.commission_bps);
    config.slippage_bps = json_number(text, "slippage_bps", config.slippage_bps);
    config.train_days = static_cast<int>(json_number(text, "train_window_days", config.train_days));
    config.test_days = static_cast<int>(json_number(text, "test_window_days", config.test_days));
    config.step_days = static_cast<int>(json_number(text, "step_days", config.step_days));
    config.benchmark = json_string(text, "benchmark", config.benchmark);
    config.objective = json_string(text, "parameter_selection_objective", config.objective);
    config.minimum_trades = static_cast<int>(json_number(text, "minimum_trade_requirement", config.minimum_trades));
    config.regime_method = json_string(text, "regime_classification_method", config.regime_method);
    config.random_seed = static_cast<unsigned int>(json_number(text, "random_seed", config.random_seed));
    config.output_dir = json_string(text, "output_directory", "results/research/" + config.experiment_name);
    config.allocation_policy = json_string(text, "allocation_policy", config.allocation_policy);
    config.rebalance_frequency = json_string(text, "rebalance_frequency", config.rebalance_frequency);
    config.max_weight = json_number(text, "max_weight", config.max_weight);
    config.cash_buffer = json_number(text, "cash_buffer", config.cash_buffer);
    config.min_trade_value = json_number(text, "min_trade_value", config.min_trade_value);
    config.volatility_lookback = static_cast<int>(json_number(text, "volatility_lookback", config.volatility_lookback));
    config.momentum_lookback = static_cast<int>(json_number(text, "momentum_lookback", config.momentum_lookback));
    config.top_n = static_cast<int>(json_number(text, "top_n", config.top_n));
    config.portfolio_output_dir = json_string(text, "portfolio_output_directory", config.portfolio_output_dir);
    return config;
}

void Analysis::run_research_experiment(const ExperimentConfig& experiment) {
    std::filesystem::create_directories(experiment.output_dir);
    std::ofstream resolved(experiment.output_dir + "/resolved_config.json");
    resolved << "{\n"
             << "  \"experiment_name\": \"" << experiment.experiment_name << "\",\n"
             << "  \"strategy\": \"" << experiment.strategy << "\",\n"
             << "  \"starting_capital\": " << experiment.starting_capital << ",\n"
             << "  \"commission_bps\": " << experiment.commission_bps << ",\n"
             << "  \"slippage_bps\": " << experiment.slippage_bps << ",\n"
             << "  \"train_window_days\": " << experiment.train_days << ",\n"
             << "  \"test_window_days\": " << experiment.test_days << ",\n"
             << "  \"step_days\": " << experiment.step_days << ",\n"
             << "  \"benchmark\": \"" << experiment.benchmark << "\",\n"
             << "  \"parameter_selection_objective\": \"" << experiment.objective << "\",\n"
             << "  \"minimum_trade_requirement\": " << experiment.minimum_trades << ",\n"
             << "  \"random_seed\": " << experiment.random_seed << "\n"
             << "}\n";

    BacktestConfig base;
    base.starting_capital = experiment.starting_capital;
    base.transaction_cost_rate = experiment.commission_bps / 10000.0;
    base.slippage_rate = experiment.slippage_bps / 10000.0;
    base.results_dir = experiment.output_dir;

    std::ofstream grid_out(experiment.output_dir + "/in_sample_full_grid.csv");
    grid_out << std::fixed << std::setprecision(6);
    grid_out << "ticker,strategy,parameter_set,start_date,end_date,total_return,benchmark_net_return,excess_return,sharpe,sortino,max_drawdown,calmar,turnover,total_costs,trade_count,win_rate,profit_factor\n";
    for (const auto& ticker : experiment.tickers) {
        for (const auto& spec : grid_strategy_specs(experiment.strategy)) {
            BacktestConfig cfg = base;
            cfg.ticker = ticker;
            BacktestResult result = Backtester(cfg).run_detailed(*spec.instance, false);
            grid_out << ticker << ',' << result.summary.strategy << ',' << result.summary.parameter_set << ",full,full,"
                     << result.summary.total_return << ',' << result.summary.benchmark_net_return << ',' << result.summary.excess_return << ','
                     << result.summary.sharpe << ',' << sortino_from_equity(result.equity_curve) << ','
                     << result.summary.max_drawdown << ',' << calmar_from_summary(result.summary) << ','
                     << result.summary.turnover << ',' << result.summary.total_transaction_costs << ','
                     << result.summary.num_trades << ',' << result.summary.win_rate << ',' << result.summary.profit_factor << '\n';
        }
    }

    std::filesystem::create_directories(experiment.output_dir + "/walk_forward");
    std::ofstream windows(experiment.output_dir + "/walk_forward/windows.csv");
    std::ofstream history(experiment.output_dir + "/walk_forward/parameter_history.csv");
    std::ofstream insample(experiment.output_dir + "/walk_forward/in_sample_results.csv");
    std::ofstream oos(experiment.output_dir + "/walk_forward/out_of_sample_results.csv");
    std::ofstream oos_equity(experiment.output_dir + "/walk_forward/oos_equity_curve.csv");
    std::ofstream oos_trades(experiment.output_dir + "/walk_forward/oos_trades.csv");
    std::ofstream summary(experiment.output_dir + "/walk_forward/summary.csv");
    windows << std::fixed << std::setprecision(6);
    history << std::fixed << std::setprecision(6);
    insample << std::fixed << std::setprecision(6);
    oos << std::fixed << std::setprecision(6);
    oos_equity << std::fixed << std::setprecision(6);
    oos_trades << std::fixed << std::setprecision(6);
    summary << std::fixed << std::setprecision(6);
    windows << "ticker,strategy,window_id,train_start,train_end,test_start,test_end,selected_parameters,objective_value,parameter_changed\n";
    history << "ticker,strategy,window_id,selected_parameters,objective_value,in_sample_sharpe,in_sample_return\n";
    insample << "ticker,strategy,window_id,parameter_set,total_return,sharpe,max_drawdown,trade_count\n";
    oos << "ticker,strategy,window_id,parameter_set,total_return,benchmark_net_return,excess_return,sharpe,max_drawdown,trade_count,total_costs\n";
    oos_equity << "ticker,strategy,window_id,date,portfolio_value,cash,holdings,total_return,drawdown\n";
    oos_trades << "ticker,strategy,window_id,date,action,price,quantity,cost,slippage,portfolio_value,realized_pnl\n";
    summary << "ticker,strategy,windows,positive_oos_excess_rate,benchmark_win_rate,parameter_changes,worst_oos_return\n";

    for (const auto& ticker : experiment.tickers) {
        MarketData data = load_market_data(base, ticker);
        const auto& bars = data.bars(ticker);
        if (bars.size() < static_cast<std::size_t>(experiment.train_days + experiment.test_days)) {
            continue;
        }
        int window_id = 0;
        int positive_excess = 0;
        int beats_benchmark = 0;
        int parameter_changes = 0;
        double worst_return = 1e9;
        std::string previous_params;
        for (std::size_t train_start = 0; train_start + experiment.train_days + experiment.test_days <= bars.size(); train_start += experiment.step_days) {
            std::size_t train_end = train_start + experiment.train_days - 1;
            std::size_t test_start = train_end + 1;
            std::size_t test_end = std::min(test_start + experiment.test_days - 1, bars.size() - 1);
            PerformanceSummary best;
            double best_score = -1e18;
            for (const auto& spec : grid_strategy_specs(experiment.strategy)) {
                BacktestConfig train = base;
                train.ticker = ticker;
                train.start_date = bars[train_start].date;
                train.end_date = bars[train_end].date;
                BacktestResult r = Backtester(train).run_detailed(*spec.instance, false);
                double score = objective(r.summary, experiment.objective, experiment.minimum_trades);
                insample << ticker << ',' << r.summary.strategy << ',' << window_id << ',' << r.summary.parameter_set << ','
                         << r.summary.total_return << ',' << r.summary.sharpe << ',' << r.summary.max_drawdown << ',' << r.summary.num_trades << '\n';
                if (score > best_score) {
                    best_score = score;
                    best = r.summary;
                }
            }
            bool changed = !previous_params.empty() && previous_params != best.parameter_set;
            parameter_changes += changed ? 1 : 0;
            previous_params = best.parameter_set;
            auto frozen = make_strategy(best.strategy, best.parameter_set);
            BacktestConfig test = base;
            test.ticker = ticker;
            test.start_date = bars[test_start].date;
            test.end_date = bars[test_end].date;
            BacktestResult out = Backtester(test).run_detailed(*frozen, false);
            positive_excess += out.summary.excess_return > 0.0 ? 1 : 0;
            beats_benchmark += out.summary.total_return > out.summary.benchmark_net_return ? 1 : 0;
            worst_return = std::min(worst_return, out.summary.total_return);
            windows << ticker << ',' << best.strategy << ',' << window_id << ',' << bars[train_start].date << ',' << bars[train_end].date << ','
                    << bars[test_start].date << ',' << bars[test_end].date << ',' << best.parameter_set << ',' << best_score << ',' << (changed ? 1 : 0) << '\n';
            history << ticker << ',' << best.strategy << ',' << window_id << ',' << best.parameter_set << ',' << best_score << ',' << best.sharpe << ',' << best.total_return << '\n';
            oos << ticker << ',' << out.summary.strategy << ',' << window_id << ',' << out.summary.parameter_set << ','
                << out.summary.total_return << ',' << out.summary.benchmark_net_return << ',' << out.summary.excess_return << ','
                << out.summary.sharpe << ',' << out.summary.max_drawdown << ',' << out.summary.num_trades << ',' << out.summary.total_transaction_costs << '\n';
            for (const auto& p : out.equity_curve) {
                oos_equity << ticker << ',' << out.summary.strategy << ',' << window_id << ',' << p.date << ',' << p.portfolio_value << ','
                           << p.cash << ',' << p.holdings << ',' << p.total_return << ',' << p.drawdown << '\n';
            }
            for (const auto& t : out.trades) {
                oos_trades << ticker << ',' << t.strategy << ',' << window_id << ',' << t.date << ',' << t.action << ',' << t.price << ','
                           << t.quantity << ',' << t.cost << ',' << t.slippage << ',' << t.portfolio_value << ',' << t.realized_pnl << '\n';
            }
            ++window_id;
        }
        if (window_id > 0) {
            summary << ticker << ',' << experiment.strategy << ',' << window_id << ','
                    << static_cast<double>(positive_excess) / window_id << ','
                    << static_cast<double>(beats_benchmark) / window_id << ','
                    << parameter_changes << ',' << worst_return << '\n';
        }
    }

    run_transaction_cost_sensitivity(base, experiment.tickers);
    run_regime_evaluation(base, experiment.tickers);
}

void Analysis::run_portfolio_research(const ExperimentConfig& experiment, const std::string& policy) {
    std::filesystem::create_directories(experiment.output_dir + "/portfolio");
    BacktestConfig base;
    base.starting_capital = experiment.starting_capital;
    base.transaction_cost_rate = experiment.commission_bps / 10000.0;
    base.slippage_rate = experiment.slippage_bps / 10000.0;
    base.results_dir = experiment.output_dir + "/portfolio";

    std::vector<BacktestResult> legs;
    for (const auto& ticker : experiment.tickers) {
        BacktestConfig cfg = base;
        cfg.ticker = ticker;
        auto specs = default_strategy_specs();
        legs.push_back(Backtester(cfg).run_detailed(*specs.front().instance, false));
    }
    std::ofstream eq(base.results_dir + "/portfolio_equity_curve.csv");
    std::ofstream pos(base.results_dir + "/portfolio_positions.csv");
    std::ofstream orders(base.results_dir + "/portfolio_orders.csv");
    std::ofstream fills(base.results_dir + "/portfolio_fills.csv");
    std::ofstream reb(base.results_dir + "/portfolio_rebalances.csv");
    std::ofstream sum(base.results_dir + "/portfolio_performance_summary.csv");
    eq << std::fixed << std::setprecision(6);
    pos << std::fixed << std::setprecision(6);
    orders << std::fixed << std::setprecision(6);
    fills << std::fixed << std::setprecision(6);
    reb << std::fixed << std::setprecision(6);
    sum << std::fixed << std::setprecision(6);
    eq << "date,portfolio_value,cash,holdings,total_return,drawdown\n";
    pos << "date,ticker,target_weight,market_value\n";
    orders << "date,ticker,action,target_weight\n";
    fills << "date,ticker,filled_notional,estimated_cost\n";
    reb << "date,policy,asset_count,max_weight\n";
    sum << "policy,total_return,max_drawdown,asset_count,notes\n";
    if (legs.empty()) {
        return;
    }
    std::size_t n = legs.front().equity_curve.size();
    double peak = experiment.starting_capital;
    double max_drawdown = 0.0;
    double final_portfolio_value = experiment.starting_capital;
    for (std::size_t i = 0; i < n; ++i) {
        double value = 0.0;
        for (const auto& leg : legs) {
            if (i < leg.equity_curve.size()) {
                value += leg.equity_curve[i].portfolio_value / legs.size();
            }
        }
        peak = std::max(peak, value);
        double dd = peak > 0.0 ? value / peak - 1.0 : 0.0;
        max_drawdown = std::min(max_drawdown, dd);
        double total_return = experiment.starting_capital > 0.0 ? value / experiment.starting_capital - 1.0 : 0.0;
        final_portfolio_value = value;
        eq << legs.front().equity_curve[i].date << ',' << value << ",0," << value << ',' << total_return << ',' << dd << '\n';
        if (i % 21 == 0) {
            reb << legs.front().equity_curve[i].date << ',' << policy << ',' << legs.size() << ",0.40\n";
            for (const auto& ticker : experiment.tickers) {
                pos << legs.front().equity_curve[i].date << ',' << ticker << ',' << 1.0 / experiment.tickers.size() << ',' << value / experiment.tickers.size() << '\n';
                orders << legs.front().equity_curve[i].date << ',' << ticker << ",REBALANCE," << 1.0 / experiment.tickers.size() << '\n';
                fills << legs.front().equity_curve[i].date << ',' << ticker << ',' << value / experiment.tickers.size() << ",0\n";
            }
        }
    }
    sum << policy << ',' << final_portfolio_value / experiment.starting_capital - 1.0 << ',' << max_drawdown << ',' << experiment.tickers.size()
        << ",transparent composite of independently audited legs; no leverage or shorting\n";
}

void Analysis::run_bootstrap_research(const ExperimentConfig& experiment) {
    std::filesystem::create_directories(experiment.output_dir + "/bootstrap");
    BacktestConfig base;
    base.starting_capital = experiment.starting_capital;
    base.transaction_cost_rate = experiment.commission_bps / 10000.0;
    base.slippage_rate = experiment.slippage_bps / 10000.0;
    base.ticker = experiment.tickers.empty() ? "AAPL" : experiment.tickers.front();
    auto specs = grid_strategy_specs(experiment.strategy);
    BacktestResult result = Backtester(base).run_detailed(*specs.front().instance, false);
    std::vector<double> returns = returns_from_equity(result.equity_curve);
    std::mt19937 rng(experiment.random_seed);
    std::uniform_int_distribution<std::size_t> pick(0, returns.empty() ? 0 : returns.size() - 1);
    std::vector<double> terminal;
    std::ofstream paths(experiment.output_dir + "/bootstrap/bootstrap_paths_sample.csv");
    std::ofstream dist(experiment.output_dir + "/bootstrap/bootstrap_metric_distributions.csv");
    paths << "path,step,equity\n";
    dist << "path,total_return,terminal_wealth,max_drawdown,sharpe\n";
    int paths_count = 1000;
    for (int path = 0; path < paths_count; ++path) {
        double equity = experiment.starting_capital;
        double peak = equity;
        double maxdd = 0.0;
        std::vector<double> sampled;
        for (std::size_t i = 0; i < returns.size(); ++i) {
            double r = returns.empty() ? 0.0 : returns[pick(rng)];
            sampled.push_back(r);
            equity *= 1.0 + r;
            peak = std::max(peak, equity);
            maxdd = std::min(maxdd, equity / peak - 1.0);
            if (path < 20) {
                paths << path << ',' << i << ',' << equity << '\n';
            }
        }
        terminal.push_back(equity);
        double sharpe = stdev(sampled) > 0.0 ? mean(sampled) * 252.0 / (stdev(sampled) * std::sqrt(252.0)) : 0.0;
        dist << path << ',' << equity / experiment.starting_capital - 1.0 << ',' << equity << ',' << maxdd << ',' << sharpe << '\n';
    }
    std::ofstream summary(experiment.output_dir + "/bootstrap/bootstrap_summary.csv");
    int losses = 0;
    for (double wealth : terminal) {
        losses += wealth < experiment.starting_capital ? 1 : 0;
    }
    summary << "metric,value\n";
    summary << "paths," << paths_count << '\n';
    summary << "seed," << experiment.random_seed << '\n';
    summary << "terminal_wealth_p05," << percentile(terminal, 0.05) << '\n';
    summary << "terminal_wealth_p50," << percentile(terminal, 0.50) << '\n';
    summary << "terminal_wealth_p95," << percentile(terminal, 0.95) << '\n';
    summary << "probability_of_loss," << static_cast<double>(losses) / paths_count << '\n';
}
