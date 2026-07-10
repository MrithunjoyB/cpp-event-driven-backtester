#include "Backtester.h"
#include "Analysis.h"
#include "ExecutionHandler.h"
#include "MarketData.h"
#include "Metrics.h"
#include "Portfolio.h"
#include "Strategy.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int cases_run = 0;

bool nearly_equal(double lhs, double rhs, double tolerance = 1e-6) {
    return std::fabs(lhs - rhs) <= tolerance;
}

void require(bool condition, const std::string& message) {
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
        require(market_data.load_csv("VALID", "tests/fixtures/valid.csv"), "valid fixture rejected");
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
        BacktestResult result = Backtester(config).run_detailed(BuyOnSecondBar(), false);
        require(!result.trades.empty(), "no trade generated");
        require(result.trades.front().date == "2024-01-03", "trade happened before next bar");
        require(nearly_equal(result.trades.front().price, 103.0), "did not execute at next open");
    });
    run_case("Benchmark calculation uses same window", [&] {
        BacktestConfig config;
        config.ticker = "VALID";
        config.data_dir = "tests/fixtures";
        config.results_dir = "test_results";
        config.transaction_cost_rate = 0.0;
        config.slippage_rate = 0.0;
        MovingAverageCrossoverStrategy strategy(1, 2);
        auto result = Backtester(config).run_detailed(strategy, false);
        require(nearly_equal(result.summary.benchmark_gross_return, (107.0 / 103.0) - 1.0), "bad benchmark window");
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
        require(config.experiment_name == "ma_walk_forward", "bad experiment name");
        require(config.tickers.size() == 5, "bad ticker universe");
        require(config.strategy == "MA_Cross", "bad strategy");
        require(config.minimum_trades == 3, "bad minimum trades");
    });
    run_case("Default grid includes all supported strategies", [&] {
        auto specs = Analysis::grid_strategy_specs();
        require(specs.size() == 41, "unexpected full grid size");
    });
    run_case("Default tickers include BTC-USD", [&] {
        auto tickers = Analysis::default_tickers();
        bool found = false;
        for (const auto& ticker : tickers) {
            found = found || ticker == "BTC-USD";
        }
        require(found, "BTC-USD missing from default universe");
    });

    std::cout << cases_run << " deterministic test cases passed.\n";
    return 0;
}
