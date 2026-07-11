#include "quant/config/ConfigLoader.h"

#include "quant/domain/Errors.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <limits>
#include <set>
#include <sstream>
#include <variant>

namespace quant::config {
namespace {
using JsonValue = std::variant<std::string, double, std::vector<std::string>>;

class Parser {
public:
    explicit Parser(std::string text) : text_(std::move(text)) {}

    std::map<std::string, JsonValue> parse_object() {
        skip_space();
        expect('{');
        std::map<std::string, JsonValue> values;
        skip_space();
        while (!consume('}')) {
            const std::string key = parse_string();
            if (values.count(key) != 0) {
                fail("duplicate field '" + key + "'");
            }
            skip_space();
            expect(':');
            skip_space();
            values.emplace(key, parse_value());
            skip_space();
            if (consume('}')) {
                break;
            }
            expect(',');
            skip_space();
        }
        skip_space();
        if (position_ != text_.size()) {
            fail("unexpected trailing content");
        }
        return values;
    }

private:
    JsonValue parse_value() {
        if (peek() == '"') {
            return parse_string();
        }
        if (peek() == '[') {
            return parse_string_array();
        }
        return parse_number();
    }

    std::string parse_string() {
        expect('"');
        std::string output;
        while (position_ < text_.size() && text_[position_] != '"') {
            char ch = text_[position_++];
            if (ch == '\\') {
                if (position_ >= text_.size()) {
                    fail("unterminated escape sequence");
                }
                const char escaped = text_[position_++];
                if (escaped == '"' || escaped == '\\' || escaped == '/') {
                    output.push_back(escaped);
                } else if (escaped == 'n') {
                    output.push_back('\n');
                } else if (escaped == 't') {
                    output.push_back('\t');
                } else {
                    fail("unsupported escape sequence");
                }
            } else {
                output.push_back(ch);
            }
        }
        expect('"');
        return output;
    }

    std::vector<std::string> parse_string_array() {
        expect('[');
        skip_space();
        std::vector<std::string> output;
        while (!consume(']')) {
            output.push_back(parse_string());
            skip_space();
            if (consume(']')) {
                break;
            }
            expect(',');
            skip_space();
        }
        return output;
    }

    double parse_number() {
        const std::size_t start = position_;
        if (peek() == '-') {
            ++position_;
        }
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
                ++position_;
            }
        }
        if (start == position_) {
            fail("expected string, number, or string array");
        }
        try {
            const double value = std::stod(text_.substr(start, position_ - start));
            if (!std::isfinite(value)) {
                fail("number must be finite");
            }
            return value;
        } catch (const std::exception&) {
            fail("invalid number");
        }
    }

    char peek() const {
        return position_ < text_.size() ? text_[position_] : '\0';
    }

    bool consume(char expected) {
        if (peek() == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    void skip_space() {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\n' || text_[position_] == '\r' || text_[position_] == '\t')) {
            ++position_;
        }
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw ConfigurationError("JSON parse error at byte " + std::to_string(position_) + ": " + message);
    }

    std::string text_;
    std::size_t position_{0};
};

std::string read_file(const std::string& filepath) {
    std::ifstream input(filepath);
    if (!input.is_open()) {
        throw ConfigurationError("Could not open configuration file: " + filepath);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        throw ConfigurationError("Could not read configuration file: " + filepath);
    }
    return buffer.str();
}

template <typename T>
T get_or(const std::map<std::string, JsonValue>& values, const std::string& key, const T& fallback) {
    auto it = values.find(key);
    if (it == values.end()) {
        return fallback;
    }
    if (const auto* typed = std::get_if<T>(&it->second)) {
        return *typed;
    }
    throw ConfigurationError("Configuration field '" + key + "' has the wrong type");
}

int integer_or(const std::map<std::string, JsonValue>& values, const std::string& key, int fallback) {
    const double value = get_or<double>(values, key, static_cast<double>(fallback));
    if (std::floor(value) != value || value < static_cast<double>(std::numeric_limits<int>::min()) ||
        value > static_cast<double>(std::numeric_limits<int>::max())) {
        throw ConfigurationError("Configuration field '" + key + "' must be an integer");
    }
    return static_cast<int>(value);
}

void require_positive(double value, const std::string& field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw ConfigurationError("Configuration field '" + field + "' must be positive");
    }
}

