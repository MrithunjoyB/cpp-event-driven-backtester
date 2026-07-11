#pragma once

#include "Backtester.h"
#include "PortfolioBacktester.h"

#include <string>
#include <vector>

namespace quant::io {

inline constexpr int kResultSchemaVersion = 2;

class CsvResultExporter {
public:
    static void write_backtest(const BacktestResult& result, const std::string& results_dir);
    static void write_combined_summary(
        const std::string& filepath,
        const std::vector<PerformanceSummary>& summaries);
    static void write_portfolio(
        const PortfolioBacktestResult& result,
        const PortfolioBacktestConfig& config);
};

}  // namespace quant::io
