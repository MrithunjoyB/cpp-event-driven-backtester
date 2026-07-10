#pragma once

#include "Event.h"
#include "MarketData.h"

#include <memory>
#include <string>
#include <vector>

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::string name() const = 0;
    virtual SignalEvent on_market_event(const MarketEvent& event, const std::vector<Bar>& history) = 0;
    virtual std::unique_ptr<Strategy> clone() const = 0;
};

class MovingAverageCrossoverStrategy : public Strategy {
public:
    MovingAverageCrossoverStrategy(int short_window = 20, int long_window = 50);
    std::string name() const override;
    SignalEvent on_market_event(const MarketEvent& event, const std::vector<Bar>& history) override;
    std::unique_ptr<Strategy> clone() const override;

private:
    int short_window_;
    int long_window_;
    int last_state_{0};
};

class RSIMeanReversionStrategy : public Strategy {
public:
    RSIMeanReversionStrategy(int period = 14, double oversold = 30.0, double overbought = 70.0);
    std::string name() const override;
    SignalEvent on_market_event(const MarketEvent& event, const std::vector<Bar>& history) override;
    std::unique_ptr<Strategy> clone() const override;

private:
    int period_;
    double oversold_;
    double overbought_;
};

class MACDMomentumStrategy : public Strategy {
public:
    MACDMomentumStrategy(int fast = 12, int slow = 26, int signal = 9);
    std::string name() const override;
    SignalEvent on_market_event(const MarketEvent& event, const std::vector<Bar>& history) override;
    std::unique_ptr<Strategy> clone() const override;

private:
    int fast_;
    int slow_;
    int signal_;
    int last_state_{0};
};

double simple_moving_average(const std::vector<Bar>& history, std::size_t end_index, int window);
double rsi(const std::vector<Bar>& history, std::size_t end_index, int period);
std::vector<double> daily_returns(const std::vector<double>& prices);
double rolling_volatility(const std::vector<double>& values, std::size_t end_index, int window);
std::vector<double> exponential_moving_average(const std::vector<double>& values, int period);

