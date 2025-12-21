#include "matching/order_book.h"

namespace exchange {

LimitOrderBook::LimitOrderBook(Symbol symbol) : symbol_(symbol) {
}

void LimitOrderBook::add_order(OrderPtr order) {
    // TODO: Implement order book add logic
}

void LimitOrderBook::cancel_order(OrderId order_id) {
    // TODO: Implement cancellation logic
}

OrderPtr LimitOrderBook::find_order(OrderId order_id) const {
    auto it = orders_by_id_.find(order_id);
    return (it != orders_by_id_.end()) ? it->second : nullptr;
}

Price LimitOrderBook::best_bid() const {
    // TODO: Implement best bid retrieval
    return Price{0};
}

Price LimitOrderBook::best_ask() const {
    // TODO: Implement best ask retrieval
    return Price{0};
}

bool LimitOrderBook::has_bid() const {
    return !bid_levels_.empty();
}

bool LimitOrderBook::has_ask() const {
    return !ask_levels_.empty();
}

Quantity LimitOrderBook::bid_quantity_at_price(Price price) const {
    auto it = bid_levels_.find(price);
    return (it != bid_levels_.end()) ? it->second : Quantity{0};
}

Quantity LimitOrderBook::ask_quantity_at_price(Price price) const {
    auto it = ask_levels_.find(price);
    return (it != ask_levels_.end()) ? it->second : Quantity{0};
}

std::vector<std::pair<Price, Quantity>> LimitOrderBook::bid_levels() const {
    std::vector<std::pair<Price, Quantity>> levels;
    // TODO: Convert map to vector in descending price order
    return levels;
}

std::vector<std::pair<Price, Quantity>> LimitOrderBook::ask_levels() const {
    std::vector<std::pair<Price, Quantity>> levels;
    // TODO: Convert map to vector in ascending price order
    return levels;
}

void LimitOrderBook::clear() {
    orders_by_id_.clear();
    bid_levels_.clear();
    ask_levels_.clear();
}

Symbol LimitOrderBook::symbol() const {
    return symbol_;
}

} // namespace exchange

