#include "Analysis.h"

#include "MarketData.h"
#include "ResearchMethodology.h"
#include "quant/io/ResultExporter.h"
#include "quant/config/ConfigLoader.h"
#include "quant/experiments/BootstrapAnalyzer.h"
#include "quant/experiments/SelectionRisk.h"
#include "quant/domain/Errors.h"

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
    quant::io::CsvResultExporter::write_combined_summary(filepath, rows);
}

MarketData load_market_data(const BacktestConfig& config, const std::string& ticker) {
    MarketData data;
    std::string path = config.data_dir + "/" + ticker + ".csv";
    if (!data.load_csv(ticker, path)) {
        throw std::runtime_error("Could not load " + path);
    }
    return data;
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
                      double profit_factor, double benchmark_relative_performance, double average_exposure,
                      const std::string& benchmark_ticker) {
    out << regime << ',' << ticker << ',' << strategy << ',' << benchmark_ticker << ','
        << total_return << ',' << sharpe << ',' << max_drawdown << ','
        << trade_count << ',' << win_rate << ',' << profit_factor << ','
        << benchmark_relative_performance << ',' << average_exposure
        << ",start_of_period_close,expanding_median_strictly_prior_volatility,start_of_period_regime\n";
}

double close_on_or_before(const std::vector<Bar>& bars, const std::string& date) {
    auto it = std::upper_bound(
        bars.begin(), bars.end(), date,
        [](const std::string& value, const Bar& bar) { return value < bar.date; });
    if (it == bars.begin()) {
        throw std::runtime_error("Benchmark has no observation on or before " + date);
    }
    return std::prev(it)->close;
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
            rolling -= values[i - static_cast<std::size_t>(window)];
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
            BacktestResult result = tester.run_detailed(*spec.instance);
            quant::io::CsvResultExporter::write_backtest(result, config.results_dir);
            summaries.push_back(result.summary);
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
            summaries.push_back(tester.run_detailed(*spec.instance).summary);
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
    windows << "schema_version,window_mode,ticker,strategy,window_id,train_start,train_end,test_start,test_end,train_observations,test_observations,parameter_set,in_sample_sharpe,in_sample_total_return,out_of_sample_total_return,out_of_sample_sharpe,out_of_sample_max_drawdown,out_of_sample_trades,starting_capital,ending_capital,continuity_policy,boundary_position_policy,boundary_liquidation_costs,linked_return,cumulative_oos_return,benchmark_ticker,benchmark_execution_policy,benchmark_cost_policy,excess_return_basis\n";
    equity_out << "schema_version,window_mode,continuity_policy,ticker,strategy,parameter_set,window_id,date,portfolio_value,cash,holdings,total_return,cumulative_oos_return,drawdown\n";
    history_out << "schema_version,window_mode,ticker,strategy,window_id,train_start,train_end,train_observations,selected_parameter_set,objective,train_sharpe,train_max_drawdown\n";

    int window_id = 0;
    for (const auto& ticker : tickers) {
        MarketData data = load_market_data(base_config, ticker);
        const auto& bars = data.bars(ticker);
        const auto calendar_windows = build_calendar_windows(bars, 3, 6, 6);
        std::map<std::string, double> capital_by_strategy;
        for (const auto& window : calendar_windows) {

            std::map<std::string, PerformanceSummary> best_by_strategy;
            for (const auto& spec : grid_strategy_specs()) {
                BacktestConfig train_config = base_config;
                train_config.ticker = ticker;
                train_config.start_date = window.train_start;
                train_config.end_date = window.train_end;
                Backtester train_tester(train_config);
                PerformanceSummary train_summary = train_tester.run_detailed(*spec.instance).summary;
                if (!best_by_strategy.count(spec.strategy) || objective(train_summary) > objective(best_by_strategy[spec.strategy])) {
                    best_by_strategy[spec.strategy] = train_summary;
                }
            }

            for (const auto& selected : best_by_strategy) {
                auto frozen = make_strategy(selected.second.strategy, selected.second.parameter_set);
                BacktestConfig test_config = base_config;
                test_config.ticker = ticker;
                double starting_capital = capital_by_strategy.count(selected.first)
                    ? capital_by_strategy[selected.first] : base_config.starting_capital;
                test_config.starting_capital = starting_capital;
                test_config.start_date = window.test_start;
                test_config.end_date = window.test_end;
                test_config.liquidate_at_end = true;
                Backtester test_tester(test_config);
                BacktestResult out_sample = test_tester.run_detailed(*frozen);
                const double ending_capital = out_sample.equity_curve.empty()
                    ? starting_capital : out_sample.equity_curve.back().portfolio_value;
                capital_by_strategy[selected.first] = ending_capital;
                double boundary_costs = 0.0;
                for (const auto& trade : out_sample.trades) {
                    if (trade.date == window.test_end && trade.action == "SELL") {
                        boundary_costs += trade.cost + trade.slippage;
                    }
                }
                const double cumulative_return = ending_capital / base_config.starting_capital - 1.0;

                windows << "2,calendar_duration," << ticker << ',' << selected.second.strategy << ',' << window_id << ','
                    << window.train_start << ',' << window.train_end << ','
                    << window.test_start << ',' << window.test_end << ','
                    << window.train_observations << ',' << window.test_observations << ','
                    << selected.second.parameter_set << ','
                    << selected.second.sharpe << ',' << selected.second.total_return << ','
                    << out_sample.summary.total_return << ',' << out_sample.summary.sharpe << ','
                    << out_sample.summary.max_drawdown << ',' << out_sample.summary.num_trades << ','
                    << starting_capital << ',' << ending_capital
                    << ",continuous_capital,liquidate_at_test_end_close," << boundary_costs << ','
                    << out_sample.summary.total_return << ',' << cumulative_return << ','
                    << out_sample.summary.benchmark_ticker << ',' << out_sample.summary.benchmark_execution_policy << ','
                    << out_sample.summary.benchmark_cost_policy << ',' << out_sample.summary.excess_return_basis << '\n';

                history_out << "2,calendar_duration," << ticker << ',' << selected.second.strategy << ',' << window_id << ','
                    << window.train_start << ',' << window.train_end << ',' << window.train_observations << ','
                    << selected.second.parameter_set << ',' << objective(selected.second) << ','
                    << selected.second.sharpe << ',' << selected.second.max_drawdown << '\n';

                for (const auto& point : out_sample.equity_curve) {
                    equity_out << "2,calendar_duration,continuous_capital," << ticker << ',' << selected.second.strategy << ',' << selected.second.parameter_set << ','
                        << window_id << ',' << point.date << ',' << point.portfolio_value << ','
                        << point.cash << ',' << point.holdings << ',' << point.total_return << ','
                        << point.portfolio_value / base_config.starting_capital - 1.0 << ',' << point.drawdown << '\n';
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
    const std::string header = "schema_version,ticker,strategy,parameter_set,benchmark_ticker,benchmark_execution_policy,benchmark_cost_policy,excess_return_basis,commission_bps,slippage_bps,net_return,sharpe,turnover,total_costs,degradation_vs_zero_cost,estimated_break_even_cost_bps\n";
    out << header;
    surface << header;
    break_even_out << "ticker,strategy,parameter_set,estimated_break_even_all_in_bps,method\n";

    for (const auto& ticker : tickers) {
        for (const auto& spec : default_strategy_specs()) {
            BacktestConfig zero = base_config;
            zero.ticker = ticker;
            zero.transaction_cost_rate = 0.0;
            zero.slippage_rate = 0.0;
            double zero_return = Backtester(zero).run_detailed(*spec.instance).summary.total_return;
            double best_break_even = 0.0;
            for (int commission_bps : {0, 5, 10, 20, 40}) {
                for (int slippage_bps : {0, 5, 10, 25, 50}) {
                    BacktestConfig config = base_config;
                    config.ticker = ticker;
                    config.transaction_cost_rate = commission_bps / 10000.0;
                    config.slippage_rate = slippage_bps / 10000.0;
                    PerformanceSummary summary = Backtester(config).run_detailed(*spec.instance).summary;
                    double total_bps = commission_bps + slippage_bps;
                    double degradation = zero_return - summary.total_return;
                    double break_even = degradation > 0.0 && total_bps > 0.0 ? total_bps * (zero_return / degradation) : 0.0;
                    if (break_even > 0.0) {
                        best_break_even = best_break_even == 0.0 ? break_even : std::min(best_break_even, break_even);
                    }
                    std::ostringstream row;
                    row << std::fixed << std::setprecision(6);
                    row << summary.schema_version << ',' << ticker << ',' << summary.strategy << ',' << summary.parameter_set << ','
                        << summary.benchmark_ticker << ',' << summary.benchmark_execution_policy << ','
                        << summary.benchmark_cost_policy << ',' << summary.excess_return_basis << ','
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
    const std::string header = "schema_version,regime,ticker,strategy,benchmark_ticker,return,sharpe,drawdown,trade_count,win_rate,profit_factor,benchmark_relative_performance,average_exposure,regime_information_cutoff,volatility_threshold_method,return_attribution_policy\n";
    out << header;
    performance << header;
    assignments << "schema_version,date,ticker,trend_regime,volatility_regime,regime,volatility_value,volatility_threshold,information_cutoff,volatility_threshold_method\n";
    const std::vector<std::string> regimes = {
        "bull/low_volatility", "bull/high_volatility",
        "bear/low_volatility", "bear/high_volatility",
        "sideways/low_volatility", "sideways/high_volatility"
    };

    for (const auto& ticker : tickers) {
        MarketData data = load_market_data(base_config, ticker);
        const auto& bars = data.bars(ticker);
        const std::string resolved_benchmark = base_config.benchmark_ticker == "same_asset"
            ? ticker : base_config.benchmark_ticker;
        MarketData benchmark_data = load_market_data(base_config, resolved_benchmark);
        const auto& benchmark_bars = benchmark_data.bars(resolved_benchmark);
        const auto causal_regimes = classify_causal_regimes(bars, 20, 60);
        for (const auto& point : causal_regimes) {
            assignments << "2," << point.date << ',' << ticker << ',' << point.trend_regime << ','
                        << point.volatility_regime << ',' << point.regime << ',' << point.volatility_value << ','
                        << point.volatility_threshold << ',' << point.information_cutoff
                        << ",expanding_median_strictly_prior_volatility\n";
        }

        for (const auto& spec : default_strategy_specs()) {
            BacktestConfig config = base_config;
            config.ticker = ticker;
            BacktestResult result = Backtester(config).run_detailed(*spec.instance);

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
                    const auto& previous_point = result.equity_curve[i - 1];
                    const RegimePoint* assigned = regime_for_return_interval(causal_regimes, previous_point.date, point.date);
                    if (assigned == nullptr || assigned->regime != regime) {
                        continue;
                    }
                    double prev_value = result.equity_curve[i - 1].portfolio_value;
                    double ret = prev_value > 0.0 ? (point.portfolio_value / prev_value) - 1.0 : 0.0;
                    regime_returns.push_back(ret);
                    equity *= 1.0 + ret;
                    const double benchmark_start = close_on_or_before(benchmark_bars, previous_point.date);
                    const double benchmark_end = close_on_or_before(benchmark_bars, point.date);
                    const double benchmark_ret = benchmark_start > 0.0 ? benchmark_end / benchmark_start - 1.0 : 0.0;
                    benchmark_returns.push_back(benchmark_ret);
                    benchmark_equity *= 1.0 + benchmark_ret;
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
                    const RegimePoint* assigned = regime_for_execution(causal_regimes, trade.date);
                    if (assigned == nullptr || assigned->regime != regime) {
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
                out << "2,";
                write_regime_row(out, regime, ticker, spec.strategy, equity - 1.0, sharpe, max_drawdown, trade_count, win_rate,
                                 profit_factor, benchmark_relative, average_exposure, resolved_benchmark);
                performance << "2,";
                write_regime_row(performance, regime, ticker, spec.strategy, equity - 1.0, sharpe, max_drawdown, trade_count, win_rate,
                                 profit_factor, benchmark_relative, average_exposure, resolved_benchmark);
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
            Backtester(config).run_detailed(strategy);
        }
        return bars.size() * 10;
    });

    measure("full_parameter_sweep_AAPL", [&]() {
        BacktestConfig config = base_config;
        config.ticker = "AAPL";
        std::size_t runs = 0;
        for (const auto& spec : grid_strategy_specs()) {
            Backtester(config).run_detailed(*spec.instance);
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
    return quant::config::ConfigLoader::load_file(filepath);
}

void Analysis::run_research_experiment(const ExperimentConfig& experiment) {
    std::filesystem::create_directories(experiment.output.results_dir);
    quant::io::JsonManifestExporter::write_resolved_config(
        experiment.output.results_dir + "/resolved_config.json", experiment);

    if (experiment.strategy == "All_Strategies" || experiment.name.rfind("selection_risk_", 0) == 0) {
        quant::experiments::SelectionRiskAnalyzer::run_experiment(experiment);
        return;
    }

    BacktestConfig base;
    base.starting_capital = experiment.execution.starting_capital;
    base.transaction_cost_rate = experiment.execution.commission_bps / 10000.0;
    base.slippage_rate = experiment.execution.slippage_bps / 10000.0;
    base.results_dir = experiment.output.results_dir;
    base.benchmark_ticker = experiment.benchmark.ticker;

    std::ofstream grid_out(experiment.output.results_dir + "/in_sample_full_grid.csv");
    grid_out << std::fixed << std::setprecision(6);
    grid_out << "schema_version,ticker,strategy,parameter_set,start_date,end_date,benchmark_ticker,benchmark_execution_policy,benchmark_cost_policy,excess_return_basis,total_return,benchmark_net_return,excess_return,sharpe,sortino,max_drawdown,calmar,turnover,total_costs,trade_count,win_rate,profit_factor\n";
    for (const auto& ticker : experiment.tickers) {
        for (const auto& spec : grid_strategy_specs(experiment.strategy)) {
            BacktestConfig cfg = base;
            cfg.ticker = ticker;
            BacktestResult result = Backtester(cfg).run_detailed(*spec.instance);
            grid_out << result.summary.schema_version << ',' << ticker << ',' << result.summary.strategy << ',' << result.summary.parameter_set << ",full,full,"
                     << result.summary.benchmark_ticker << ',' << result.summary.benchmark_execution_policy << ','
                     << result.summary.benchmark_cost_policy << ',' << result.summary.excess_return_basis << ','
                     << result.summary.total_return << ',' << result.summary.benchmark_net_return << ',' << result.summary.excess_return << ','
                     << result.summary.sharpe << ',' << sortino_from_equity(result.equity_curve) << ','
                     << result.summary.max_drawdown << ',' << calmar_from_summary(result.summary) << ','
                     << result.summary.turnover << ',' << result.summary.total_transaction_costs << ','
                     << result.summary.num_trades << ',' << result.summary.win_rate << ',' << result.summary.profit_factor << '\n';
        }
    }

    std::filesystem::create_directories(experiment.output.results_dir + "/walk_forward");
    std::ofstream windows(experiment.output.results_dir + "/walk_forward/windows.csv");
    std::ofstream history(experiment.output.results_dir + "/walk_forward/parameter_history.csv");
    std::ofstream insample(experiment.output.results_dir + "/walk_forward/in_sample_results.csv");
    std::ofstream oos(experiment.output.results_dir + "/walk_forward/out_of_sample_results.csv");
    std::ofstream oos_equity(experiment.output.results_dir + "/walk_forward/oos_equity_curve.csv");
    std::ofstream oos_trades(experiment.output.results_dir + "/walk_forward/oos_trades.csv");
    std::ofstream summary(experiment.output.results_dir + "/walk_forward/summary.csv");
    windows << std::fixed << std::setprecision(6);
    history << std::fixed << std::setprecision(6);
    insample << std::fixed << std::setprecision(6);
    oos << std::fixed << std::setprecision(6);
    oos_equity << std::fixed << std::setprecision(6);
    oos_trades << std::fixed << std::setprecision(6);
    summary << std::fixed << std::setprecision(6);
    windows << "schema_version,window_mode,ticker,strategy,window_id,train_start,train_end,test_start,test_end,train_observations,test_observations,selected_parameters,objective_value,parameter_changed,starting_capital,ending_capital,continuity_policy,boundary_position_policy,boundary_liquidation_costs,linked_return,cumulative_oos_return,benchmark_ticker,benchmark_execution_policy,benchmark_cost_policy,excess_return_basis\n";
    history << "schema_version,window_mode,ticker,strategy,window_id,selected_parameters,objective_value,in_sample_sharpe,in_sample_return\n";
    insample << "schema_version,window_mode,ticker,strategy,window_id,parameter_set,total_return,sharpe,max_drawdown,trade_count\n";
    oos << "schema_version,window_mode,continuity_policy,ticker,strategy,window_id,parameter_set,starting_capital,ending_capital,total_return,cumulative_oos_return,benchmark_ticker,benchmark_net_return,excess_return,excess_return_basis,sharpe,max_drawdown,trade_count,total_costs,boundary_liquidation_costs\n";
    oos_equity << "schema_version,window_mode,continuity_policy,ticker,strategy,window_id,date,portfolio_value,cash,holdings,total_return,cumulative_oos_return,drawdown\n";
    oos_trades << "schema_version,window_mode,continuity_policy,ticker,strategy,window_id,date,action,price,quantity,cost,slippage,portfolio_value,realized_pnl\n";
    summary << "schema_version,window_mode,continuity_policy,boundary_position_policy,ticker,strategy,windows,positive_oos_excess_rate,benchmark_win_rate,parameter_changes,worst_oos_return,initial_capital,final_capital,cumulative_oos_return,benchmark_ticker\n";

    for (const auto& ticker : experiment.tickers) {
        MarketData data = load_market_data(base, ticker);
        const auto& bars = data.bars(ticker);
        std::vector<CalendarWindow> experiment_windows;
        if (experiment.walk_forward.window_mode == "calendar_duration") {
            experiment_windows = build_calendar_windows(bars, experiment.walk_forward.train_years, experiment.walk_forward.test_months, experiment.walk_forward.step_months);
        } else {
            for (std::size_t begin = 0;
                 begin + static_cast<std::size_t>(experiment.walk_forward.train_days + experiment.walk_forward.test_days) <= bars.size();
                 begin += static_cast<std::size_t>(experiment.walk_forward.step_days)) {
                const std::size_t train_end = begin + static_cast<std::size_t>(experiment.walk_forward.train_days) - 1;
                const std::size_t test_begin = train_end + 1;
                const std::size_t test_end = test_begin + static_cast<std::size_t>(experiment.walk_forward.test_days) - 1;
                experiment_windows.push_back(CalendarWindow{begin, train_end, test_begin, test_end,
                    bars[begin].date, bars[train_end].date, bars[test_begin].date, bars[test_end].date,
                    static_cast<std::size_t>(experiment.walk_forward.train_days), static_cast<std::size_t>(experiment.walk_forward.test_days)});
            }
        }
        int window_id = 0;
        int positive_excess = 0;
        int beats_benchmark = 0;
        int parameter_changes = 0;
        double worst_return = 1e9;
        double continuous_capital = experiment.execution.starting_capital;
        std::string previous_params;
        for (const auto& window : experiment_windows) {
            PerformanceSummary best;
            double best_score = -1e18;
            for (const auto& spec : grid_strategy_specs(experiment.strategy)) {
                BacktestConfig train = base;
                train.ticker = ticker;
                train.start_date = window.train_start;
                train.end_date = window.train_end;
                BacktestResult r = Backtester(train).run_detailed(*spec.instance);
                double score = objective(r.summary, experiment.parameter_selection.objective, experiment.parameter_selection.minimum_trades);
                insample << experiment.result_schema_version << ',' << experiment.walk_forward.window_mode << ',' << ticker << ',' << r.summary.strategy << ',' << window_id << ',' << r.summary.parameter_set << ','
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
            const double starting_capital = experiment.walk_forward.continuity_policy == "continuous_capital"
                ? continuous_capital : experiment.execution.starting_capital;
            test.starting_capital = starting_capital;
            test.start_date = window.test_start;
            test.end_date = window.test_end;
            test.liquidate_at_end = true;
            BacktestResult out = Backtester(test).run_detailed(*frozen);
            const double ending_capital = out.equity_curve.empty() ? starting_capital : out.equity_curve.back().portfolio_value;
            if (experiment.walk_forward.continuity_policy == "continuous_capital") {
                continuous_capital = ending_capital;
            }
            double boundary_costs = 0.0;
            for (const auto& trade : out.trades) {
                if (trade.date == window.test_end && trade.action == "SELL") {
                    boundary_costs += trade.cost + trade.slippage;
                }
            }
            const double cumulative_return = (experiment.walk_forward.continuity_policy == "continuous_capital" ? continuous_capital : ending_capital)
                / experiment.execution.starting_capital - 1.0;
            positive_excess += out.summary.excess_return > 0.0 ? 1 : 0;
            beats_benchmark += out.summary.total_return > out.summary.benchmark_net_return ? 1 : 0;
            worst_return = std::min(worst_return, out.summary.total_return);
            windows << experiment.result_schema_version << ',' << experiment.walk_forward.window_mode << ',' << ticker << ',' << best.strategy << ',' << window_id << ','
                    << window.train_start << ',' << window.train_end << ',' << window.test_start << ',' << window.test_end << ','
                    << window.train_observations << ',' << window.test_observations << ',' << best.parameter_set << ',' << best_score << ',' << (changed ? 1 : 0) << ','
                    << starting_capital << ',' << ending_capital << ',' << experiment.walk_forward.continuity_policy << ',' << experiment.walk_forward.boundary_position_policy << ','
                    << boundary_costs << ',' << out.summary.total_return << ',' << cumulative_return << ',' << out.summary.benchmark_ticker << ','
                    << out.summary.benchmark_execution_policy << ',' << out.summary.benchmark_cost_policy << ',' << out.summary.excess_return_basis << '\n';
            history << experiment.result_schema_version << ',' << experiment.walk_forward.window_mode << ',' << ticker << ',' << best.strategy << ',' << window_id << ',' << best.parameter_set << ',' << best_score << ',' << best.sharpe << ',' << best.total_return << '\n';
            oos << experiment.result_schema_version << ',' << experiment.walk_forward.window_mode << ',' << experiment.walk_forward.continuity_policy << ',' << ticker << ',' << out.summary.strategy << ',' << window_id << ',' << out.summary.parameter_set << ','
                << starting_capital << ',' << ending_capital << ',' << out.summary.total_return << ',' << cumulative_return << ',' << out.summary.benchmark_ticker << ','
                << out.summary.benchmark_net_return << ',' << out.summary.excess_return << ',' << out.summary.excess_return_basis << ','
                << out.summary.sharpe << ',' << out.summary.max_drawdown << ',' << out.summary.num_trades << ',' << out.summary.total_transaction_costs << ',' << boundary_costs << '\n';
            for (const auto& p : out.equity_curve) {
                oos_equity << experiment.result_schema_version << ',' << experiment.walk_forward.window_mode << ',' << experiment.walk_forward.continuity_policy << ',' << ticker << ',' << out.summary.strategy << ',' << window_id << ',' << p.date << ',' << p.portfolio_value << ','
                           << p.cash << ',' << p.holdings << ',' << p.total_return << ',' << p.portfolio_value / experiment.execution.starting_capital - 1.0 << ',' << p.drawdown << '\n';
            }
            for (const auto& t : out.trades) {
                oos_trades << experiment.result_schema_version << ',' << experiment.walk_forward.window_mode << ',' << experiment.walk_forward.continuity_policy << ',' << ticker << ',' << t.strategy << ',' << window_id << ',' << t.date << ',' << t.action << ',' << t.price << ','
                           << t.quantity << ',' << t.cost << ',' << t.slippage << ',' << t.portfolio_value << ',' << t.realized_pnl << '\n';
            }
            ++window_id;
        }
        if (window_id > 0) {
            summary << experiment.result_schema_version << ',' << experiment.walk_forward.window_mode << ',' << experiment.walk_forward.continuity_policy << ',' << experiment.walk_forward.boundary_position_policy << ',' << ticker << ',' << experiment.strategy << ',' << window_id << ','
                    << static_cast<double>(positive_excess) / window_id << ','
                    << static_cast<double>(beats_benchmark) / window_id << ','
                    << parameter_changes << ',' << worst_return << ',' << experiment.execution.starting_capital << ',' << continuous_capital << ','
                    << continuous_capital / experiment.execution.starting_capital - 1.0 << ',' << (experiment.benchmark.ticker == "same_asset" ? ticker : experiment.benchmark.ticker) << '\n';
        }
    }

    run_transaction_cost_sensitivity(base, experiment.tickers);
    run_regime_evaluation(base, experiment.tickers);
    quant::experiments::SelectionRiskAnalyzer::run_experiment(experiment);
}

void Analysis::run_bootstrap_research(const ExperimentConfig& experiment) {
    BacktestConfig base;
    base.starting_capital = experiment.execution.starting_capital;
    base.transaction_cost_rate = experiment.execution.commission_bps / 10000.0;
    base.slippage_rate = experiment.execution.slippage_bps / 10000.0;
    base.ticker = experiment.tickers.empty() ? "AAPL" : experiment.tickers.front();
    auto specs = grid_strategy_specs(experiment.strategy);
    if (specs.empty()) {
        throw quant::ConfigurationError("Bootstrap research requires a non-empty single-family strategy grid");
    }
    BacktestResult result = Backtester(base).run_detailed(*specs.front().instance);
    const auto bootstrap = quant::experiments::BootstrapAnalyzer::run(
        result, experiment.bootstrap, experiment.execution.starting_capital);
    quant::io::CsvResultExporter::write_bootstrap(bootstrap, experiment.output.results_dir + "/bootstrap");
}
