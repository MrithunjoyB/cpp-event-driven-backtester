#include "Backtester.h"
#include "Strategy.h"

#include <iomanip>
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
}

int main(int argc, char** argv) {
    try {
        std::string ticker = get_arg(argc, argv, "--ticker", "");
        std::string strategy_name = get_arg(argc, argv, "--strategy", "");
        double capital = get_double_arg(argc, argv, "--capital", 100000.0);
        double transaction_cost = get_double_arg(argc, argv, "--transaction-cost", 0.001);
        double slippage = get_double_arg(argc, argv, "--slippage", 0.0005);

        std::vector<PerformanceSummary> summaries;
        std::cout << std::fixed << std::setprecision(4);
        std::cout << std::left << std::setw(8) << "Ticker"
                  << std::setw(20) << "Strategy"
                  << std::right << std::setw(12) << "Return"
                  << std::setw(12) << "Sharpe"
                  << std::setw(14) << "MaxDD"
                  << std::setw(12) << "WinRate"
                  << std::setw(8) << "Trades" << '\n';

        if (!ticker.empty() && !strategy_name.empty()) {
            BacktestConfig config{ticker, capital, transaction_cost, slippage};
            Backtester backtester(config);
            auto strategy = make_strategy(strategy_name);
            auto summary = backtester.run(*strategy);
            summaries.push_back(summary);
            print_summary(summary);
        } else {
            std::vector<std::string> tickers = {"AAPL", "MSFT", "SPY"};
            std::vector<std::string> strategies = {"ma_cross", "rsi", "macd"};
            for (const auto& t : tickers) {
                for (const auto& s : strategies) {
                    BacktestConfig config{t, capital, transaction_cost, slippage};
                    Backtester backtester(config);
                    auto strategy = make_strategy(s);
                    auto summary = backtester.run(*strategy);
                    summaries.push_back(summary);
                    print_summary(summary);
                }
            }
        }

        Backtester::write_combined_summary("results/strategy_comparison.csv", summaries);
        std::cout << "\nResults written to results/.\n";
    } catch (const std::exception& ex) {
        std::cerr << "Backtest failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}

