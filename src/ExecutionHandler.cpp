#include "ExecutionHandler.h"

#include <cmath>

ExecutionHandler::ExecutionHandler(double transaction_cost_rate, double slippage_rate)
    : transaction_cost_rate_(transaction_cost_rate), slippage_rate_(slippage_rate) {}

FillEvent ExecutionHandler::execute_order(const OrderEvent& order, double market_price) const {
    if (order.quantity <= 0 || market_price <= 0.0) {
        return FillEvent{EventType::Fill, order.date, order.ticker, order.strategy, order.side, 0, market_price, 0.0, 0.0, 0.0};
    }

    double direction = order.side == OrderSide::Buy ? 1.0 : -1.0;
    double fill_price = market_price * (1.0 + direction * slippage_rate_);
    double gross_value = fill_price * order.quantity;
    double transaction_cost = std::abs(gross_value) * transaction_cost_rate_;
    double slippage_cost = std::abs(market_price * order.quantity * slippage_rate_);

    return FillEvent{
        EventType::Fill,
        order.date,
        order.ticker,
        order.strategy,
        order.side,
        order.quantity,
        fill_price,
        gross_value,
        transaction_cost,
        slippage_cost
    };
}

