#pragma once

#include "Event.h"

#include <string>
#include <vector>

struct Trade {
    std::string date;
    std::string ticker;
    std::string strategy;
    std::string action;
    double price{0.0};
    int quantity{0};
    double cost{0.0};
    double slippage{0.0};
    double portfolio_value{0.0};
    double realized_pnl{0.0};
    double trade_return{0.0};
};

struct EquityPoint {
    std::string date;
    double portfolio_value{0.0};
    double cash{0.0};
    double holdings{0.0};
    double total_return{0.0};
    double drawdown{0.0};
};

class Portfolio {
public:
    explicit Portfolio(double starting_capital);

    OrderEvent generate_order(const SignalEvent& signal, double market_price) const;
    bool process_fill(const FillEvent& fill, double market_price);
    void mark_to_market(const std::string& date, double close_price);

    double cash() const;
    int position() const;
    double current_value() const;
    double starting_capital() const;
    const std::vector<Trade>& trades() const;
    const std::vector<EquityPoint>& equity_curve() const;

private:
    double starting_capital_{0.0};
    double cash_{0.0};
    int position_{0};
    double last_price_{0.0};
    double average_entry_price_{0.0};
    double peak_value_{0.0};
    std::vector<Trade> trades_;
    std::vector<EquityPoint> equity_curve_;
};

