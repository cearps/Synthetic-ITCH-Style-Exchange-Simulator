#include "core/order.h"

namespace exchange {

Order::Order(
    OrderId id,
    Symbol symbol,
    OrderSide side,
    OrderType type,
    Price price,
    Quantity quantity,
    Timestamp timestamp
) : id_(id), symbol_(symbol), side_(side), type_(type),
    price_(price), quantity_(quantity), filled_quantity_(Quantity{0}), timestamp_(timestamp) {
}

OrderId Order::id() const {
    return id_;
}

Symbol Order::symbol() const {
    return symbol_;
}

OrderSide Order::side() const {
    return side_;
}

OrderType Order::type() const {
    return type_;
}

Price Order::price() const {
    return price_;
}

Quantity Order::quantity() const {
    return quantity_;
}

Quantity Order::filled_quantity() const {
    return filled_quantity_;
}

Quantity Order::remaining_quantity() const {
    return quantity_.value > filled_quantity_.value 
        ? Quantity{quantity_.value - filled_quantity_.value}
        : Quantity{0};
}

void Order::fill(Quantity quantity) {
    if (filled_quantity_.value + quantity.value <= quantity_.value) {
        filled_quantity_.value += quantity.value;
    }
}

bool Order::is_filled() const {
    return filled_quantity_.value >= quantity_.value;
}

bool Order::is_active() const {
    return !is_filled();
}

} // namespace exchange

