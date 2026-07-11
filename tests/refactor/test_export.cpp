#include "Backtester.h"
#include "Strategy.h"
#include "quant/io/ResultExporter.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const std::string output_dir = "test_results/export_target";
    std::filesystem::remove_all(output_dir);
    BacktestConfig config;
    config.ticker = "VALID";
    config.data_dir = "tests/fixtures";
    const auto result = Backtester(config).run_detailed(MovingAverageCrossoverStrategy(1, 2));
    if (std::filesystem::exists(output_dir)) return 1;
    quant::io::CsvResultExporter::write_backtest(result, output_dir);
    std::ifstream summary(output_dir + "/VALID_MA_Cross_performance_summary.csv");
    std::string header;
    std::getline(summary, header);
    if (header.rfind("schema_version,ticker,strategy", 0) != 0) return 1;
    std::cout << "export_tests passed\n";
    return 0;
}
