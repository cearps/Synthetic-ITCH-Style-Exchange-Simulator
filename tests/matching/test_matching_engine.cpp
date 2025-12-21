#include <gtest/gtest.h>
#include "matching/matching_engine.h"
#include "matching/order_book.h"
#include "core/events.h"
#include <memory>
#include <vector>

namespace exchange {
namespace test {

class PriceTimeMatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<PriceTimeMatchingEngine>();
        symbol_ = Symbol{"AAPL"};
        order_book_ = std::make_shared<LimitOrderBook>(symbol_);
        engine_->set_order_book(symbol_, order_book_);
        
        trades_.clear();
        book_updates_.clear();
        
        engine_->set_trade_callback([this](const TradeEvent& trade) {
            trades_.push_back(trade);
        });
        
        engine_->set_book_update_callback([this](const BookUpdateEvent& update) {
            book_updates_.push_back(update);
        });
    }
    
    std::unique_ptr<PriceTimeMatchingEngine> engine_;
    Symbol symbol_;
    std::shared_ptr<LimitOrderBook> order_book_;
    std::vector<TradeEvent> trades_;
    std::vector<BookUpdateEvent> book_updates_;
};

TEST_F(PriceTimeMatchingEngineTest, AddLimitOrderNoMatch) {
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = OrderId{1};
    event.symbol = symbol_;
    event.side = OrderSide::BUY;
    event.order_type = OrderType::LIMIT;
    event.price = Price{10000};
    event.quantity = Quantity{100};
    event.timestamp = Timestamp{0};
    event.sequence_number = 1;
    
    engine_->process_order_event(event);
    
    // Order should be added to book
    auto order = order_book_->find_order(OrderId{1});
    EXPECT_NE(order, nullptr);
    EXPECT_EQ(order_book_->best_bid().value, 10000);
    
    // No trade should occur
    EXPECT_EQ(trades_.size(), 0);
}

TEST_F(PriceTimeMatchingEngineTest, LimitOrderMatchesAtBestPrice) {
    // Add sell order first
    OrderEvent sell_event{};
    sell_event.type = EventType::ORDER_ADD;
    sell_event.order_id = OrderId{1};
    sell_event.symbol = symbol_;
    sell_event.side = OrderSide::SELL;
    sell_event.order_type = OrderType::LIMIT;
    sell_event.price = Price{10000};
    sell_event.quantity = Quantity{100};
    sell_event.timestamp = Timestamp{0};
    sell_event.sequence_number = 1;
    engine_->process_order_event(sell_event);
    
    // Add buy order that matches
    OrderEvent buy_event{};
    buy_event.type = EventType::ORDER_ADD;
    buy_event.order_id = OrderId{2};
    buy_event.symbol = symbol_;
    buy_event.side = OrderSide::BUY;
    buy_event.order_type = OrderType::LIMIT;
    buy_event.price = Price{10000};
    buy_event.quantity = Quantity{100};
    buy_event.timestamp = Timestamp{1};
    buy_event.sequence_number = 2;
    engine_->process_order_event(buy_event);
    
    // Trade should occur
    EXPECT_EQ(trades_.size(), 1);
    EXPECT_EQ(trades_[0].execution_price.value, 10000);
    EXPECT_EQ(trades_[0].execution_quantity.value, 100);
    EXPECT_EQ(trades_[0].buy_order_id.value, 2);
    EXPECT_EQ(trades_[0].sell_order_id.value, 1);
}

TEST_F(PriceTimeMatchingEngineTest, PriceTimePriority) {
    // Add multiple sell orders at same price
    OrderEvent sell1{};
    sell1.type = EventType::ORDER_ADD;
    sell1.order_id = OrderId{1};
    sell1.symbol = symbol_;
    sell1.side = OrderSide::SELL;
    sell1.order_type = OrderType::LIMIT;
    sell1.price = Price{10000};
    sell1.quantity = Quantity{50};
    sell1.timestamp = Timestamp{0};
    sell1.sequence_number = 1;
    engine_->process_order_event(sell1);
    
    OrderEvent sell2{};
    sell2.type = EventType::ORDER_ADD;
    sell2.order_id = OrderId{2};
    sell2.symbol = symbol_;
    sell2.side = OrderSide::SELL;
    sell2.order_type = OrderType::LIMIT;
    sell2.price = Price{10000};
    sell2.quantity = Quantity{50};
    sell2.timestamp = Timestamp{1};
    sell2.sequence_number = 2;
    engine_->process_order_event(sell2);
    
    // Add buy order that matches both
    OrderEvent buy_event{};
    buy_event.type = EventType::ORDER_ADD;
    buy_event.order_id = OrderId{3};
    buy_event.symbol = symbol_;
    buy_event.side = OrderSide::BUY;
    buy_event.order_type = OrderType::LIMIT;
    buy_event.price = Price{10000};
    buy_event.quantity = Quantity{100};
    buy_event.timestamp = Timestamp{2};
    buy_event.sequence_number = 3;
    engine_->process_order_event(buy_event);
    
    // Should match first order (earlier timestamp) completely, then second
    EXPECT_EQ(trades_.size(), 2);
    EXPECT_EQ(trades_[0].sell_order_id.value, 1); // First order matched first
    EXPECT_EQ(trades_[1].sell_order_id.value, 2); // Second order matched second
}

