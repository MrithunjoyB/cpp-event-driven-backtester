#include "Backtester.h"
#include "Strategy.h"
#include "quant/performance/DeterministicExecutor.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

int main() {
    int cases = 0;
    auto check = [&](bool condition, const char* message) {
        ++cases;
        if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
    };
    using quant::performance::DeterministicExecutor;
    check(DeterministicExecutor::effective_threads("serial", 4) == 1, "serial forces one worker");
    check(DeterministicExecutor::effective_threads("parallel", 2) == 2, "two workers retained");
    check(DeterministicExecutor::effective_threads("parallel", 4) == 4, "four workers retained");
    bool zero_rejected = false; try { (void)DeterministicExecutor::effective_threads("parallel", 0); } catch (...) { zero_rejected = true; }
    check(zero_rejected, "zero threads rejected");
    bool high_rejected = false; try { (void)DeterministicExecutor::effective_threads("parallel", 65); } catch (...) { high_rejected = true; }
    check(high_rejected, "unbounded worker count rejected");
    bool mode_rejected = false; try { (void)DeterministicExecutor::effective_threads("automatic", 2); } catch (...) { mode_rejected = true; }
    check(mode_rejected, "unknown execution mode rejected");
    check(DeterministicExecutor::effective_threads("parallel", 64) == 64, "bounded maximum accepted");

    auto run = [](int threads) {
        DeterministicExecutor executor(threads == 1 ? "serial" : "parallel", threads);
        return executor.map(32, [](std::size_t index) { return index * index; });
    };
    const auto serial = run(1);
    check(run(2) == serial, "two-thread result order equals serial");
    check(run(4) == serial, "four-thread result order equals serial");
    check(run(8) == serial, "eight-thread result order equals serial");
    check(serial.front() == 0 && serial.back() == 961, "stable task enumeration");
    check(DeterministicExecutor("parallel", 4).map(0, [](std::size_t value) { return value; }).empty(), "empty task set");
    bool propagated = false;
    try {
        (void)DeterministicExecutor("parallel", 4).map(16, [](std::size_t index) {
            if (index == 5) throw std::runtime_error("fixture failure"); return index;
        });
    } catch (const std::runtime_error&) { propagated = true; }
    check(propagated, "worker exception propagated");
    for (int repetition = 0; repetition < 20; ++repetition) {
        check(run(4) == serial, "repeated parallel result remains deterministic");
    }

    auto data = std::make_shared<MarketData>();
    check(data->load_csv("VALID", "tests/fixtures/VALID.csv"), "fixture market data loads");
    std::shared_ptr<const MarketData> immutable = data;
    BacktestConfig cached;
    cached.ticker = "VALID"; cached.data_dir = "tests/fixtures"; cached.benchmark_ticker = "same_asset";
    cached.immutable_market_data = immutable;
    BacktestConfig uncached = cached; uncached.immutable_market_data.reset();
    MovingAverageCrossoverStrategy strategy(2, 3);
    const auto cached_result = Backtester(cached).run_detailed(strategy);
    const auto uncached_result = Backtester(uncached).run_detailed(strategy);
    check(cached_result.summary.total_return == uncached_result.summary.total_return, "immutable data reuse preserves return");
    check(cached_result.equity_curve.size() == uncached_result.equity_curve.size(), "immutable data reuse preserves observations");
    check(&immutable->bars("VALID") == &immutable->bars("VALID"), "immutable cache identity stable");
    bool missing_rejected = false;
    try { BacktestConfig bad = cached; bad.ticker = "MISSING"; (void)Backtester(bad).run_detailed(strategy); }
    catch (const std::runtime_error&) { missing_rejected = true; }
    check(missing_rejected, "cache path/ticker separation");
    MarketData malformed;
    check(!malformed.load_csv("BAD", "tests/fixtures/malformed_row.csv"), "malformed data behavior unchanged");

    auto benchmark = std::make_shared<const BenchmarkResult>(BenchmarkResult{0.1, 0.09, "VALID", "fixture", "fixture"});
    BacktestConfig overridden = cached; overridden.benchmark_override = benchmark;
    const auto override_result = Backtester(overridden).run_detailed(strategy);
    check(override_result.summary.benchmark_net_return == 0.09, "benchmark override retained");
    check(override_result.summary.benchmark_gross_return == 0.1, "benchmark gross override retained");

    std::cout << cases << " performance/concurrency cases passed\n";
    return 0;
}
