#include "matching/matching_engine.h"
#include "matching/order_book.h"
#include "core/order.h"
#include <algorithm>
#include <vector>
#include <chrono>

namespace exchange {

PriceTimeMatchingEngine::PriceTimeMatchingEngine() 
    : sequence_counter_(0), current_timestamp_{0} {
}

void PriceTimeMatchingEngine::process_order_event(const OrderEvent& event) {
    auto book = get_order_book(event.symbol);
    if (!book) {
        return;  // No order book for this symbol
    }
    
    switch (event.type) {
        case EventType::ORDER_ADD:
            if (event.order_type == OrderType::LIMIT) {
                match_limit_order(event, book);
            } else {
                match_market_order(event, book);
            }
            break;
            
        case EventType::ORDER_CANCEL:
            book->cancel_order(event.order_id);
            emit_book_update(event.symbol, event.side, book);
            break;
            
        case EventType::ORDER_AGGRESSIVE_TAKE:
            match_market_order(event, book);
            break;
            
        default:
            break;
    }
}

void PriceTimeMatchingEngine::set_order_book(Symbol symbol, std::shared_ptr<IOrderBook> order_book) {
    order_books_[symbol] = order_book;
}

std::shared_ptr<IOrderBook> PriceTimeMatchingEngine::get_order_book(Symbol symbol) const {
    auto it = order_books_.find(symbol);
    return (it != order_books_.end()) ? it->second : nullptr;
}

void PriceTimeMatchingEngine::set_trade_callback(std::function<void(const TradeEvent&)> callback) {
    trade_callback_ = callback;
}

void PriceTimeMatchingEngine::set_book_update_callback(std::function<void(const BookUpdateEvent&)> callback) {
    book_update_callback_ = callback;
}

void PriceTimeMatchingEngine::match_limit_order(const OrderEvent& event, std::shared_ptr<IOrderBook> book) {
    // Create order object
    auto order = std::make_shared<Order>(
        event.order_id, event.symbol, event.side, event.order_type,
        event.price, event.quantity, event.timestamp
    );
    
    // Check if order crosses the book
    bool crosses = false;
    if (event.side == OrderSide::BUY && book->has_ask()) {
        crosses = (event.price.value >= book->best_ask().value);
    } else if (event.side == OrderSide::SELL && book->has_bid()) {
        crosses = (event.price.value <= book->best_bid().value);
    }
    
    if (crosses) {
        // Match against opposite side
        match_against_book(order, book);
    } else {
        // Add to book
        book->add_order(order);
        emit_book_update(event.symbol, event.side, book);
    }
}

void PriceTimeMatchingEngine::match_market_order(const OrderEvent& event, std::shared_ptr<IOrderBook> book) {
    // Create order object
    auto order = std::make_shared<Order>(
        event.order_id, event.symbol, event.side, event.order_type,
        event.price, event.quantity, event.timestamp
    );
    
    // Market orders always match if there's liquidity
    if ((event.side == OrderSide::BUY && book->has_ask()) ||
        (event.side == OrderSide::SELL && book->has_bid())) {
        match_against_book(order, book);
    }
}

void PriceTimeMatchingEngine::match_against_book(OrderPtr incoming_order, std::shared_ptr<IOrderBook> book) {
    auto limit_book = std::dynamic_pointer_cast<LimitOrderBook>(book);
    if (!limit_book) {
        return;
    }
    
    Quantity remaining_qty = incoming_order->remaining_quantity();
    
    // Continue matching until incoming order is filled or no more liquidity
    while (remaining_qty.value > 0) {
        // Get best opposite price
        Price match_price{0};
        bool has_liquidity = false;
        
        if (incoming_order->side() == OrderSide::BUY && book->has_ask()) {
            match_price = book->best_ask();
            has_liquidity = true;
        } else if (incoming_order->side() == OrderSide::SELL && book->has_bid()) {
            match_price = book->best_bid();
            has_liquidity = true;
        }
        
        if (!has_liquidity) {
            break;  // No more liquidity
        }
        
        // Check if price is acceptable (for limit orders)
        if (incoming_order->type() == OrderType::LIMIT) {
            if (incoming_order->side() == OrderSide::BUY && 
                incoming_order->price().value < match_price.value) {
                break;  // Limit price too low
            }
            if (incoming_order->side() == OrderSide::SELL && 
                incoming_order->price().value > match_price.value) {
                break;  // Limit price too high
            }
        }
        
        // Get first order at match price (price-time priority)
        OrderPtr matched_order = (incoming_order->side() == OrderSide::BUY)
            ? limit_book->get_first_ask_order_at_price(match_price)
            : limit_book->get_first_bid_order_at_price(match_price);
        
        if (!matched_order || !matched_order->is_active()) {
            break;  // No active order at this price
        }
        
        // Calculate match quantity
        Quantity matched_remaining = matched_order->remaining_quantity();
        Quantity match_qty{std::min(remaining_qty.value, matched_remaining.value)};
        
        // Create trade
        TradeEvent trade;
        if (incoming_order->side() == OrderSide::BUY) {
            trade.buy_order_id = incoming_order->id();
            trade.sell_order_id = matched_order->id();
        } else {
            trade.buy_order_id = matched_order->id();
            trade.sell_order_id = incoming_order->id();
        }
        trade.symbol = incoming_order->symbol();
        trade.execution_price = match_price;
        trade.execution_quantity = match_qty;
        trade.timestamp = get_current_timestamp();
        trade.sequence_number = ++sequence_counter_;
        
        // Fill both orders
        incoming_order->fill(match_qty);
        matched_order->fill(match_qty);
        
        // Update price level quantity for partially filled order
        Price matched_price = matched_order->price();
        OrderSide matched_side = matched_order->side();
        
        // Remove matched order from book if fully filled
        if (matched_order->is_filled()) {
            book->cancel_order(matched_order->id());
            // Emit book update for the side that was matched
            emit_book_update(incoming_order->symbol(), 
                           (incoming_order->side() == OrderSide::BUY) ? OrderSide::SELL : OrderSide::BUY, 
                           book);
        } else {
            // Update price level quantity for partially filled order
            auto limit_book_for_update = std::dynamic_pointer_cast<LimitOrderBook>(book);
            if (limit_book_for_update) {
                limit_book_for_update->update_price_level_quantity(matched_price, matched_side);
            }
            emit_book_update(incoming_order->symbol(), 
                           (incoming_order->side() == OrderSide::BUY) ? OrderSide::SELL : OrderSide::BUY, 
                           book);
        }
        
        // Emit trade
        if (trade_callback_) {
            trade_callback_(trade);
        }
        
        // Update remaining quantity and continue matching
        remaining_qty = incoming_order->remaining_quantity();
        
        // If incoming order is fully filled, break
        if (incoming_order->is_filled()) {
            break;
        }
    }
    
    // Add incoming order to book if not fully filled
    if (incoming_order->remaining_quantity().value > 0 && 
        incoming_order->type() == OrderType::LIMIT) {
        book->add_order(incoming_order);
    }
    
    // Emit book update for incoming order side
    emit_book_update(incoming_order->symbol(), incoming_order->side(), book);
}

void PriceTimeMatchingEngine::emit_book_update(Symbol symbol, OrderSide side, std::shared_ptr<IOrderBook> book) {
    if (!book_update_callback_) {
        return;
    }
    
    BookUpdateEvent update;
    update.symbol = symbol;
    update.side = side;
    update.timestamp = get_current_timestamp();
    update.sequence_number = ++sequence_counter_;
    
    if (side == OrderSide::BUY && book->has_bid()) {
        update.price_level = book->best_bid();
        update.quantity_at_level = book->bid_quantity_at_price(book->best_bid());
    } else if (side == OrderSide::SELL && book->has_ask()) {
        update.price_level = book->best_ask();
        update.quantity_at_level = book->ask_quantity_at_price(book->best_ask());
    } else {
        return;  // No update needed
    }
    
    book_update_callback_(update);
}

Timestamp PriceTimeMatchingEngine::get_current_timestamp() const {
    return current_timestamp_;
}

void PriceTimeMatchingEngine::set_current_timestamp(Timestamp timestamp) {
    current_timestamp_ = timestamp;
}

} // namespace exchange