void require_non_negative(double value, const std::string& field) {
    if (!std::isfinite(value) || value < 0.0) {
        throw ConfigurationError("Configuration field '" + field + "' must be non-negative");
    }
}
}

ExperimentConfig ConfigLoader::load_file(const std::string& filepath) {
    const auto values = Parser(read_file(filepath)).parse_object();
    const std::set<std::string> known = {
        "experiment_name", "ticker_universe", "strategy", "starting_capital", "commission_bps", "slippage_bps",
        "train_window_days", "test_window_days", "step_days", "window_mode", "train_years", "test_months",
        "step_months", "oos_continuity_policy", "boundary_position_policy", "benchmark",
        "parameter_selection_objective", "minimum_trade_requirement", "regime_classification_method", "random_seed",
        "output_directory", "allocation_policy", "rebalance_frequency", "max_weight", "cash_buffer",
        "min_trade_value", "volatility_lookback", "momentum_lookback", "top_n",
        "portfolio_output_directory", "result_schema_version"};
    for (const auto& value : values) {
        if (known.count(value.first) == 0) {
            throw ConfigurationError("Unknown configuration field '" + value.first + "'");
        }
    }

    ExperimentConfig config;
    config.name = get_or<std::string>(values, "experiment_name", config.name);
    config.tickers = get_or<std::vector<std::string>>(values, "ticker_universe", config.tickers);
    config.strategy = get_or<std::string>(values, "strategy", config.strategy);
    config.execution.starting_capital = get_or<double>(values, "starting_capital", config.execution.starting_capital);
    config.execution.commission_bps = get_or<double>(values, "commission_bps", config.execution.commission_bps);
    config.execution.slippage_bps = get_or<double>(values, "slippage_bps", config.execution.slippage_bps);
    config.walk_forward.train_days = integer_or(values, "train_window_days", config.walk_forward.train_days);
    config.walk_forward.test_days = integer_or(values, "test_window_days", config.walk_forward.test_days);
    config.walk_forward.step_days = integer_or(values, "step_days", config.walk_forward.step_days);
    config.walk_forward.window_mode = get_or<std::string>(values, "window_mode", config.walk_forward.window_mode);
    config.walk_forward.train_years = integer_or(values, "train_years", config.walk_forward.train_years);
    config.walk_forward.test_months = integer_or(values, "test_months", config.walk_forward.test_months);
    config.walk_forward.step_months = integer_or(values, "step_months", config.walk_forward.step_months);
    config.walk_forward.continuity_policy = get_or<std::string>(values, "oos_continuity_policy", config.walk_forward.continuity_policy);
    config.walk_forward.boundary_position_policy = get_or<std::string>(values, "boundary_position_policy", config.walk_forward.boundary_position_policy);
    config.benchmark.ticker = get_or<std::string>(values, "benchmark", config.benchmark.ticker);
    config.parameter_selection.objective = get_or<std::string>(values, "parameter_selection_objective", config.parameter_selection.objective);
    config.parameter_selection.minimum_trades = integer_or(values, "minimum_trade_requirement", config.parameter_selection.minimum_trades);
    config.regime.method = get_or<std::string>(values, "regime_classification_method", config.regime.method);
    const int random_seed = integer_or(values, "random_seed", static_cast<int>(config.bootstrap.random_seed));
    if (random_seed < 0) {
        throw ConfigurationError("Configuration field 'random_seed' must be non-negative");
    }
    config.bootstrap.random_seed = static_cast<unsigned int>(random_seed);
    config.output.results_dir = get_or<std::string>(values, "output_directory", "results/research/" + config.name);
    config.portfolio.allocation_policy = get_or<std::string>(values, "allocation_policy", config.portfolio.allocation_policy);
    config.portfolio.rebalance_frequency = get_or<std::string>(values, "rebalance_frequency", config.portfolio.rebalance_frequency);
    config.portfolio.max_weight = get_or<double>(values, "max_weight", config.portfolio.max_weight);
    config.portfolio.cash_buffer = get_or<double>(values, "cash_buffer", config.portfolio.cash_buffer);
    config.portfolio.min_trade_value = get_or<double>(values, "min_trade_value", config.portfolio.min_trade_value);
    config.portfolio.volatility_lookback = integer_or(values, "volatility_lookback", config.portfolio.volatility_lookback);
    config.portfolio.momentum_lookback = integer_or(values, "momentum_lookback", config.portfolio.momentum_lookback);
    config.portfolio.top_n = integer_or(values, "top_n", config.portfolio.top_n);
    config.output.portfolio_results_dir = get_or<std::string>(values, "portfolio_output_directory", config.output.portfolio_results_dir);
    config.result_schema_version = integer_or(values, "result_schema_version", config.result_schema_version);
    validate(config);
    return config;
}

