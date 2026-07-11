#include "quant/experiments/BootstrapAnalyzer.h"

#include "quant/domain/Errors.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace quant::experiments {
namespace {
std::vector<double> equity_returns(const std::vector<EquityPoint>& equity) {
    std::vector<double> returns;
    if (equity.size() > 1) returns.reserve(equity.size() - 1);
    for (std::size_t i = 1; i < equity.size(); ++i) {
        if (equity[i - 1].portfolio_value <= 0.0) throw SimulationError("Bootstrap source contains non-positive equity");
        returns.push_back(equity[i].portfolio_value / equity[i - 1].portfolio_value - 1.0);
    }
    return returns;
}
double mean(const std::vector<double>& values) {
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}
double stdev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double average = mean(values);
    double sum = 0.0;
    for (double value : values) sum += (value - average) * (value - average);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}
double percentile(std::vector<double> values, double probability) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(probability * static_cast<double>(values.size() - 1))];
}
}

BootstrapResult BootstrapAnalyzer::run(const BacktestResult& source, const config::BootstrapConfig& config,
                                       double starting_capital, int path_count, int retained_path_count) {
    if (!std::isfinite(starting_capital) || starting_capital <= 0.0) throw ConfigurationError("bootstrap starting_capital must be positive and finite");
    if (path_count <= 0 || retained_path_count < 0) throw ConfigurationError("bootstrap path counts are invalid");
    const auto returns = equity_returns(source.equity_curve);
    if (returns.empty()) throw SimulationError("Bootstrap requires at least two equity observations");

    BootstrapResult result;
    result.seed = config.random_seed;
    result.path_count = path_count;
    result.metrics.reserve(static_cast<std::size_t>(path_count));
    result.sampled_paths.reserve(static_cast<std::size_t>(std::min(path_count, retained_path_count)) * returns.size());
    std::vector<double> terminal;
    terminal.reserve(static_cast<std::size_t>(path_count));
    std::mt19937 rng(config.random_seed);
    std::uniform_int_distribution<std::size_t> pick(0, returns.size() - 1);
    for (int path = 0; path < path_count; ++path) {
        double equity = starting_capital;
        double peak = equity;
        double max_drawdown = 0.0;
        std::vector<double> sampled;
        sampled.reserve(returns.size());
        for (std::size_t step = 0; step < returns.size(); ++step) {
            const double daily_return = returns[pick(rng)];
            sampled.push_back(daily_return);
            equity *= 1.0 + daily_return;
            peak = std::max(peak, equity);
            max_drawdown = std::min(max_drawdown, equity / peak - 1.0);
            if (path < retained_path_count) result.sampled_paths.push_back({path, step, equity});
        }
        const double volatility = stdev(sampled);
        const double sharpe = volatility > 0.0 ? mean(sampled) * std::sqrt(252.0) / volatility : 0.0;
        terminal.push_back(equity);
        result.metrics.push_back({path, equity / starting_capital - 1.0, equity, max_drawdown, sharpe});
    }
    const auto losses = std::count_if(terminal.begin(), terminal.end(), [starting_capital](double wealth) { return wealth < starting_capital; });
    result.terminal_wealth_p05 = percentile(terminal, 0.05);
    result.terminal_wealth_p50 = percentile(terminal, 0.50);
    result.terminal_wealth_p95 = percentile(terminal, 0.95);
    result.probability_of_loss = static_cast<double>(losses) / static_cast<double>(path_count);
    return result;
}

}  // namespace quant::experiments
