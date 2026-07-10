#pragma once

#include <string>

enum class EventType {
    Market,
    Signal,
    Order,
    Fill
};

enum class SignalType {
    Hold,
    Buy,
    Sell
};

enum class OrderSide {
    Buy,
    Sell
};

struct MarketEvent {
    EventType type{EventType::Market};
    std::string date;
    std::string ticker;
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    long long volume{0};
    std::size_t index{0};
};

struct SignalEvent {
    EventType type{EventType::Signal};
    std::string date;
    std::string ticker;
    std::string strategy;
    SignalType signal{SignalType::Hold};
};

struct OrderEvent {
    EventType type{EventType::Order};
    std::string date;
    std::string ticker;
    std::string strategy;
    OrderSide side{OrderSide::Buy};
    int quantity{0};
};

struct FillEvent {
    EventType type{EventType::Fill};
    std::string date;
    std::string ticker;
    std::string strategy;
    OrderSide side{OrderSide::Buy};
    int quantity{0};
    double fill_price{0.0};
    double gross_value{0.0};
    double transaction_cost{0.0};
    double slippage_cost{0.0};
};

