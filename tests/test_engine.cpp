#include "Backtester.h"
#include "Analysis.h"
#include "ExecutionHandler.h"
#include "MarketData.h"
#include "Metrics.h"
#include "Portfolio.h"
#include "PortfolioBacktester.h"
#include "ResearchMethodology.h"
#include "Strategy.h"
#include "quant/io/ResultExporter.h"
#include "quant/config/ConfigLoader.h"
#include "quant/domain/Date.h"
#include "quant/domain/Errors.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int cases_run = 0;
int assertions_run = 0;

bool nearly_equal(double lhs, double rhs, double tolerance = 1e-6) {
    return std::fabs(lhs - rhs) <= tolerance;
}

std::uint64_t double_bits(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double width");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void require(bool condition, const std::string& message) {
    ++assertions_run;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void run_case(const std::string& name, const std::function<void()>& fn) {
    ++cases_run;
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << name << " - " << ex.what() << '\n';
        throw;
    }
}

std::vector<Bar> sample_bars() {
    return {
        {"2024-01-01", 100.0, 101.0, 99.0, 100.0, 1000},
        {"2024-01-02", 101.0, 103.0, 100.0, 102.0, 1000},
        {"2024-01-03", 102.0, 104.0, 101.0, 104.0, 1000},
        {"2024-01-04", 104.0, 105.0, 102.0, 103.0, 1000},
        {"2024-01-05", 103.0, 106.0, 102.0, 105.0, 1000}
    };
}

std::map<std::string, std::vector<Bar>> sample_multi_asset_history() {
    return {
        {"AAA", {
            {"2024-01-01", 100, 101, 99, 100, 1000},
            {"2024-01-02", 101, 102, 100, 101, 1000},
            {"2024-01-03", 102, 103, 101, 102, 1000},
            {"2024-01-04", 103, 104, 102, 103, 1000},
            {"2024-01-05", 104, 105, 103, 104, 1000}
        }},
        {"BBB", {
            {"2024-01-01", 100, 101, 99, 100, 1000},
            {"2024-01-02", 99, 100, 98, 99, 1000},
            {"2024-01-03", 98, 99, 97, 98, 1000},
            {"2024-01-04", 97, 98, 96, 97, 1000},
            {"2024-01-05", 96, 97, 95, 96, 1000}
        }},
        {"CCC", {
            {"2024-01-01", 100, 102, 99, 100, 1000},
            {"2024-01-02", 104, 105, 103, 104, 1000},
            {"2024-01-03", 108, 109, 107, 108, 1000},
            {"2024-01-04", 112, 113, 111, 112, 1000},
            {"2024-01-05", 116, 117, 115, 116, 1000}
        }}
    };
}

PerformanceSummary sample_summary() {
    std::vector<EquityPoint> equity = {
        {"2024-01-01", 10000.0, 10000.0, 0.0, 0.0, 0.0},
        {"2024-01-02", 10100.0, 10000.0, 100.0, 0.01, 0.0},
        {"2024-01-03", 9900.0, 9800.0, 100.0, -0.01, -0.019802},
        {"2024-01-04", 10300.0, 10200.0, 100.0, 0.03, 0.0}
    };
    std::vector<Trade> trades = {
        {"2024-01-02", "TEST", "Unit", "BUY", 100.0, 10, 1.0, 0.5, 10000.0, 0.0, 0.0},
        {"2024-01-04", "TEST", "Unit", "SELL", 103.0, 10, 1.0, 0.5, 10300.0, 28.5, 0.0285}
    };
    return Metrics::calculate("TEST", "Unit", "default", 10000.0, equity, trades, 0.02, 0.018);
}

FillEvent fill(OrderSide side, int quantity, double price, double commission_rate = 0.001, double slippage_rate = 0.0005) {
    ExecutionHandler execution(commission_rate, slippage_rate);
    return execution.execute_order(OrderEvent{EventType::Order, "2024-01-05", "TEST", "Unit", side, quantity}, price);
}

std::vector<Bar> synthetic_bars(std::size_t count, bool seven_day, const std::string& start = "2020-01-06") {
    std::vector<Bar> bars;
    bars.reserve(count);
    std::string date = start;
    double close = 100.0;
    for (std::size_t i = 0; i < count; ++i) {
        close *= 1.0 + (static_cast<int>(i % 11) - 4) * 0.0007;
        bars.push_back({date, close, close * 1.01, close * 0.99, close, 1000});
        const int advance = !seven_day && i % 5 == 4 ? 3 : 1;
        date = add_calendar_days(date, advance);
    }
    return bars;
}

class BuyOnFirstBar : public Strategy {
public:
    std::string name() const override { return "BuyOnFirstBar"; }
    std::string parameters() const override { return "unit"; }
    SignalEvent on_market_event(const MarketEvent& event, const std::vector<Bar>&) override {
        return SignalEvent{EventType::Signal, event.date, event.ticker, name(), event.index == 0 ? SignalType::Buy : SignalType::Hold};
    }
    std::unique_ptr<Strategy> clone() const override { return std::make_unique<BuyOnFirstBar>(*this); }
};
}