void ConfigLoader::validate(const ExperimentConfig& config) {
    if (config.name.empty()) throw ConfigurationError("Configuration field 'experiment_name' cannot be empty");
    if (config.tickers.empty()) throw ConfigurationError("Configuration field 'ticker_universe' cannot be empty");
    if (config.strategy != "MA_Cross" && config.strategy != "RSI_Mean_Reversion" &&
        config.strategy != "MACD_Momentum" && config.strategy != "Volatility_Breakout") {
        throw ConfigurationError("Unknown strategy '" + config.strategy + "'");
    }
    require_positive(config.execution.starting_capital, "starting_capital");
    require_non_negative(config.execution.commission_bps, "commission_bps");
    require_non_negative(config.execution.slippage_bps, "slippage_bps");
    if (config.walk_forward.window_mode != "calendar_duration" && config.walk_forward.window_mode != "observation_count")
        throw ConfigurationError("Configuration field 'window_mode' must be calendar_duration or observation_count");
    if (config.walk_forward.train_years <= 0 || config.walk_forward.test_months <= 0 || config.walk_forward.step_months <= 0 ||
        config.walk_forward.train_days <= 0 || config.walk_forward.test_days <= 0 || config.walk_forward.step_days <= 0)
        throw ConfigurationError("Walk-forward durations must be positive");
    if (config.walk_forward.continuity_policy != "continuous_capital" && config.walk_forward.continuity_policy != "normalized_window")
        throw ConfigurationError("Invalid oos_continuity_policy");
    if (config.walk_forward.boundary_position_policy != "liquidate_at_test_end_close")
        throw ConfigurationError("Invalid boundary_position_policy");
    if (config.benchmark.ticker.empty() || config.benchmark.ticker == "same_ticker")
        throw ConfigurationError("Invalid benchmark definition");
    if (config.parameter_selection.minimum_trades < 0) throw ConfigurationError("minimum_trade_requirement cannot be negative");
    const std::set<std::string> objectives = {"sharpe_min_trades", "calmar", "excess_return", "sharpe_maxdd"};
    if (objectives.count(config.parameter_selection.objective) == 0)
        throw ConfigurationError("Unknown parameter_selection_objective '" + config.parameter_selection.objective + "'");
    if (config.regime.method != "trend_200_sma_60_return_vol_20_expanding_median")
        throw ConfigurationError("Unknown regime_classification_method '" + config.regime.method + "'");
    const std::set<std::string> allocation_policies = {"", "equal_weight", "inverse_volatility", "momentum_top_n"};
    if (allocation_policies.count(config.portfolio.allocation_policy) == 0)
        throw ConfigurationError("Unknown allocation_policy '" + config.portfolio.allocation_policy + "'");
    if (config.portfolio.rebalance_frequency != "weekly" && config.portfolio.rebalance_frequency != "monthly")
        throw ConfigurationError("Unknown rebalance_frequency '" + config.portfolio.rebalance_frequency + "'");
    if (config.portfolio.max_weight <= 0.0 || config.portfolio.max_weight > 1.0)
        throw ConfigurationError("max_weight must be in (0, 1]");
    if (config.portfolio.cash_buffer < 0.0 || config.portfolio.cash_buffer >= 1.0)
        throw ConfigurationError("cash_buffer must be in [0, 1)");
    require_non_negative(config.portfolio.min_trade_value, "min_trade_value");
    if (config.portfolio.volatility_lookback <= 1 || config.portfolio.momentum_lookback <= 0 || config.portfolio.top_n <= 0)
        throw ConfigurationError("Portfolio lookbacks and top_n must be positive");
    if (config.result_schema_version != 2) throw ConfigurationError("Only result_schema_version 2 is supported");
    if (config.output.results_dir.empty()) throw ConfigurationError("output_directory cannot be empty");
    if (config.output.portfolio_results_dir.empty()) throw ConfigurationError("portfolio_output_directory cannot be empty");
}

