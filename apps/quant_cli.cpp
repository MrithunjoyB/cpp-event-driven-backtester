#include "quant/app/Application.h"
#include "quant/config/ConfigLoader.h"
#include "quant/domain/Errors.h"
#include "quant/random/StableRng.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace {
void print_help() {
    std::cout
        << "quant_cli " << QUANT_PROJECT_VERSION << "\n\n"
        << "Commands:\n"
        << "  run --config <file> [--dry-run] [--execution-mode serial|parallel] [--threads N]\n"
        << "  validate-config --config <file>\n"
        << "  print-resolved-config --config <file>\n"
        << "  list-strategies\n"
        << "  list-allocation-policies\n"
        << "  version\n"
        << "  help\n\n"
        << "Legacy mode remains available through --mode and related flags.\n";
}

std::map<std::string, std::string> parse_options(
    int argc,
    char** argv,
    int start,
    const std::set<std::string>& value_options,
    const std::set<std::string>& flags) {
    std::map<std::string, std::string> output;
    for (int i = start; i < argc; ++i) {
        const std::string key = argv[i];
        if (flags.count(key) != 0) {
            if (output.count(key) != 0) throw quant::CliError("Duplicate option '" + key + "'");
            output[key] = "true";
            continue;
        }
        if (value_options.count(key) == 0) throw quant::CliError("Unknown option '" + key + "'");
        if (i + 1 >= argc) throw quant::CliError("Missing value for option '" + key + "'");
        if (output.count(key) != 0) throw quant::CliError("Duplicate option '" + key + "'");
        output[key] = argv[++i];
    }
    return output;
}

std::string require_config(const std::map<std::string, std::string>& options) {
    auto it = options.find("--config");
    if (it == options.end() || it->second.empty()) throw quant::CliError("--config <file> is required");
    return it->second;
}

double number_option(const std::map<std::string, std::string>& options, const std::string& key, double fallback) {
    auto it = options.find(key);
    if (it == options.end()) return fallback;
    std::size_t consumed = 0;
    const double value = std::stod(it->second, &consumed);
    if (consumed != it->second.size()) throw quant::CliError("Invalid numeric value for '" + key + "'");
    return value;
}

int integer_option(const std::map<std::string, std::string>& options, const std::string& key, int fallback) {
    auto it = options.find(key);
    if (it == options.end()) return fallback;
    std::size_t consumed = 0;
    long long value = 0;
    try {
        value = std::stoll(it->second, &consumed);
    } catch (const std::exception&) {
        throw quant::CliError("Option '" + key + "' must be an integer");
    }
    if (consumed != it->second.size() || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        throw quant::CliError("Option '" + key + "' must be an integer");
    }
    return static_cast<int>(value);
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 1 || std::string(argv[1]) == "help" || std::string(argv[1]) == "--help") {
            print_help();
            return 0;
        }
        const std::string command = argv[1];
        if (command == "version" || command == "--version") {
            std::cout << "cpp-event-driven-backtester " << QUANT_PROJECT_VERSION << '\n'
                      << "stochastic_methodology_version="
                      << quant::random::kStochasticMethodologyVersion << '\n'
                      << "rng_mapping=" << quant::random::kRngMapping << '\n';
            return 0;
        }
        if (command == "list-strategies") {
            std::cout << "MA_Cross\nRSI_Mean_Reversion\nMACD_Momentum\nVolatility_Breakout\n";
            return 0;
        }
        if (command == "list-allocation-policies") {
            std::cout << "equal_weight\ninverse_volatility\nmomentum_top_n\n";
            return 0;
        }
        if (command == "run") {
            const auto options = parse_options(argc, argv, 2, {"--config", "--execution-mode", "--threads"}, {"--dry-run"});
            const std::string mode = options.count("--execution-mode") ? options.at("--execution-mode") : "";
            return quant::app::Application::run_config(require_config(options), options.count("--dry-run") != 0,
                mode, integer_option(options, "--threads", 0));
        }
        if (command == "validate-config") {
            const auto options = parse_options(argc, argv, 2, {"--config"}, {});
            const auto config = quant::config::ConfigLoader::load_file(require_config(options));
            std::cout << "Configuration valid: " << config.name << '\n';
            return 0;
        }
        if (command == "print-resolved-config") {
            const auto options = parse_options(argc, argv, 2, {"--config"}, {});
            std::cout << quant::config::ConfigLoader::to_json(
                quant::config::ConfigLoader::load_file(require_config(options)));
            return 0;
        }
        if (!command.empty() && command.front() == '-') {
            const auto options = parse_options(
                argc, argv, 1,
                {"--mode", "--ticker", "--strategy", "--start", "--end", "--benchmark",
                 "--capital", "--transaction-cost", "--slippage"}, {});
            quant::app::LegacyRunRequest request;
            if (options.count("--mode")) request.mode = options.at("--mode");
            if (options.count("--ticker")) request.ticker = options.at("--ticker");
            if (options.count("--strategy")) request.strategy = options.at("--strategy");
            if (options.count("--start")) request.start_date = options.at("--start");
            if (options.count("--end")) request.end_date = options.at("--end");
            if (options.count("--benchmark")) request.benchmark = options.at("--benchmark");
            request.capital = number_option(options, "--capital", request.capital);
            request.transaction_cost = number_option(options, "--transaction-cost", request.transaction_cost);
            request.slippage = number_option(options, "--slippage", request.slippage);
            return quant::app::Application::run_legacy(request);
        }
        throw quant::CliError("Unknown command '" + command + "'; run 'quant_cli help'");
    } catch (const std::exception& error) {
        std::cerr << "quant_cli: " << error.what() << '\n';
        return 1;
    }
}
