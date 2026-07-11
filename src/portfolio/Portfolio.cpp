#include "Portfolio.h"

#include <algorithm>
#include <cmath>

Portfolio::Portfolio(double starting_capital)
    : starting_capital_(starting_capital),
      cash_(starting_capital),
      peak_value_(starting_capital) {}

OrderEvent Portfolio::generate_order(const SignalEvent& signal, double market_price) const {
    if (signal.signal == SignalType::Hold || market_price <= 0.0) {
        return OrderEvent{EventType::Order, signal.date, signal.ticker, signal.strategy, OrderSide::Buy, 0};
    }

    if (signal.signal == SignalType::Buy && position_ == 0) {
        double deployable_cash = cash_ * 0.95;
        int quantity = static_cast<int>(deployable_cash / market_price);
        return OrderEvent{EventType::Order, signal.date, signal.ticker, signal.strategy, OrderSide::Buy, quantity};
    }

    if (signal.signal == SignalType::Sell && position_ > 0) {
        return OrderEvent{EventType::Order, signal.date, signal.ticker, signal.strategy, OrderSide::Sell, position_};
    }

    return OrderEvent{EventType::Order, signal.date, signal.ticker, signal.strategy, OrderSide::Buy, 0};
}

bool Portfolio::process_fill(const FillEvent& fill, double market_price) {
    if (fill.quantity <= 0) {
        return false;
    }

    double total_cost = fill.gross_value + fill.transaction_cost;
    double proceeds = fill.gross_value - fill.transaction_cost;
    double realized_pnl = 0.0;
    double trade_return = 0.0;

    if (fill.side == OrderSide::Buy) {
        if (total_cost > cash_) {
            return false;
        }
        int new_position = position_ + fill.quantity;
        average_entry_price_ = new_position > 0
            ? ((average_entry_price_ * position_) + total_cost) / new_position
            : 0.0;
        cash_ -= total_cost;
        position_ = new_position;
    } else {
        if (fill.quantity > position_) {
            return false;
        }
        realized_pnl = proceeds - average_entry_price_ * fill.quantity;
        double basis = average_entry_price_ * fill.quantity;
        trade_return = basis > 0.0 ? realized_pnl / basis : 0.0;
        cash_ += proceeds;
        position_ -= fill.quantity;
        if (position_ == 0) {
            average_entry_price_ = 0.0;
        }
    }

    last_price_ = market_price;
    double value = current_value();
    trades_.push_back(Trade{
        fill.date,
        fill.ticker,
        fill.strategy,
        fill.side == OrderSide::Buy ? "BUY" : "SELL",
        fill.fill_price,
        fill.quantity,
        fill.transaction_cost,
        fill.slippage_cost,
        value,
        realized_pnl,
        trade_return
    });
    return true;
}

void Portfolio::mark_to_market(const std::string& date, double close_price) {
    last_price_ = close_price;
    double holdings = position_ * close_price;
    double value = cash_ + holdings;
    peak_value_ = std::max(peak_value_, value);
    double total_return = starting_capital_ > 0.0 ? (value / starting_capital_) - 1.0 : 0.0;
    double drawdown = peak_value_ > 0.0 ? (value / peak_value_) - 1.0 : 0.0;

    equity_curve_.push_back(EquityPoint{date, value, cash_, holdings, total_return, drawdown});
}

double Portfolio::cash() const {
    return cash_;
}

int Portfolio::position() const {
    return position_;
}

double Portfolio::current_value() const {
    return cash_ + position_ * last_price_;
}

double Portfolio::starting_capital() const {
    return starting_capital_;
}

const std::vector<Trade>& Portfolio::trades() const {
    return trades_;
}

const std::vector<EquityPoint>& Portfolio::equity_curve() const {
    return equity_curve_;
}
