#include "Strategy.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace {
SignalEvent make_signal(const MarketEvent& event, const std::string& strategy_name, SignalType signal) {
    return SignalEvent{EventType::Signal, event.date, event.ticker, strategy_name, signal};
}

std::vector<double> closes_until(const std::vector<Bar>& history, std::size_t end_index) {
    std::vector<double> closes;
    closes.reserve(end_index + 1);
    for (std::size_t i = 0; i <= end_index && i < history.size(); ++i) {
        closes.push_back(history[i].close);
    }
    return closes;
}
}

MovingAverageCrossoverStrategy::MovingAverageCrossoverStrategy(int short_window, int long_window)
    : short_window_(short_window), long_window_(long_window) {}

std::string MovingAverageCrossoverStrategy::name() const {
    return "MA_Cross";
}

std::string MovingAverageCrossoverStrategy::parameters() const {
    return "short=" + std::to_string(short_window_) + ";long=" + std::to_string(long_window_);
}

SignalEvent MovingAverageCrossoverStrategy::on_market_event(const MarketEvent& event, const std::vector<Bar>& history) {
    if (event.index + 1 < static_cast<std::size_t>(long_window_)) {
        return make_signal(event, name(), SignalType::Hold);
    }

    double short_ma = simple_moving_average(history, event.index, short_window_);
    double long_ma = simple_moving_average(history, event.index, long_window_);
    int state = short_ma > long_ma ? 1 : -1;
    SignalType signal = SignalType::Hold;

    if (last_state_ != 0 && state != last_state_) {
        signal = state > 0 ? SignalType::Buy : SignalType::Sell;
    }
    last_state_ = state;
    return make_signal(event, name(), signal);
}

std::unique_ptr<Strategy> MovingAverageCrossoverStrategy::clone() const {
    return std::make_unique<MovingAverageCrossoverStrategy>(*this);
}

RSIMeanReversionStrategy::RSIMeanReversionStrategy(int period, double oversold, double overbought)
    : period_(period), oversold_(oversold), overbought_(overbought) {}

std::string RSIMeanReversionStrategy::name() const {
    return "RSI_Mean_Reversion";
}

std::string RSIMeanReversionStrategy::parameters() const {
    std::ostringstream out;
    out << "period=" << period_ << ";oversold=" << oversold_ << ";overbought=" << overbought_;
    return out.str();
}

SignalEvent RSIMeanReversionStrategy::on_market_event(const MarketEvent& event, const std::vector<Bar>& history) {
    if (event.index + 1 < static_cast<std::size_t>(period_ + 1)) {
        return make_signal(event, name(), SignalType::Hold);
    }

    double value = rsi(history, event.index, period_);
    if (value < oversold_) {
        return make_signal(event, name(), SignalType::Buy);
    }
    if (value > overbought_) {
        return make_signal(event, name(), SignalType::Sell);
    }
    return make_signal(event, name(), SignalType::Hold);
}

std::unique_ptr<Strategy> RSIMeanReversionStrategy::clone() const {
    return std::make_unique<RSIMeanReversionStrategy>(*this);
}

MACDMomentumStrategy::MACDMomentumStrategy(int fast, int slow, int signal)
    : fast_(fast), slow_(slow), signal_(signal) {}

std::string MACDMomentumStrategy::name() const {
    return "MACD_Momentum";
}

std::string MACDMomentumStrategy::parameters() const {
    return "fast=" + std::to_string(fast_) + ";slow=" + std::to_string(slow_) + ";signal=" + std::to_string(signal_);
}

SignalEvent MACDMomentumStrategy::on_market_event(const MarketEvent& event, const std::vector<Bar>& history) {
    if (event.index + 1 < static_cast<std::size_t>(slow_ + signal_)) {
        return make_signal(event, name(), SignalType::Hold);
    }

    const auto closes = closes_until(history, event.index);
    const auto ema_fast = exponential_moving_average(closes, fast_);
    const auto ema_slow = exponential_moving_average(closes, slow_);

    std::vector<double> macd_line;
    macd_line.reserve(closes.size());
    for (std::size_t i = 0; i < closes.size(); ++i) {
        macd_line.push_back(ema_fast[i] - ema_slow[i]);
    }
    const auto signal_line = exponential_moving_average(macd_line, signal_);

    int state = macd_line.back() > signal_line.back() ? 1 : -1;
    SignalType signal = SignalType::Hold;
    if (last_state_ != 0 && state != last_state_) {
        signal = state > 0 ? SignalType::Buy : SignalType::Sell;
    }
    last_state_ = state;
    return make_signal(event, name(), signal);
}

