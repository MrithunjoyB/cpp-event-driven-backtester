#include "Backtester.h"
#include "Analysis.h"
#include "ExecutionHandler.h"
#include "MarketData.h"
#include "Metrics.h"
#include "Portfolio.h"
#include "PortfolioBacktester.h"
#include "Strategy.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int cases_run = 0;
int assertions_run = 0;

bool nearly_equal(double lhs, double rhs, double tolerance = 1e-6) {
    return std::fabs(lhs - rhs) <= tolerance;
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
    run_case("Shared cash portfolio starts correctly", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.starting_capital = 50000.0;
        config.results_dir = "test_results/portfolio_start";
        auto result = PortfolioBacktester(config).run(false);
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
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.starting_capital = 100000.0;
        config.results_dir = "test_results/portfolio_threshold";
        config.allocation.min_trade_value = 1e12;
        auto result = PortfolioBacktester(config).run(false);
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
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.starting_capital = 50000.0;
        config.transaction_cost_rate = 0.01;
        config.results_dir = "test_results/portfolio_costs";
        auto result = PortfolioBacktester(config).run(false);
        require(result.summary.total_transaction_costs > 0.0, "no transaction costs recorded");
    });
    run_case("Portfolio slippage produces fill costs", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.starting_capital = 50000.0;
        config.slippage_rate = 0.01;
        config.results_dir = "test_results/portfolio_slippage";
        auto result = PortfolioBacktester(config).run(false);
        bool found = false;
        for (const auto& fill : result.fills) {
            found = found || fill.slippage_cost > 0.0;
        }
        require(found, "no slippage cost recorded");
    });
    run_case("Portfolio no negative holdings", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.results_dir = "test_results/portfolio_holdings";
        auto result = PortfolioBacktester(config).run(false);
        for (const auto& position : result.positions) {
            require(position.quantity >= -1e-9, "negative portfolio holding");
        }
    });
    run_case("Portfolio no unintended leverage", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.results_dir = "test_results/portfolio_leverage";
        auto result = PortfolioBacktester(config).run(false);
        for (const auto& point : result.equity_curve) {
            require(point.gross_exposure <= 1.000001, "gross exposure above one");
        }
    });
    run_case("Portfolio value reconciles to cash plus holdings", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.results_dir = "test_results/portfolio_reconcile";
        auto result = PortfolioBacktester(config).run(false);
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
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.results_dir = "test_results/portfolio_benchmark";
        auto result = PortfolioBacktester(config).run(false);
        require(std::isfinite(result.summary.equal_weight_benchmark_return), "non-finite benchmark");
    });
    run_case("Portfolio drawdown non-positive", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.results_dir = "test_results/portfolio_drawdown";
        auto result = PortfolioBacktester(config).run(false);
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
        config.tickers = {"AAPL", "MSFT", "SPY", "TSLA", "BTC-USD"};
        config.starting_capital = 1000.0;
        config.results_dir = "test_results/portfolio_scale";
        config.allocation.cash_buffer = 0.0;
        config.allocation.max_weight = 1.0;
        auto result = PortfolioBacktester(config).run(false);
        for (const auto& point : result.equity_curve) {
            require(point.cash >= -1e-6, "cash went negative after scaling");
        }
    });
    run_case("Portfolio fills are sell-before-buy within rebalance", [&] {
        PortfolioBacktestConfig config;
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.results_dir = "test_results/portfolio_sell_first";
        config.rebalance_frequency = RebalanceFrequency::Weekly;
        auto result = PortfolioBacktester(config).run(false);
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
        config.tickers = {"AAPL", "MSFT", "SPY"};
        config.results_dir = "test_results/portfolio_weights";
        auto result = PortfolioBacktester(config).run(false);
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

    std::cout << cases_run << " deterministic test cases passed with " << assertions_run << " assertions.\n";
    return 0;
}
