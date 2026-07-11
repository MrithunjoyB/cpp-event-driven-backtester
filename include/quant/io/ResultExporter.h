#pragma once

#include "Backtester.h"
#include "PortfolioBacktester.h"
#include "quant/config/ExperimentConfig.h"
#include "quant/experiments/BootstrapAnalyzer.h"

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
    static void write_bootstrap(
        const quant::experiments::BootstrapResult& result,
        const std::string& directory);
};

class JsonManifestExporter {
public:
    static void write_text(const std::string& filepath, const std::string& json);
    static void write_resolved_config(
        const std::string& filepath,
        const quant::config::ExperimentConfig& config);
};

}  // namespace quant::io
