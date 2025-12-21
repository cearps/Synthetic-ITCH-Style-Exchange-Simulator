#pragma once

#include "../core/events.h"
#include "order_book.h"
#include <memory>
#include <vector>
#include <map>
#include <functional>

namespace exchange {

class IMatchingEngine {
public:
    virtual ~IMatchingEngine() = default;
    
    virtual void process_order_event(const OrderEvent& event) = 0;
    virtual void set_order_book(Symbol symbol, std::shared_ptr<IOrderBook> order_book) = 0;
    virtual std::shared_ptr<IOrderBook> get_order_book(Symbol symbol) const = 0;
    
    virtual void set_trade_callback(std::function<void(const TradeEvent&)> callback) = 0;
    virtual void set_book_update_callback(std::function<void(const BookUpdateEvent&)> callback) = 0;
};

class PriceTimeMatchingEngine : public IMatchingEngine {
public:
    PriceTimeMatchingEngine();
    virtual ~PriceTimeMatchingEngine() = default;
    
    void process_order_event(const OrderEvent& event) override;
    void set_order_book(Symbol symbol, std::shared_ptr<IOrderBook> order_book) override;
    std::shared_ptr<IOrderBook> get_order_book(Symbol symbol) const override;
    
    void set_trade_callback(std::function<void(const TradeEvent&)> callback) override;
    void set_book_update_callback(std::function<void(const BookUpdateEvent&)> callback) override;
    
    void set_current_timestamp(Timestamp timestamp);
    
private:
    std::map<Symbol, std::shared_ptr<IOrderBook>> order_books_;
    std::function<void(const TradeEvent&)> trade_callback_;
    std::function<void(const BookUpdateEvent&)> book_update_callback_;
    uint64_t sequence_counter_;
    Timestamp current_timestamp_;
    
    void match_limit_order(const OrderEvent& event, std::shared_ptr<IOrderBook> book);
    void match_market_order(const OrderEvent& event, std::shared_ptr<IOrderBook> book);
    void match_against_book(OrderPtr incoming_order, std::shared_ptr<IOrderBook> book);
    void emit_book_update(Symbol symbol, OrderSide side, std::shared_ptr<IOrderBook> book);
    Timestamp get_current_timestamp() const;
};

} // namespace exchange

