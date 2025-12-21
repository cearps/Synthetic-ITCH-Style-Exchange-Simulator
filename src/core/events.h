#pragma once

#include <cstdint>
#include <string>
#include <memory>

namespace exchange {

enum class EventType : uint8_t {
    ORDER_ADD = 1,
    ORDER_CANCEL = 2,
    ORDER_AGGRESSIVE_TAKE = 3,
    TRADE = 4,
    ORDER_BOOK_UPDATE = 5
};

enum class OrderSide : uint8_t {
    BUY = 1,
    SELL = 2
};

enum class OrderType : uint8_t {
    LIMIT = 1,
    MARKET = 2
};

struct OrderId {
    uint64_t value;
    
    bool operator==(const OrderId& other) const { return value == other.value; }
    bool operator<(const OrderId& other) const { return value < other.value; }
};

struct Symbol {
    std::string value;
    
    bool operator==(const Symbol& other) const { return value == other.value; }
    bool operator<(const Symbol& other) const { return value < other.value; }
};

struct Price {
    int64_t value; // Price in ticks (e.g., cents for USD, or smallest price increment)
    
    bool operator==(const Price& other) const { return value == other.value; }
    bool operator<(const Price& other) const { return value < other.value; }
    bool operator>(const Price& other) const { return value > other.value; }
    bool operator<=(const Price& other) const { return value <= other.value; }
    bool operator>=(const Price& other) const { return value >= other.value; }
};

struct Quantity {
    uint64_t value;
    
    Quantity operator-(const Quantity& other) const {
        return Quantity{value > other.value ? value - other.value : 0};
    }
    
    bool operator==(const Quantity& other) const { return value == other.value; }
    bool operator<(const Quantity& other) const { return value < other.value; }
};

struct Timestamp {
    uint64_t nanoseconds_since_epoch;
    
    bool operator<(const Timestamp& other) const {
        return nanoseconds_since_epoch < other.nanoseconds_since_epoch;
    }
    bool operator==(const Timestamp& other) const {
        return nanoseconds_since_epoch == other.nanoseconds_since_epoch;
    }
};

struct OrderEvent {
    EventType type;
    OrderId order_id;
    Symbol symbol;
    OrderSide side;
    OrderType order_type;
    Price price;
    Quantity quantity;
    Timestamp timestamp;
    uint64_t sequence_number;
};

struct TradeEvent {
    OrderId buy_order_id;
    OrderId sell_order_id;
    Symbol symbol;
    Price execution_price;
    Quantity execution_quantity;
    Timestamp timestamp;
    uint64_t sequence_number;
};

struct BookUpdateEvent {
    Symbol symbol;
    OrderSide side;
    Price price_level;
    Quantity quantity_at_level;
    Timestamp timestamp;
    uint64_t sequence_number;
};

} // namespace exchange