std::string ConfigLoader::to_json(const ExperimentConfig& config) {
    validate(config);
    std::ostringstream output;
    output << std::fixed << std::setprecision(6)
           << "{\n  \"experiment_name\": \"" << config.name << "\",\n"
           << "  \"ticker_universe\": [";
    for (std::size_t i = 0; i < config.tickers.size(); ++i) {
        if (i > 0) output << ", ";
        output << '"' << config.tickers[i] << '"';
    }
    output << "],\n"
           << "  \"strategy\": \"" << config.strategy << "\",\n"
           << "  \"starting_capital\": " << config.execution.starting_capital << ",\n"
           << "  \"commission_bps\": " << config.execution.commission_bps << ",\n"
           << "  \"slippage_bps\": " << config.execution.slippage_bps << ",\n"
           << "  \"train_window_days\": " << config.walk_forward.train_days << ",\n"
           << "  \"test_window_days\": " << config.walk_forward.test_days << ",\n"
           << "  \"step_days\": " << config.walk_forward.step_days << ",\n"
           << "  \"window_mode\": \"" << config.walk_forward.window_mode << "\",\n"
           << "  \"train_years\": " << config.walk_forward.train_years << ",\n"
           << "  \"test_months\": " << config.walk_forward.test_months << ",\n"
           << "  \"step_months\": " << config.walk_forward.step_months << ",\n"
           << "  \"oos_continuity_policy\": \"" << config.walk_forward.continuity_policy << "\",\n"
           << "  \"boundary_position_policy\": \"" << config.walk_forward.boundary_position_policy << "\",\n"
           << "  \"benchmark\": \"" << config.benchmark.ticker << "\",\n"
           << "  \"parameter_selection_objective\": \"" << config.parameter_selection.objective << "\",\n"
           << "  \"minimum_trade_requirement\": " << config.parameter_selection.minimum_trades << ",\n"
           << "  \"regime_classification_method\": \"" << config.regime.method << "\",\n"
           << "  \"random_seed\": " << config.bootstrap.random_seed << ",\n"
           << "  \"output_directory\": \"" << config.output.results_dir << "\",\n"
           << "  \"allocation_policy\": \"" << config.portfolio.allocation_policy << "\",\n"
           << "  \"rebalance_frequency\": \"" << config.portfolio.rebalance_frequency << "\",\n"
           << "  \"max_weight\": " << config.portfolio.max_weight << ",\n"
           << "  \"cash_buffer\": " << config.portfolio.cash_buffer << ",\n"
           << "  \"min_trade_value\": " << config.portfolio.min_trade_value << ",\n"
           << "  \"volatility_lookback\": " << config.portfolio.volatility_lookback << ",\n"
           << "  \"momentum_lookback\": " << config.portfolio.momentum_lookback << ",\n"
           << "  \"top_n\": " << config.portfolio.top_n << ",\n"
           << "  \"portfolio_output_directory\": \"" << config.output.portfolio_results_dir << "\",\n"
           << "  \"result_schema_version\": " << config.result_schema_version << "\n}\n";
    return output.str();
}

}  // namespace quant::config
