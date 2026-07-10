#pragma once

#include "Event.h"

class ExecutionHandler {
public:
    ExecutionHandler(double transaction_cost_rate = 0.001, double slippage_rate = 0.0005);

    FillEvent execute_order(const OrderEvent& order, double market_price) const;

private:
    double transaction_cost_rate_;
    double slippage_rate_;
};

