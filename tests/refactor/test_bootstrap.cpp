#include "quant/experiments/BootstrapAnalyzer.h"
#include "quant/io/ResultExporter.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    BacktestResult source;
    source.equity_curve = {
        {"2024-01-01", 100.0, 100.0, 0.0, 0.0, 0.0},
        {"2024-01-02", 101.0, 101.0, 0.0, 0.01, 0.0},
        {"2024-01-03", 99.0, 99.0, 0.0, -0.01, -0.019802},
        {"2024-01-04", 102.0, 102.0, 0.0, 0.02, 0.0}};
    quant::config::BootstrapConfig config;
    config.random_seed = 17;
    const auto first = quant::experiments::BootstrapAnalyzer::run(source, config, 100.0, 50, 3);
    const auto second = quant::experiments::BootstrapAnalyzer::run(source, config, 100.0, 50, 3);
    if (first.metrics.size() != 50 || first.sampled_paths.size() != 9) return 1;
    if (first.terminal_wealth_p50 != second.terminal_wealth_p50 ||
        first.metrics.front().terminal_wealth != second.metrics.front().terminal_wealth) return 1;
    const std::string output = "test_results/bootstrap";
    std::filesystem::remove_all(output);
    quant::io::CsvResultExporter::write_bootstrap(first, output);
    std::ifstream summary(output + "/bootstrap_summary.csv");
    std::string header;
    std::getline(summary, header);
    if (header != "metric,value") return 1;
    std::cout << "bootstrap_tests passed\n";
    return 0;
}