std::unique_ptr<Strategy> MACDMomentumStrategy::clone() const {
    return std::make_unique<MACDMomentumStrategy>(*this);
}

VolatilityBreakoutStrategy::VolatilityBreakoutStrategy(int lookback, double volatility_multiplier)
    : lookback_(lookback), volatility_multiplier_(volatility_multiplier) {}

std::string VolatilityBreakoutStrategy::name() const {
    return "Volatility_Breakout";
}

std::string VolatilityBreakoutStrategy::parameters() const {
    std::ostringstream out;
    out << "lookback=" << lookback_ << ";multiplier=" << volatility_multiplier_;
    return out.str();
}

SignalEvent VolatilityBreakoutStrategy::on_market_event(const MarketEvent& event, const std::vector<Bar>& history) {
    if (event.index < static_cast<std::size_t>(lookback_) || event.index < 2) {
        return make_signal(event, name(), SignalType::Hold);
    }
    std::vector<double> closes = closes_until(history, event.index);
    std::vector<double> returns = daily_returns(closes);
    double vol = rolling_volatility(returns, returns.size() - 1, lookback_);
    double reference = history[event.index - 1].close;
    double upper = reference * (1.0 + volatility_multiplier_ * vol);
    double lower = reference * (1.0 - volatility_multiplier_ * vol);
    if (event.close > upper) {
        return make_signal(event, name(), SignalType::Buy);
    }
    if (event.close < lower) {
        return make_signal(event, name(), SignalType::Sell);
    }
    return make_signal(event, name(), SignalType::Hold);
}

std::unique_ptr<Strategy> VolatilityBreakoutStrategy::clone() const {
    return std::make_unique<VolatilityBreakoutStrategy>(*this);
}

double simple_moving_average(const std::vector<Bar>& history, std::size_t end_index, int window) {
    if (window <= 0 || history.empty() || end_index + 1 < static_cast<std::size_t>(window)) {
        return 0.0;
    }
    double sum = 0.0;
    std::size_t start = end_index + 1 - static_cast<std::size_t>(window);
    for (std::size_t i = start; i <= end_index; ++i) {
        sum += history[i].close;
    }
    return sum / window;
}

double rsi(const std::vector<Bar>& history, std::size_t end_index, int period) {
    if (period <= 0 || end_index < static_cast<std::size_t>(period)) {
        return 50.0;
    }

    double gains = 0.0;
    double losses = 0.0;
    std::size_t start = end_index + 1 - static_cast<std::size_t>(period);
    for (std::size_t i = start; i <= end_index; ++i) {
        double change = history[i].close - history[i - 1].close;
        if (change >= 0.0) {
            gains += change;
        } else {
            losses -= change;
        }
    }

    if (losses == 0.0) {
        return 100.0;
    }
    double rs = gains / losses;
    return 100.0 - (100.0 / (1.0 + rs));
}

std::vector<double> daily_returns(const std::vector<double>& prices) {
    std::vector<double> returns;
    if (prices.size() < 2) {
        return returns;
    }
    returns.reserve(prices.size() - 1);
    for (std::size_t i = 1; i < prices.size(); ++i) {
        returns.push_back((prices[i] / prices[i - 1]) - 1.0);
    }
    return returns;
}

double rolling_volatility(const std::vector<double>& values, std::size_t end_index, int window) {
    if (window <= 1 || values.empty() || end_index + 1 < static_cast<std::size_t>(window)) {
        return 0.0;
    }
    std::size_t start = end_index + 1 - static_cast<std::size_t>(window);
    double mean = 0.0;
    for (std::size_t i = start; i <= end_index; ++i) {
        mean += values[i];
    }
    mean /= window;

    double variance = 0.0;
    for (std::size_t i = start; i <= end_index; ++i) {
        double diff = values[i] - mean;
        variance += diff * diff;
    }
    return std::sqrt(variance / (window - 1));
}

std::vector<double> exponential_moving_average(const std::vector<double>& values, int period) {
    std::vector<double> ema(values.size(), 0.0);
    if (values.empty() || period <= 0) {
        return ema;
    }

    double alpha = 2.0 / (period + 1.0);
    ema[0] = values[0];
    for (std::size_t i = 1; i < values.size(); ++i) {
        ema[i] = alpha * values[i] + (1.0 - alpha) * ema[i - 1];
    }
    return ema;
}
