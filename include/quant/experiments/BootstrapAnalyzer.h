#pragma once

#include "Backtester.h"
#include "quant/config/ExperimentConfig.h"

#include <cstddef>
#include <vector>

namespace quant::experiments {

struct BootstrapPathPoint { int path{0}; std::size_t step{0}; double equity{0.0}; };
struct BootstrapMetric {
    int path{0};
    double total_return{0.0};
    double terminal_wealth{0.0};
    double max_drawdown{0.0};
    double sharpe{0.0};
};
struct BootstrapResult {
    unsigned int seed{0};
    int path_count{0};
    double terminal_wealth_p05{0.0};
    double terminal_wealth_p50{0.0};
    double terminal_wealth_p95{0.0};
    double probability_of_loss{0.0};
    std::vector<BootstrapPathPoint> sampled_paths;
    std::vector<BootstrapMetric> metrics;
};

class BootstrapAnalyzer {
public:
    static BootstrapResult run(const BacktestResult& source, const config::BootstrapConfig& config,
                               double starting_capital, int path_count = 1000, int retained_path_count = 20);
};

}  // namespace quant::experiments