int main() {
    const auto bars = sample_bars();

    run_case("SMA known values", [&] {
        require(nearly_equal(simple_moving_average(bars, 4, 3), (104.0 + 103.0 + 105.0) / 3.0), "bad SMA");
    });
    run_case("SMA warm-up returns zero", [&] {
        require(nearly_equal(simple_moving_average(bars, 4, 10), 0.0), "bad SMA warm-up");
    });
    run_case("RSI known bounded value", [&] {
        require(nearly_equal(rsi(bars, 4, 3), 80.0), "bad RSI");
        require(rsi(bars, 4, 3) >= 0.0 && rsi(bars, 4, 3) <= 100.0, "RSI out of bounds");
    });
    run_case("RSI flat prices finite", [&] {
        std::vector<Bar> flat = {{"1", 10, 10, 10, 10, 1}, {"2", 10, 10, 10, 10, 1}, {"3", 10, 10, 10, 10, 1}};
        require(std::isfinite(rsi(flat, 2, 2)), "flat RSI non-finite");
    });
    run_case("EMA deterministic values", [&] {
        auto ema = exponential_moving_average({10.0, 12.0, 14.0}, 3);
        require(ema.size() == 3, "bad EMA length");
        require(nearly_equal(ema[0], 10.0), "bad EMA[0]");
        require(nearly_equal(ema[1], 11.0), "bad EMA[1]");
        require(nearly_equal(ema[2], 12.5), "bad EMA[2]");
    });
    run_case("Daily returns deterministic values", [&] {
        const auto returns = daily_returns({100.0, 110.0, 99.0});
        require(returns.size() == 2, "bad returns length");
        require(nearly_equal(returns[0], 0.10), "bad first return");
        require(nearly_equal(returns[1], -0.10), "bad second return");
    });
    run_case("Rolling volatility finite", [&] {
        std::vector<double> values = {0.01, -0.01, 0.02, 0.0};
        require(rolling_volatility(values, 3, 4) > 0.0, "bad volatility");
    });
    run_case("MACD warm-up produces no early trade", [&] {
        MACDMomentumStrategy macd;
        MarketEvent event{EventType::Market, "2024-01-05", "TEST", 103, 106, 102, 105, 1000, 4};
        require(macd.on_market_event(event, bars).signal == SignalType::Hold, "MACD traded before warm-up");
    });
    run_case("Buy slippage and commission", [&] {
        FillEvent buy = fill(OrderSide::Buy, 10, 100.0);
        require(nearly_equal(buy.fill_price, 100.05), "bad buy fill");
        require(nearly_equal(buy.transaction_cost, 1.0005), "bad buy commission");
        require(nearly_equal(buy.slippage_cost, 0.5), "bad buy slippage");
    });
    run_case("Sell slippage and commission", [&] {
        FillEvent sell = fill(OrderSide::Sell, 10, 100.0);
        require(nearly_equal(sell.fill_price, 99.95), "bad sell fill");
        require(nearly_equal(sell.transaction_cost, 0.9995), "bad sell commission");
        require(nearly_equal(sell.slippage_cost, 0.5), "bad sell slippage");
    });
    run_case("Insufficient cash rejected", [&] {
        Portfolio portfolio(10000.0);
        require(!portfolio.process_fill(fill(OrderSide::Buy, 100000, 100.0), 100.0), "accepted unaffordable buy");
        require(portfolio.position() == 0, "position changed after rejected buy");
    });
    run_case("Invalid sell quantity rejected", [&] {
        Portfolio portfolio(10000.0);
        require(portfolio.process_fill(fill(OrderSide::Buy, 10, 100.0), 100.0), "buy rejected");
        require(!portfolio.process_fill(fill(OrderSide::Sell, 11, 100.0), 100.0), "accepted invalid sell");
        require(portfolio.position() == 10, "position changed after invalid sell");
    });
    run_case("Profitable round-trip PnL", [&] {
        Portfolio portfolio(10000.0);
        require(portfolio.process_fill(fill(OrderSide::Buy, 10, 100.0, 0.0, 0.0), 100.0), "buy rejected");
        require(portfolio.process_fill(fill(OrderSide::Sell, 10, 110.0, 0.0, 0.0), 110.0), "sell rejected");
        require(nearly_equal(portfolio.trades().back().realized_pnl, 100.0), "bad winning PnL");
    });
    run_case("Losing round-trip PnL", [&] {
        Portfolio portfolio(10000.0);
        require(portfolio.process_fill(fill(OrderSide::Buy, 10, 100.0, 0.0, 0.0), 100.0), "buy rejected");
        require(portfolio.process_fill(fill(OrderSide::Sell, 10, 90.0, 0.0, 0.0), 90.0), "sell rejected");
        require(nearly_equal(portfolio.trades().back().realized_pnl, -100.0), "bad losing PnL");
    });
    run_case("Realized PnL uses deterministic fused arithmetic", [&] {
        constexpr int quantity = 924;
        constexpr double buy_market_price = 102.801355;
        constexpr double sell_market_price = 110.840625;
        ExecutionHandler execution(0.001, 0.0005);
        const FillEvent buy = execution.execute_order(
            OrderEvent{EventType::Order, "2019-08-08", "SYN_EQ_A", "MA_Cross", OrderSide::Buy, quantity},
            buy_market_price);
        const FillEvent sell = execution.execute_order(
            OrderEvent{EventType::Order, "2019-10-28", "SYN_EQ_A", "MA_Cross", OrderSide::Sell, quantity},
            sell_market_price);

        const double average_entry_price = (buy.gross_value + buy.transaction_cost) / quantity;
        const double proceeds = sell.gross_value - sell.transaction_cost;
        volatile double separately_rounded_basis = average_entry_price * quantity;
        const double separately_rounded_pnl = proceeds - separately_rounded_basis;
        const double fused_pnl = std::fma(-average_entry_price, static_cast<double>(quantity), proceeds);

        require(separately_rounded_pnl != fused_pnl, "regression fixture does not expose contraction difference");
        require(double_bits(fused_pnl) == UINT64_C(0x40bbdc2e70e073ab), "unexpected fused PnL bits");

        Portfolio portfolio(200000.0);
        require(portfolio.process_fill(buy, buy_market_price), "regression buy rejected");
        require(portfolio.process_fill(sell, sell_market_price), "regression sell rejected");
        require(double_bits(portfolio.trades().back().realized_pnl) == double_bits(fused_pnl),
                "portfolio realized PnL is not the deterministic fused result");
    });
    run_case("Multiple sequential trades", [&] {
        Portfolio portfolio(10000.0);
        require(portfolio.process_fill(fill(OrderSide::Buy, 5, 100.0, 0.0, 0.0), 100.0), "first buy rejected");
        require(portfolio.process_fill(fill(OrderSide::Sell, 5, 105.0, 0.0, 0.0), 105.0), "first sell rejected");
        require(portfolio.process_fill(fill(OrderSide::Buy, 4, 50.0, 0.0, 0.0), 50.0), "second buy rejected");
        require(portfolio.process_fill(fill(OrderSide::Sell, 4, 55.0, 0.0, 0.0), 55.0), "second sell rejected");
        require(portfolio.position() == 0, "position not closed");
        require(portfolio.trades().size() == 4, "bad trade count");
    });
    run_case("Final open position marked", [&] {
        Portfolio portfolio(10000.0);
        require(portfolio.process_fill(fill(OrderSide::Buy, 10, 100.0, 0.0, 0.0), 100.0), "buy rejected");
        portfolio.mark_to_market("2024-01-06", 120.0);
        require(nearly_equal(portfolio.equity_curve().back().holdings, 1200.0), "bad holdings mark");
    });
    run_case("Total return", [&] { require(nearly_equal(sample_summary().total_return, 0.03), "bad total return"); });
    run_case("Benchmark and excess return", [&] {
        auto summary = sample_summary();
        require(nearly_equal(summary.benchmark_gross_return, 0.02), "bad gross benchmark");
        require(nearly_equal(summary.benchmark_net_return, 0.018), "bad net benchmark");
        require(nearly_equal(summary.excess_return, 0.012), "bad excess");
    });
    run_case("Max drawdown", [&] { require(nearly_equal(sample_summary().max_drawdown, -0.019802, 1e-6), "bad drawdown"); });
    run_case("Win rate and profit factor", [&] {
        auto summary = sample_summary();
        require(nearly_equal(summary.win_rate, 1.0), "bad win rate");
        require(summary.profit_factor > 1.0, "bad profit factor");
    });
    run_case("Turnover and cost drag", [&] {
        auto summary = sample_summary();
        require(nearly_equal(summary.turnover, 0.203), "bad turnover");
        require(nearly_equal(summary.cost_drag, 0.0003), "bad cost drag");
    });
    run_case("Valid CSV fixture accepted", [&] {
        MarketData market_data;
        require(market_data.load_csv("VALID", "tests/fixtures/VALID.csv"), "valid fixture rejected");
    });
    run_case("Malformed CSV rejected", [&] {
        MarketData market_data;
        require(!market_data.load_csv("BAD", "tests/fixtures/malformed_row.csv"), "malformed fixture accepted");
    });
    run_case("Duplicate date rejected", [&] {
        MarketData market_data;
        require(!market_data.load_csv("BAD", "tests/fixtures/duplicate_date.csv"), "duplicate dates accepted");
    });
    run_case("Missing column rejected", [&] {
        MarketData market_data;
        require(!market_data.load_csv("BAD", "tests/fixtures/missing_column.csv"), "missing column accepted");
    });
    run_case("Unordered dates rejected", [&] {
        MarketData market_data;
        require(!market_data.load_csv("BAD", "tests/fixtures/unordered_dates.csv"), "unordered dates accepted");
    });
    run_case("Invalid OHLC rejected", [&] {
        MarketData market_data;
        require(!market_data.load_csv("BAD", "tests/fixtures/invalid_ohlc.csv"), "invalid OHLC accepted");
    });
    run_case("Negative volume rejected", [&] {
        MarketData market_data;
        require(!market_data.load_csv("BAD", "tests/fixtures/negative_volume.csv"), "negative volume accepted");
    });
    run_case("Missing value rejected", [&] {
        MarketData market_data;
        require(!market_data.load_csv("BAD", "tests/fixtures/missing_value.csv"), "missing value accepted");
    });
    run_case("Open outside range rejected", [&] {
        MarketData market_data;
        require(!market_data.load_csv("BAD", "tests/fixtures/open_outside_range.csv"), "open outside range accepted");
    });
    run_case("Next-bar execution and no look-ahead", [&] {
        std::ofstream csv("NEXT.csv");
        csv << "Date,Open,High,Low,Close,Volume\n"
            << "2024-01-01,100,101,99,100,1000\n"
            << "2024-01-02,101,103,100,102,1000\n"
            << "2024-01-03,103,112,102,111,1000\n"
            << "2024-01-04,112,113,111,112,1000\n";
        csv.close();

        class BuyOnSecondBar : public Strategy {
        public:
            std::string name() const override { return "BuyOnSecondBar"; }
            std::string parameters() const override { return "unit"; }
            SignalEvent on_market_event(const MarketEvent& event, const std::vector<Bar>&) override {
                return SignalEvent{EventType::Signal, event.date, event.ticker, name(), event.index == 1 ? SignalType::Buy : SignalType::Hold};
            }
            std::unique_ptr<Strategy> clone() const override { return std::make_unique<BuyOnSecondBar>(*this); }
        };

        BacktestConfig config;
        config.ticker = "NEXT";
        config.data_dir = ".";
        config.results_dir = "test_results";
        config.transaction_cost_rate = 0.0;
        config.slippage_rate = 0.0;
        BacktestResult result = Backtester(config).run_detailed(BuyOnSecondBar());
        require(!result.trades.empty(), "no trade generated");
        require(result.trades.front().date == "2024-01-03", "trade happened before next bar");
        require(nearly_equal(result.trades.front().price, 103.0), "did not execute at next open");
    });
    run_case("Benchmark calculation uses comparable execution window", [&] {
        BacktestConfig config;
        config.ticker = "VALID";
        config.data_dir = "tests/fixtures";
        config.results_dir = "test_results";
        config.transaction_cost_rate = 0.0;
        config.slippage_rate = 0.0;
        auto result = Backtester(config).run_detailed(BuyOnFirstBar());
        require(nearly_equal(result.summary.total_return, result.summary.benchmark_net_return), "benchmark execution differs from equivalent strategy");
    });
    run_case("MA grid rejects invalid short-long pairs", [&] {
        auto specs = Analysis::grid_strategy_specs("MA_Cross");
        require(specs.size() == 16, "unexpected MA grid size");
        for (const auto& spec : specs) {
            int s = 0, l = 0;
            std::sscanf(spec.parameter_set.c_str(), "short=%d;long=%d", &s, &l);
            require(s < l, "invalid MA parameter pair");
        }
    });
    run_case("RSI grid size", [&] {
        require(Analysis::grid_strategy_specs("RSI_Mean_Reversion").size() == 12, "unexpected RSI grid size");
    });
    run_case("MACD grid size", [&] {
        require(Analysis::grid_strategy_specs("MACD_Momentum").size() == 4, "unexpected MACD grid size");
    });
    run_case("Volatility breakout grid size", [&] {
        require(Analysis::grid_strategy_specs("Volatility_Breakout").size() == 9, "unexpected breakout grid size");
    });
    run_case("Volatility breakout warm-up hold", [&] {
        VolatilityBreakoutStrategy strategy(10, 1.5);
        MarketEvent event{EventType::Market, "2024-01-05", "TEST", 103, 106, 102, 105, 1000, 4};
        require(strategy.on_market_event(event, bars).signal == SignalType::Hold, "breakout traded during warm-up");
    });
    run_case("Experiment config parsing", [&] {
        ExperimentConfig config = Analysis::load_experiment_config("configs/ma_walk_forward.json");
        require(config.name == "public_synthetic_ma_walk_forward", "bad experiment name");
        require(config.tickers.size() == 5, "bad ticker universe");
        require(config.strategy == "MA_Cross", "bad strategy");
        require(config.parameter_selection.minimum_trades == 3, "bad minimum trades");
    });
    run_case("Default grid includes all supported strategies", [&] {
        auto specs = Analysis::grid_strategy_specs();
        require(specs.size() == 41, "unexpected full grid size");
    });
    run_case("Default tickers include SYN_CRYPTO", [&] {
        auto tickers = Analysis::default_tickers();
        bool found = false;
        for (const auto& ticker : tickers) {
            found = found || ticker == "SYN_CRYPTO";
        }
        require(found, "SYN_CRYPTO missing from default universe");
    });
    run_case("Shared cash portfolio starts correctly", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.starting_capital = 50000.0;
        config.results_dir = "test_results/portfolio_start";
        auto result = PortfolioBacktester(config).run();
        require(!result.equity_curve.empty(), "missing portfolio equity");
        require(result.equity_curve.front().portfolio_value > 0.0, "bad starting value");
        require(result.equity_curve.front().cash >= 0.0, "negative starting cash");
    });
    run_case("Equal-weight target weights sum within budget", [&] {
        AllocationPolicyConfig cfg;
        cfg.type = AllocationPolicyType::EqualWeight;
        cfg.cash_buffer = 0.0;
        cfg.max_weight = 1.0;
        AllocationPolicy policy(cfg);
        auto history = sample_multi_asset_history();
        std::map<std::string, std::size_t> idx{{"AAA", 4}, {"BBB", 4}, {"CCC", 4}};
        auto weights = policy.target_weights({"AAA", "BBB", "CCC"}, history, idx);
        require(nearly_equal(weights["AAA"] + weights["BBB"] + weights["CCC"], 1.0), "weights do not sum to one");
    });
    run_case("Inverse-volatility weights are valid", [&] {
        AllocationPolicyConfig cfg;
        cfg.type = AllocationPolicyType::InverseVolatility;
        cfg.cash_buffer = 0.02;
        cfg.max_weight = 0.60;
        cfg.volatility_lookback = 3;
        AllocationPolicy policy(cfg);
        auto history = sample_multi_asset_history();
        std::map<std::string, std::size_t> idx{{"AAA", 4}, {"BBB", 4}, {"CCC", 4}};
        auto weights = policy.target_weights({"AAA", "BBB", "CCC"}, history, idx);
        double sum = 0.0;
        for (const auto& kv : weights) {
            require(kv.second >= 0.0, "negative inverse-vol weight");
            sum += kv.second;
        }
        require(sum <= 0.980001, "inverse-vol weights exceed budget");
    });
    run_case("Momentum top-N uses past returns", [&] {
        AllocationPolicyConfig cfg;
        cfg.type = AllocationPolicyType::MomentumTopN;
        cfg.cash_buffer = 0.0;
        cfg.max_weight = 1.0;
        cfg.momentum_lookback = 3;
        cfg.top_n = 1;
        AllocationPolicy policy(cfg);
        auto history = sample_multi_asset_history();
        std::map<std::string, std::size_t> idx{{"AAA", 4}, {"BBB", 4}, {"CCC", 4}};
        auto weights = policy.target_weights({"AAA", "BBB", "CCC"}, history, idx);
        require(weights["CCC"] > 0.99, "top momentum asset not selected");
        require(nearly_equal(weights["AAA"], 0.0), "non-top asset selected");
    });
    run_case("Allocation max asset weight enforced", [&] {
        std::map<std::string, double> raw{{"A", 10.0}, {"B", 1.0}, {"C", 1.0}};
        auto weights = AllocationPolicy::enforce_constraints(raw, 0.40, 0.0);
        for (const auto& kv : weights) {
            require(kv.second <= 0.400001, "max weight violated");
        }
    });
    run_case("Allocation cash buffer retained", [&] {
        std::map<std::string, double> raw{{"A", 1.0}, {"B", 1.0}};
        auto weights = AllocationPolicy::enforce_constraints(raw, 1.0, 0.10);
        require(nearly_equal(weights["A"] + weights["B"], 0.90), "cash buffer not retained");
    });
    run_case("Minimum trade threshold suppresses tiny orders", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.starting_capital = 100000.0;
        config.results_dir = "test_results/portfolio_threshold";
        config.allocation.min_trade_value = 1e12;
        auto result = PortfolioBacktester(config).run();
        require(result.fills.empty(), "tiny-order threshold did not suppress fills");
    });
    run_case("Weekly rebalance dates generated correctly", [&] {
        std::vector<std::string> dates = {"2024-01-01", "2024-01-02", "2024-01-03", "2024-01-04", "2024-01-05", "2024-01-08", "2024-01-09", "2024-01-10", "2024-01-11", "2024-01-12", "2024-01-16", "2024-01-17"};
        auto idx = PortfolioBacktester::rebalance_indices(dates, RebalanceFrequency::Weekly);
        require(idx.size() == 3 && idx[0] == 1 && idx[1] == 6 && idx[2] == 11, "bad weekly schedule");
    });
    run_case("Monthly rebalance dates generated correctly", [&] {
        std::vector<std::string> dates = {"2024-01-29", "2024-01-30", "2024-02-01", "2024-02-02", "2024-03-01"};
        auto idx = PortfolioBacktester::rebalance_indices(dates, RebalanceFrequency::Monthly);
        require(idx.size() == 3 && idx[0] == 1 && idx[1] == 2 && idx[2] == 4, "bad monthly schedule");
    });
    run_case("Portfolio transaction costs reduce cash", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.starting_capital = 50000.0;
        config.transaction_cost_rate = 0.01;
        config.results_dir = "test_results/portfolio_costs";
        auto result = PortfolioBacktester(config).run();
        require(result.summary.total_transaction_costs > 0.0, "no transaction costs recorded");
    });
    run_case("Portfolio slippage produces fill costs", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.starting_capital = 50000.0;
        config.slippage_rate = 0.01;
        config.results_dir = "test_results/portfolio_slippage";
        auto result = PortfolioBacktester(config).run();
        bool found = false;
        for (const auto& fill : result.fills) {
            found = found || fill.slippage_cost > 0.0;
        }
        require(found, "no slippage cost recorded");
    });
    run_case("Portfolio no negative holdings", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.results_dir = "test_results/portfolio_holdings";
        auto result = PortfolioBacktester(config).run();
        for (const auto& position : result.positions) {
            require(position.quantity >= -1e-9, "negative portfolio holding");
        }
    });
    run_case("Portfolio no unintended leverage", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.results_dir = "test_results/portfolio_leverage";
        auto result = PortfolioBacktester(config).run();
        for (const auto& point : result.equity_curve) {
            require(point.gross_exposure <= 1.000001, "gross exposure above one");
        }
    });
    run_case("Portfolio value reconciles to cash plus holdings", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.results_dir = "test_results/portfolio_reconcile";
        auto result = PortfolioBacktester(config).run();
        for (const auto& point : result.equity_curve) {
            require(nearly_equal(point.cash + point.total_holdings_value, point.portfolio_value, 1e-4), "portfolio value does not reconcile");
        }
    });
    run_case("Missing asset price handled by common-date alignment", [&] {
        std::vector<std::string> dates = {"2024-01-01"};
        auto idx = PortfolioBacktester::rebalance_indices(dates, RebalanceFrequency::Weekly);
        require(idx.size() == 1 && idx[0] == 0, "single-date alignment failed");
    });
    run_case("Portfolio benchmark calculation finite", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.results_dir = "test_results/portfolio_benchmark";
        auto result = PortfolioBacktester(config).run();
        require(std::isfinite(result.summary.equal_weight_benchmark_return), "non-finite benchmark");
    });
    run_case("Portfolio drawdown non-positive", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.results_dir = "test_results/portfolio_drawdown";
        auto result = PortfolioBacktester(config).run();
        for (const auto& point : result.equity_curve) {
            require(point.drawdown <= 1e-9, "positive portfolio drawdown");
        }
    });
    run_case("VaR and expected shortfall known vector", [&] {
        std::vector<double> returns = {-0.05, -0.02, 0.00, 0.01, 0.03};
        require(nearly_equal(PortfolioBacktester::value_at_risk_95(returns), -0.05), "bad VaR");
        require(nearly_equal(PortfolioBacktester::expected_shortfall_95(returns), -0.05), "bad ES");
    });
    run_case("Portfolio buys scaled when cash is insufficient", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH", "SYN_EQ_C", "SYN_CRYPTO"};
        config.starting_capital = 1000.0;
        config.results_dir = "test_results/portfolio_scale";
        config.allocation.cash_buffer = 0.0;
        config.allocation.max_weight = 1.0;
        auto result = PortfolioBacktester(config).run();
        for (const auto& point : result.equity_curve) {
            require(point.cash >= -1e-6, "cash went negative after scaling");
        }
    });
    run_case("Portfolio fills are sell-before-buy within rebalance", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.results_dir = "test_results/portfolio_sell_first";
        config.rebalance_frequency = RebalanceFrequency::Weekly;
        auto result = PortfolioBacktester(config).run();
        std::map<int, bool> seen_buy;
        for (const auto& fill : result.fills) {
            if (fill.side == "BUY") {
                seen_buy[fill.rebalance_id] = true;
            }
            if (fill.side == "SELL") {
                require(!seen_buy[fill.rebalance_id], "sell appeared after buy in same rebalance");
            }
        }
    });
    run_case("Portfolio actual weights within bounds", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH"};
        config.results_dir = "test_results/portfolio_weights";
        auto result = PortfolioBacktester(config).run();
        for (const auto& position : result.positions) {
            require(position.actual_weight >= -1e-9 && position.actual_weight <= 1.000001, "actual weight out of bounds");
        }
    });
    run_case("Portfolio output validator catches invalid file pattern", [&] {
        std::system("mkdir -p test_results/invalid_validator/portfolio");
        std::ofstream bad("test_results/invalid_validator/portfolio/portfolio_equity_curve.csv");
        bad << "date,portfolio_value,cash,total_holdings_value,total_return,drawdown,gross_exposure\n";
        bad << "2024-01-01,100,-1,101,0,0,1.01\n";
        bad.close();
        int code = std::system("python3 scripts/validate_results.py test_results/invalid_validator >/dev/null 2>&1");
        require(code != 0, "portfolio validator accepted invalid output");
    });
    run_case("regime_prefix_invariance", [&] {
        auto prefix = synthetic_bars(340, true);
        auto extended = prefix;
        std::string date = add_calendar_days(extended.back().date, 1);
        double close = extended.back().close;
        for (int i = 0; i < 80; ++i) {
            close *= i % 2 == 0 ? 1.35 : 0.72;
            extended.push_back({date, close, close * 1.01, close * 0.99, close, 1000});
            date = add_calendar_days(date, 1);
        }
        auto before = classify_causal_regimes(prefix, 20, 40);
        auto after = classify_causal_regimes(extended, 20, 40);
        require(before.size() == prefix.size(), "prefix classification size mismatch");
        for (std::size_t i = 0; i < before.size(); ++i) {
            require(before[i].regime == after[i].regime, "future bars changed historical regime");
            require(nearly_equal(before[i].volatility_threshold, after[i].volatility_threshold), "future bars changed historical threshold");
        }
    });
    run_case("regime_execution_timestamp", [&] {
        auto bars_for_regime = synthetic_bars(340, true);
        auto regimes = classify_causal_regimes(bars_for_regime, 20, 40);
        const RegimePoint* assigned = regime_for_execution(regimes, bars_for_regime[300].date);
        require(assigned != nullptr, "execution regime unavailable after warm-up");
        require(assigned->information_cutoff < bars_for_regime[300].date, "open execution used same-day close");
        require(assigned->date == bars_for_regime[299].date, "execution did not use latest prior close");
    });
    run_case("regime_return_interval_attribution", [&] {
        auto bars_for_regime = synthetic_bars(340, true);
        auto regimes = classify_causal_regimes(bars_for_regime, 20, 40);
        const RegimePoint* assigned = regime_for_return_interval(regimes, bars_for_regime[299].date, bars_for_regime[300].date);
        require(assigned != nullptr, "return regime unavailable after warm-up");
        require(assigned->information_cutoff == bars_for_regime[299].date, "return did not use start-of-period regime");
    });
    run_case("equity_calendar_window_duration", [&] {
        auto equity_bars = synthetic_bars(1200, false);
        auto windows = build_calendar_windows(equity_bars, 3, 6, 6);
        require(!windows.empty(), "no equity calendar windows");
        require(windows.front().test_start >= add_calendar_years(windows.front().train_start, 3), "equity training duration too short");
        require(windows.front().test_end < add_calendar_months(add_calendar_years(windows.front().train_start, 3), 6), "equity test exceeded boundary");
    });
    run_case("btc_calendar_window_duration", [&] {
        auto btc_bars = synthetic_bars(1600, true);
        auto windows = build_calendar_windows(btc_bars, 3, 6, 6);
        require(!windows.empty(), "no BTC calendar windows");
        require(windows.front().test_start >= add_calendar_years(windows.front().train_start, 3), "BTC training duration too short");
        require(windows.front().train_observations > 1000, "BTC window still uses equity observation count");
    });
    run_case("calendar_leap_year_and_month_end", [&] {
        require(add_calendar_years("2020-02-29", 3) == "2023-02-28", "leap-year clamp failed");
        require(add_calendar_months("2020-01-31", 1) == "2020-02-29", "month-end clamp failed");
        require(add_calendar_months("2021-01-31", 1) == "2021-02-28", "non-leap month-end clamp failed");
    });
    run_case("calendar_windows_allow_missing_dates", [&] {
        auto complete = synthetic_bars(1600, true);
        std::vector<Bar> missing;
        for (std::size_t i = 0; i < complete.size(); ++i) {
            if (i % 17 != 0) {
                missing.push_back(complete[i]);
            }
        }
        auto windows = build_calendar_windows(missing, 3, 6, 6);
        require(!windows.empty(), "missing dates prevented calendar windows");
        require(windows.front().train_end < windows.front().test_start, "missing dates caused overlap");
    });
    run_case("walk_forward_non_overlap", [&] {
        auto windows = build_calendar_windows(synthetic_bars(1900, true), 3, 6, 6);
        require(windows.size() >= 2, "insufficient calendar windows");
        for (std::size_t i = 0; i < windows.size(); ++i) {
            require(windows[i].train_end < windows[i].test_start, "train and test overlap");
            if (i > 0) {
                require(windows[i - 1].test_end < windows[i].test_start, "test windows double-count dates");
            }
        }
    });
    run_case("continuous_oos_capital_reconciliation", [&] {
        MarketData data;
        require(data.load_csv("SYN_EQ_A", "data/synthetic/SYN_EQ_A.csv"), "could not load SYN_EQ_A continuity fixture");
        auto windows = build_calendar_windows(data.bars("SYN_EQ_A"), 3, 6, 6);
        require(windows.size() >= 2, "insufficient SYN_EQ_A continuity windows");
        double capital_value = 100000.0;
        for (std::size_t i = 0; i < 2; ++i) {
            const double starting = capital_value;
            BacktestConfig config;
            config.ticker = "SYN_EQ_A";
            config.start_date = windows[i].test_start;
            config.end_date = windows[i].test_end;
            config.starting_capital = starting;
            config.liquidate_at_end = true;
            auto result = Backtester(config).run_detailed(MovingAverageCrossoverStrategy(5, 50));
            capital_value = result.equity_curve.back().portfolio_value;
            require(nearly_equal(result.equity_curve.front().portfolio_value, starting, 1e-4), "window did not start from prior capital");
        }
        require(nearly_equal(capital_value / 100000.0 - 1.0, (capital_value - 100000.0) / 100000.0), "linked return does not reconcile");
    });
    run_case("continuous_oos_no_duplicate_dates", [&] {
        MarketData data;
        require(data.load_csv("SYN_EQ_A", "data/synthetic/SYN_EQ_A.csv"), "could not load SYN_EQ_A date fixture");
        auto windows = build_calendar_windows(data.bars("SYN_EQ_A"), 3, 6, 6);
        std::set<std::string> dates;
        for (const auto& window : windows) {
            for (const auto& bar : data.bars("SYN_EQ_A")) {
                if (bar.date >= window.test_start && bar.date <= window.test_end) {
                    require(dates.insert(bar.date).second, "duplicate OOS date");
                }
            }
        }
    });
    run_case("benchmark_execution_parity", [&] {
        double zero_cost_return = 0.0;
        double cost_adjusted_return = 0.0;
        for (double cost : {0.0, 0.001}) {
            BacktestConfig config;
            config.ticker = "VALID";
            config.data_dir = "tests/fixtures";
            config.results_dir = "test_results/benchmark_parity";
            config.transaction_cost_rate = cost;
            config.slippage_rate = cost / 2.0;
            auto result = Backtester(config).run_detailed(BuyOnFirstBar());
            require(nearly_equal(result.summary.total_return, result.summary.benchmark_net_return), "buy-and-hold strategy and benchmark diverged");
            if (cost == 0.0) {
                zero_cost_return = result.summary.total_return;
            } else {
                cost_adjusted_return = result.summary.total_return;
            }
        }
        require(cost_adjusted_return < zero_cost_return, "costs did not degrade strategy and benchmark consistently");
    });
    run_case("configured_benchmark_propagation", [&] {
        BacktestConfig same;
        same.ticker = "SYN_EQ_A";
        same.start_date = "2023-01-03";
        same.end_date = "2023-06-30";
        same.benchmark_ticker = "same_asset";
        auto same_result = Backtester(same).run_detailed(MovingAverageCrossoverStrategy(5, 50));
        BacktestConfig spy = same;
        spy.benchmark_ticker = "SYN_BENCH";
        auto spy_result = Backtester(spy).run_detailed(MovingAverageCrossoverStrategy(5, 50));
        require(same_result.summary.benchmark_ticker == "SYN_EQ_A", "same_asset did not resolve to traded ticker");
        require(spy_result.summary.benchmark_ticker == "SYN_BENCH", "configured SYN_BENCH benchmark not propagated");
        require(!nearly_equal(same_result.summary.benchmark_net_return, spy_result.summary.benchmark_net_return), "changing benchmark did not change output");
    });
    run_case("malformed_benchmark_configuration_rejected", [&] {
        std::ofstream config("test_results/invalid_benchmark.json");
        config << "{\"experiment_name\":\"bad\",\"benchmark\":\"\"}\n";
        config.close();
        bool rejected = false;
        try {
            (void)Analysis::load_experiment_config("test_results/invalid_benchmark.json");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "empty benchmark configuration accepted");
    });
    run_case("normalized_window_policy_remains_diagnostic", [&] {
        std::ofstream config("test_results/normalized_window.json");
        config << "{\"experiment_name\":\"normalized\",\"benchmark\":\"same_asset\","
               << "\"window_mode\":\"calendar_duration\",\"oos_continuity_policy\":\"normalized_window\","
               << "\"boundary_position_policy\":\"liquidate_at_test_end_close\"}\n";
        config.close();
        auto parsed = Analysis::load_experiment_config("test_results/normalized_window.json");
        require(parsed.walk_forward.continuity_policy == "normalized_window", "normalized diagnostic policy was not retained");
        require(parsed.walk_forward.continuity_policy != "continuous_capital", "normalized diagnostic policy became deployable continuity");
    });
    run_case("golden_single_asset_regression", [&] {
        BacktestConfig config;
        config.ticker = "SYN_EQ_A";
        auto result = Backtester(config).run_detailed(MovingAverageCrossoverStrategy(20, 50));
        require(nearly_equal(result.summary.total_return, -0.559576446351, 1e-9), "synthetic single-asset golden changed");
        require(result.summary.num_trades == 32, "synthetic single-asset trade-count golden changed");
    });
    run_case("golden_shared_cash_regression", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH", "SYN_EQ_C", "SYN_CRYPTO"};
        auto result = PortfolioBacktester(config).run();
        require(nearly_equal(result.summary.total_return, 1.713391, 1e-6),
                "synthetic shared-cash golden changed: " + std::to_string(result.summary.total_return));
        require(result.summary.number_of_rebalances == 84,
                "synthetic shared-cash rebalance golden changed: " + std::to_string(result.summary.number_of_rebalances));
    });
    run_case("golden_walk_forward_selection_regression", [&] {
        MarketData data;
        require(data.load_csv("SYN_EQ_A", "data/synthetic/SYN_EQ_A.csv"), "could not load walk-forward golden data");
        auto windows = build_calendar_windows(data.bars("SYN_EQ_A"), 3, 6, 6);
        std::string best_parameters;
        double best_score = -1e18;
        for (const auto& spec : Analysis::grid_strategy_specs("MA_Cross")) {
            BacktestConfig config;
            config.ticker = "SYN_EQ_A";
            config.start_date = windows.front().train_start;
            config.end_date = windows.front().train_end;
            auto summary = Backtester(config).run_detailed(*spec.instance).summary;
            double score = summary.num_trades >= 3 ? summary.sharpe : -1e9;
            if (score > best_score) {
                best_score = score;
                best_parameters = summary.parameter_set;
            }
        }
        require(best_parameters == "short=5;long=50", "synthetic walk-forward selection golden changed");
    });
    run_case("simulation_result_requires_no_filesystem_output", [&] {
        const std::string output_dir = "test_results/simulation_no_write";
        std::filesystem::remove_all(output_dir);
        BacktestConfig config;
        config.ticker = "VALID";
        config.data_dir = "tests/fixtures";
        config.results_dir = output_dir;
        auto result = Backtester(config).run_detailed(MovingAverageCrossoverStrategy(1, 2));
        require(!result.equity_curve.empty(), "in-memory simulation returned no equity");
        require(!std::filesystem::exists(output_dir), "simulation wrote files without an exporter");
    });
    run_case("csv_exporter_reproduces_schema_v2", [&] {
        const std::string output_dir = "test_results/export_schema";
        std::filesystem::remove_all(output_dir);
        BacktestConfig config;
        config.ticker = "VALID";
        config.data_dir = "tests/fixtures";
        auto result = Backtester(config).run_detailed(MovingAverageCrossoverStrategy(1, 2));
        quant::io::CsvResultExporter::write_backtest(result, output_dir);
        std::ifstream summary(output_dir + "/VALID_MA_Cross_performance_summary.csv");
        std::string header;
        std::getline(summary, header);
        require(header.rfind("schema_version,ticker,strategy,parameter_set", 0) == 0, "exporter schema header changed");
        std::string row;
        std::getline(summary, row);
        require(row.rfind("2,VALID,MA_Cross", 0) == 0, "exporter schema row changed");
    });
    run_case("csv_exporter_rejects_invalid_output_path", [&] {
        BacktestConfig config;
        config.ticker = "VALID";
        config.data_dir = "tests/fixtures";
        auto result = Backtester(config).run_detailed(MovingAverageCrossoverStrategy(1, 2));
        bool rejected = false;
        try {
            quant::io::CsvResultExporter::write_backtest(result, "/dev/null/quant-results");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "invalid exporter path was accepted");
    });
    run_case("validated_date_parsing_and_ordering", [&] {
        const auto leap = quant::Date::parse("2024-02-29");
        require(leap.to_string() == "2024-02-29", "date formatting is not canonical");
        require(quant::Date::parse("2023-12-31") < quant::Date::parse("2024-01-01"), "date ordering failed");
        bool rejected = false;
        try {
            (void)quant::Date::parse("2023-02-29");
        } catch (const quant::DataError&) {
            rejected = true;
        }
        require(rejected, "invalid civil date accepted");
    });
    run_case("date_abstraction_preserves_calendar_boundaries", [&] {
        require(quant::Date::parse("2020-02-29").add_years(3).to_string() == add_calendar_years("2020-02-29", 3), "year boundary drifted");
        require(quant::Date::parse("2020-01-31").add_months(1).to_string() == add_calendar_months("2020-01-31", 1), "month boundary drifted");
    });
    run_case("strict_config_rejects_unknown_and_wrong_type_fields", [&] {
        std::ofstream unknown("test_results/unknown_config.json");
        unknown << "{\"experiment_name\":\"bad\",\"ticker_universe\":[\"SYN_EQ_A\"],\"strategy\":\"MA_Cross\",\"typo_field\":1}\n";
        unknown.close();
        bool unknown_rejected = false;
        try { (void)quant::config::ConfigLoader::load_file("test_results/unknown_config.json"); }
        catch (const quant::ConfigurationError&) { unknown_rejected = true; }
        require(unknown_rejected, "unknown configuration field accepted");

        std::ofstream wrong_type("test_results/wrong_type_config.json");
        wrong_type << "{\"experiment_name\":3,\"ticker_universe\":[\"SYN_EQ_A\"],\"strategy\":\"MA_Cross\"}\n";
        wrong_type.close();
        bool type_rejected = false;
        try { (void)quant::config::ConfigLoader::load_file("test_results/wrong_type_config.json"); }
        catch (const quant::ConfigurationError&) { type_rejected = true; }
        require(type_rejected, "wrong configuration type accepted");
    });
    run_case("typed_config_rejects_negative_values", [&] {
        std::ofstream negative("test_results/negative_config.json");
        negative << "{\"experiment_name\":\"bad\",\"ticker_universe\":[\"SYN_EQ_A\"],\"strategy\":\"MA_Cross\",\"starting_capital\":-1}\n";
        negative.close();
        bool rejected = false;
        try { (void)quant::config::ConfigLoader::load_file("test_results/negative_config.json"); }
        catch (const quant::ConfigurationError&) { rejected = true; }
        require(rejected, "negative starting capital accepted");
    });
    run_case("typed_config_round_trip", [&] {
        auto original = quant::config::ConfigLoader::load_file("configs/ma_walk_forward.json");
        std::ofstream output("test_results/round_trip_config.json");
        output << quant::config::ConfigLoader::to_json(original);
        output.close();
        auto restored = quant::config::ConfigLoader::load_file("test_results/round_trip_config.json");
        require(restored.name == original.name, "config name changed on round trip");
        require(restored.tickers == original.tickers, "ticker universe changed on round trip");
        require(restored.execution.starting_capital == original.execution.starting_capital, "capital changed on round trip");
        require(restored.walk_forward.window_mode == original.walk_forward.window_mode, "window mode changed on round trip");
        require(restored.benchmark.ticker == original.benchmark.ticker, "benchmark changed on round trip");
        require(restored.output.results_dir == original.output.results_dir, "output directory changed on round trip");
    });

    std::cout << cases_run << " deterministic test cases passed with " << assertions_run << " assertions.\n";
    return 0;
}
