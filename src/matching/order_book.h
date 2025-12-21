#pragma once

#include "../core/order.h"
#include "../core/events.h"
#include <vector>
#include <memory>
#include <map>
#include <utility>

namespace exchange {

class IOrderBook {
public:
    virtual ~IOrderBook() = default;
    
    virtual void add_order(OrderPtr order) = 0;
    virtual void cancel_order(OrderId order_id) = 0;
    virtual OrderPtr find_order(OrderId order_id) const = 0;
    
    virtual Price best_bid() const = 0;
    virtual Price best_ask() const = 0;
    virtual bool has_bid() const = 0;
    virtual bool has_ask() const = 0;
    
    virtual Quantity bid_quantity_at_price(Price price) const = 0;
    virtual Quantity ask_quantity_at_price(Price price) const = 0;
    
    virtual std::vector<std::pair<Price, Quantity>> bid_levels() const = 0;
    virtual std::vector<std::pair<Price, Quantity>> ask_levels() const = 0;
    
    virtual void clear() = 0;
};

class LimitOrderBook : public IOrderBook {
public:
    LimitOrderBook(Symbol symbol);
    virtual ~LimitOrderBook() = default;
    
    void add_order(OrderPtr order) override;
    void cancel_order(OrderId order_id) override;
    OrderPtr find_order(OrderId order_id) const override;
    
    Price best_bid() const override;
    Price best_ask() const override;
    bool has_bid() const override;
    bool has_ask() const override;
    
    Quantity bid_quantity_at_price(Price price) const override;
    Quantity ask_quantity_at_price(Price price) const override;
    
    std::vector<std::pair<Price, Quantity>> bid_levels() const override;
    std::vector<std::pair<Price, Quantity>> ask_levels() const override;
    
    void clear() override;
    
    Symbol symbol() const;
    
private:
    Symbol symbol_;
    std::map<OrderId, OrderPtr> orders_by_id_;
    std::map<Price, Quantity> bid_levels_;
    std::map<Price, Quantity> ask_levels_;
};

} // namespace exchange

