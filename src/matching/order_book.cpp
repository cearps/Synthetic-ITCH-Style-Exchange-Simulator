#include "matching/order_book.h"
#include <algorithm>

namespace exchange {

LimitOrderBook::LimitOrderBook(Symbol symbol) : symbol_(symbol) {
}

void LimitOrderBook::add_order(OrderPtr order) {
    if (!order) {
        return;
    }
    
    // Store order by ID
    orders_by_id_[order->id()] = order;
    
    Price price = order->price();
    
    // Update price level quantities and time-ordered queues
    // Only count remaining quantity (in case order was partially filled before adding)
    if (order->side() == OrderSide::BUY) {
        bid_levels_[price].value += order->remaining_quantity().value;
        bid_queues_[price].push_back(order);
    } else {
        ask_levels_[price].value += order->remaining_quantity().value;
        ask_queues_[price].push_back(order);
    }
}

void LimitOrderBook::cancel_order(OrderId order_id) {
    auto it = orders_by_id_.find(order_id);
    if (it == orders_by_id_.end()) {
        return;  // Order not found
    }
    
    OrderPtr order = it->second;
    Price price = order->price();
    
    // Update price level quantity (subtract remaining, which may have changed due to fills)
    if (order->side() == OrderSide::BUY) {
        auto bid_it = bid_levels_.find(price);
        if (bid_it != bid_levels_.end()) {
            // Recalculate from queue to ensure accuracy
            Quantity total_qty{0};
            auto queue_it = bid_queues_.find(price);
            if (queue_it != bid_queues_.end()) {
                for (const auto& o : queue_it->second) {
                    if (o != order) {  // Exclude the order being cancelled
                        total_qty.value += o->remaining_quantity().value;
                    }
                }
            }
            
            if (total_qty.value > 0) {
                bid_it->second = total_qty;
            } else {
                bid_levels_.erase(bid_it);
            }
        }
        
        // Remove from queue
        auto queue_it = bid_queues_.find(price);
        if (queue_it != bid_queues_.end()) {
            auto& queue = queue_it->second;
            queue.erase(std::remove(queue.begin(), queue.end(), order), queue.end());
            if (queue.empty()) {
                bid_queues_.erase(queue_it);
            }
        }
    } else {
        auto ask_it = ask_levels_.find(price);
        if (ask_it != ask_levels_.end()) {
            // Recalculate from queue
            Quantity total_qty{0};
            auto queue_it = ask_queues_.find(price);
            if (queue_it != ask_queues_.end()) {
                for (const auto& o : queue_it->second) {
                    if (o != order) {
                        total_qty.value += o->remaining_quantity().value;
                    }
                }
            }
            
            if (total_qty.value > 0) {
                ask_it->second = total_qty;
            } else {
                ask_levels_.erase(ask_it);
            }
        }
        
        // Remove from queue
        auto queue_it = ask_queues_.find(price);
        if (queue_it != ask_queues_.end()) {
            auto& queue = queue_it->second;
            queue.erase(std::remove(queue.begin(), queue.end(), order), queue.end());
            if (queue.empty()) {
                ask_queues_.erase(queue_it);
            }
        }
    }
    
    // Remove from orders map
    orders_by_id_.erase(it);
}

OrderPtr LimitOrderBook::find_order(OrderId order_id) const {
    auto it = orders_by_id_.find(order_id);
    return (it != orders_by_id_.end()) ? it->second : nullptr;
}

Price LimitOrderBook::best_bid() const {
    if (bid_levels_.empty()) {
        return Price{0};
    }
    // Best bid is highest price (map is sorted by key)
    return bid_levels_.rbegin()->first;
}

Price LimitOrderBook::best_ask() const {
    if (ask_levels_.empty()) {
        return Price{0};
    }
    // Best ask is lowest price (map is sorted by key)
    return ask_levels_.begin()->first;
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
    levels.reserve(bid_levels_.size());
    
    // Convert map to vector in descending price order (highest first)
    for (auto it = bid_levels_.rbegin(); it != bid_levels_.rend(); ++it) {
        levels.emplace_back(it->first, it->second);
    }
    
    return levels;
}

std::vector<std::pair<Price, Quantity>> LimitOrderBook::ask_levels() const {
    std::vector<std::pair<Price, Quantity>> levels;
    levels.reserve(ask_levels_.size());
    
    // Convert map to vector in ascending price order (lowest first)
    for (const auto& pair : ask_levels_) {
        levels.emplace_back(pair.first, pair.second);
    }
    
    return levels;
}

OrderPtr LimitOrderBook::get_first_bid_order_at_price(Price price) const {
    auto it = bid_queues_.find(price);
    if (it != bid_queues_.end() && !it->second.empty()) {
        // Return first order in queue (oldest at this price)
        return it->second.front();
    }
    return nullptr;
}

OrderPtr LimitOrderBook::get_first_ask_order_at_price(Price price) const {
    auto it = ask_queues_.find(price);
    if (it != ask_queues_.end() && !it->second.empty()) {
        // Return first order in queue (oldest at this price)
        return it->second.front();
    }
    return nullptr;
}

void LimitOrderBook::update_price_level_quantity(Price price, OrderSide side) {
    Quantity new_qty{0};
    
    if (side == OrderSide::BUY) {
        auto queue_it = bid_queues_.find(price);
        if (queue_it != bid_queues_.end()) {
            for (const auto& order : queue_it->second) {
                new_qty.value += order->remaining_quantity().value;
            }
        }
        if (new_qty.value > 0) {
            bid_levels_[price] = new_qty;
        } else {
            bid_levels_.erase(price);
        }
    } else {
        auto queue_it = ask_queues_.find(price);
        if (queue_it != ask_queues_.end()) {
            for (const auto& order : queue_it->second) {
                new_qty.value += order->remaining_quantity().value;
            }
        }
        if (new_qty.value > 0) {
            ask_levels_[price] = new_qty;
        } else {
            ask_levels_.erase(price);
        }
    }
}

void LimitOrderBook::clear() {
    orders_by_id_.clear();
    bid_levels_.clear();
    ask_levels_.clear();
    bid_queues_.clear();
    ask_queues_.clear();
}

Symbol LimitOrderBook::symbol() const {
    return symbol_;
}

} // namespace exchange

