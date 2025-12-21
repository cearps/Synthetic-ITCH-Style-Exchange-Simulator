#include <gtest/gtest.h>
#include "matching/order_book.h"
#include "core/order.h"
#include <memory>

namespace exchange {
namespace test {

class LimitOrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        symbol_ = Symbol{"AAPL"};
        book_ = std::make_unique<LimitOrderBook>(symbol_);
    }
    
    Symbol symbol_;
    std::unique_ptr<LimitOrderBook> book_;
};

TEST_F(LimitOrderBookTest, AddBuyOrder) {
    auto order = std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    );
    
    book_->add_order(order);
    
    EXPECT_TRUE(book_->has_bid());
    EXPECT_EQ(book_->best_bid().value, 10000);
    EXPECT_EQ(book_->bid_quantity_at_price(Price{10000}).value, 100);
}

TEST_F(LimitOrderBookTest, AddSellOrder) {
    auto order = std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10100}, Quantity{50}, Timestamp{0}
    );
    
    book_->add_order(order);
    
    EXPECT_TRUE(book_->has_ask());
    EXPECT_EQ(book_->best_ask().value, 10100);
    EXPECT_EQ(book_->ask_quantity_at_price(Price{10100}).value, 50);
}

TEST_F(LimitOrderBookTest, AddMultipleOrdersAtSamePrice) {
    auto order1 = std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    );
    auto order2 = std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{200}, Timestamp{1}
    );
    
    book_->add_order(order1);
    book_->add_order(order2);
    
    EXPECT_EQ(book_->bid_quantity_at_price(Price{10000}).value, 300);
}

TEST_F(LimitOrderBookTest, BestBidAskWithMultipleLevels) {
    // Add multiple bid levels
    book_->add_order(std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{9900}, Quantity{100}, Timestamp{0}
    ));
    book_->add_order(std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{200}, Timestamp{1}
    ));
    
    // Add multiple ask levels
    book_->add_order(std::make_shared<Order>(
        OrderId{3}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10100}, Quantity{150}, Timestamp{2}
    ));
    book_->add_order(std::make_shared<Order>(
        OrderId{4}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10200}, Quantity{50}, Timestamp{3}
    ));
    
    EXPECT_EQ(book_->best_bid().value, 10000); // Highest bid
    EXPECT_EQ(book_->best_ask().value, 10100); // Lowest ask
}

TEST_F(LimitOrderBookTest, CancelOrder) {
    auto order = std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    );
    
    book_->add_order(order);
    EXPECT_EQ(book_->bid_quantity_at_price(Price{10000}).value, 100);
    
    book_->cancel_order(OrderId{1});
    
    // Order should be removed
    auto found = book_->find_order(OrderId{1});
    EXPECT_EQ(found, nullptr);
    
    // Quantity should be reduced
    EXPECT_EQ(book_->bid_quantity_at_price(Price{10000}).value, 0);
}

TEST_F(LimitOrderBookTest, CancelPartialOrder) {
    auto order = std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    );
    
    book_->add_order(order);
    
    // Fill part of the order
    order->fill(Quantity{30});
    
    // Cancel should work on remaining quantity
    book_->cancel_order(OrderId{1});
    
    // Order should be removed from book
    EXPECT_EQ(book_->find_order(OrderId{1}), nullptr);
}

TEST_F(LimitOrderBookTest, FindOrder) {
    auto order = std::make_shared<Order>(
        OrderId{42}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    );
    
    book_->add_order(order);
    
    auto found = book_->find_order(OrderId{42});
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->id().value, 42);
}

TEST_F(LimitOrderBookTest, FindNonExistentOrder) {
    auto found = book_->find_order(OrderId{999});
    EXPECT_EQ(found, nullptr);
}

TEST_F(LimitOrderBookTest, BidLevels) {
    book_->add_order(std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{9900}, Quantity{100}, Timestamp{0}
    ));
    book_->add_order(std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{200}, Timestamp{1}
    ));
    book_->add_order(std::make_shared<Order>(
        OrderId{3}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{50}, Timestamp{2}
    ));
    
    auto levels = book_->bid_levels();
    
    // Should have 2 price levels
    EXPECT_EQ(levels.size(), 2);
    
    // Levels should be sorted by price (descending)
    EXPECT_EQ(levels[0].first.value, 10000);
    EXPECT_EQ(levels[0].second.value, 250); // 200 + 50
    EXPECT_EQ(levels[1].first.value, 9900);
    EXPECT_EQ(levels[1].second.value, 100);
}

TEST_F(LimitOrderBookTest, AskLevels) {
    book_->add_order(std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10100}, Quantity{150}, Timestamp{0}
    ));
    book_->add_order(std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10200}, Quantity{50}, Timestamp{1}
    ));
    book_->add_order(std::make_shared<Order>(
        OrderId{3}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10100}, Quantity{25}, Timestamp{2}
    ));
    
    auto levels = book_->ask_levels();
    
    // Should have 2 price levels
    EXPECT_EQ(levels.size(), 2);
    
    // Levels should be sorted by price (ascending)
    EXPECT_EQ(levels[0].first.value, 10100);
    EXPECT_EQ(levels[0].second.value, 175); // 150 + 25
    EXPECT_EQ(levels[1].first.value, 10200);
    EXPECT_EQ(levels[1].second.value, 50);
}

TEST_F(LimitOrderBookTest, Clear) {
    book_->add_order(std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    ));
    book_->add_order(std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10100}, Quantity{50}, Timestamp{1}
    ));
    
    book_->clear();
    
    EXPECT_FALSE(book_->has_bid());
    EXPECT_FALSE(book_->has_ask());
    EXPECT_EQ(book_->find_order(OrderId{1}), nullptr);
    EXPECT_EQ(book_->find_order(OrderId{2}), nullptr);
}

TEST_F(LimitOrderBookTest, EmptyBookNoBidAsk) {
    EXPECT_FALSE(book_->has_bid());
    EXPECT_FALSE(book_->has_ask());
}

TEST_F(LimitOrderBookTest, SymbolAccess) {
    EXPECT_EQ(book_->symbol().value, "AAPL");
}

} // namespace test
} // namespace exchange