TEST_F(PriceTimeMatchingEngineTest, PartialFill) {
    // Add sell order
    OrderEvent sell_event{};
    sell_event.type = EventType::ORDER_ADD;
    sell_event.order_id = OrderId{1};
    sell_event.symbol = symbol_;
    sell_event.side = OrderSide::SELL;
    sell_event.order_type = OrderType::LIMIT;
    sell_event.price = Price{10000};
    sell_event.quantity = Quantity{200};
    sell_event.timestamp = Timestamp{0};
    sell_event.sequence_number = 1;
    engine_->process_order_event(sell_event);
    
    // Add smaller buy order
    OrderEvent buy_event{};
    buy_event.type = EventType::ORDER_ADD;
    buy_event.order_id = OrderId{2};
    buy_event.symbol = symbol_;
    buy_event.side = OrderSide::BUY;
    buy_event.order_type = OrderType::LIMIT;
    buy_event.price = Price{10000};
    buy_event.quantity = Quantity{100};
    buy_event.timestamp = Timestamp{1};
    buy_event.sequence_number = 2;
    engine_->process_order_event(buy_event);
    
    // Should have one trade for 100
    EXPECT_EQ(trades_.size(), 1);
    EXPECT_EQ(trades_[0].execution_quantity.value, 100);
    
    // Sell order should still be in book with remaining quantity
    auto sell_order = order_book_->find_order(OrderId{1});
    EXPECT_NE(sell_order, nullptr);
    EXPECT_EQ(sell_order->remaining_quantity().value, 100);
}

TEST_F(PriceTimeMatchingEngineTest, MarketOrderMatchesImmediately) {
    // Add sell order
    OrderEvent sell_event{};
    sell_event.type = EventType::ORDER_ADD;
    sell_event.order_id = OrderId{1};
    sell_event.symbol = symbol_;
    sell_event.side = OrderSide::SELL;
    sell_event.order_type = OrderType::LIMIT;
    sell_event.price = Price{10000};
    sell_event.quantity = Quantity{100};
    sell_event.timestamp = Timestamp{0};
    sell_event.sequence_number = 1;
    engine_->process_order_event(sell_event);
    
    // Add market buy order
    OrderEvent buy_event{};
    buy_event.type = EventType::ORDER_AGGRESSIVE_TAKE;
    buy_event.order_id = OrderId{2};
    buy_event.symbol = symbol_;
    buy_event.side = OrderSide::BUY;
    buy_event.order_type = OrderType::MARKET;
    buy_event.price = Price{0}; // Market orders may have price 0
    buy_event.quantity = Quantity{100};
    buy_event.timestamp = Timestamp{1};
    buy_event.sequence_number = 2;
    engine_->process_order_event(buy_event);
    
    // Should match immediately
    EXPECT_EQ(trades_.size(), 1);
    EXPECT_EQ(trades_[0].execution_price.value, 10000); // At best ask
}

TEST_F(PriceTimeMatchingEngineTest, CancelOrder) {
    // Add order
    OrderEvent add_event{};
    add_event.type = EventType::ORDER_ADD;
    add_event.order_id = OrderId{1};
    add_event.symbol = symbol_;
    add_event.side = OrderSide::BUY;
    add_event.order_type = OrderType::LIMIT;
    add_event.price = Price{10000};
    add_event.quantity = Quantity{100};
    add_event.timestamp = Timestamp{0};
    add_event.sequence_number = 1;
    engine_->process_order_event(add_event);
    
    // Cancel order
    OrderEvent cancel_event{};
    cancel_event.type = EventType::ORDER_CANCEL;
    cancel_event.order_id = OrderId{1};
    cancel_event.symbol = symbol_;
    cancel_event.side = OrderSide::BUY;
    cancel_event.order_type = OrderType::LIMIT;
    cancel_event.price = Price{10000};
    cancel_event.quantity = Quantity{100};
    cancel_event.timestamp = Timestamp{1};
    cancel_event.sequence_number = 2;
    engine_->process_order_event(cancel_event);
    
    // Order should be removed
    EXPECT_EQ(order_book_->find_order(OrderId{1}), nullptr);
    EXPECT_FALSE(order_book_->has_bid());
}

TEST_F(PriceTimeMatchingEngineTest, BookUpdateCallback) {
    // Add order - should trigger book update
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = OrderId{1};
    event.symbol = symbol_;
    event.side = OrderSide::BUY;
    event.order_type = OrderType::LIMIT;
    event.price = Price{10000};
    event.quantity = Quantity{100};
    event.timestamp = Timestamp{0};
    event.sequence_number = 1;
    engine_->process_order_event(event);
    
    // Should have book update
    EXPECT_GT(book_updates_.size(), 0);
}

TEST_F(PriceTimeMatchingEngineTest, MultipleSymbols) {
    Symbol symbol2{"MSFT"};
    auto book2 = std::make_shared<LimitOrderBook>(symbol2);
    engine_->set_order_book(symbol2, book2);
    
    // Add order for first symbol
    OrderEvent event1{};
    event1.type = EventType::ORDER_ADD;
    event1.order_id = OrderId{1};
    event1.symbol = symbol_;
    event1.side = OrderSide::BUY;
    event1.order_type = OrderType::LIMIT;
    event1.price = Price{10000};
    event1.quantity = Quantity{100};
    event1.timestamp = Timestamp{0};
    event1.sequence_number = 1;
    engine_->process_order_event(event1);
    
    // Add order for second symbol
    OrderEvent event2{};
    event2.type = EventType::ORDER_ADD;
    event2.order_id = OrderId{2};
    event2.symbol = symbol2;
    event2.side = OrderSide::BUY;
    event2.order_type = OrderType::LIMIT;
    event2.price = Price{20000};
    event2.quantity = Quantity{200};
    event2.timestamp = Timestamp{1};
    event2.sequence_number = 2;
    engine_->process_order_event(event2);
    
    // Both orders should be in their respective books
    EXPECT_NE(engine_->get_order_book(symbol_)->find_order(OrderId{1}), nullptr);
    EXPECT_NE(engine_->get_order_book(symbol2)->find_order(OrderId{2}), nullptr);
}

} // namespace test
} // namespace exchange

