#pragma once

#include "events.h"
#include <memory>

namespace exchange {

class Order {
public:
    Order(
        OrderId id,
        Symbol symbol,
        OrderSide side,
        OrderType type,
        Price price,
        Quantity quantity,
        Timestamp timestamp
    );
    
    OrderId id() const;
    Symbol symbol() const;
    OrderSide side() const;
    OrderType type() const;
    Price price() const;
    Quantity quantity() const;
    Quantity filled_quantity() const;
    Quantity remaining_quantity() const;
    Timestamp timestamp() const;
    
    void fill(Quantity quantity);
    bool is_filled() const;
    bool is_active() const;
    
private:
    OrderId id_;
    Symbol symbol_;
    OrderSide side_;
    OrderType type_;
    Price price_;
    Quantity quantity_;
    Quantity filled_quantity_;
    Timestamp timestamp_;
};

using OrderPtr = std::shared_ptr<Order>;

} // namespace exchange

