#include <gtest/gtest.h>
#include "core/order.h"

namespace exchange {
namespace test {

TEST(OrderTest, OrderCreation) {
    Order order(
        OrderId{1},
        Symbol{"AAPL"},
        OrderSide::BUY,
        OrderType::LIMIT,
        Price{10000},
        Quantity{100},
        Timestamp{0}
    );
    
    EXPECT_EQ(order.id().value, 1);
    EXPECT_EQ(order.symbol().value, "AAPL");
    EXPECT_EQ(order.side(), OrderSide::BUY);
    EXPECT_EQ(order.type(), OrderType::LIMIT);
    EXPECT_EQ(order.price().value, 10000);
    EXPECT_EQ(order.quantity().value, 100);
    EXPECT_EQ(order.filled_quantity().value, 0);
    EXPECT_FALSE(order.is_filled());
    EXPECT_TRUE(order.is_active());
}

TEST(OrderTest, OrderFilling) {
    Order order(
        OrderId{1},
        Symbol{"AAPL"},
        OrderSide::BUY,
        OrderType::LIMIT,
        Price{10000},
        Quantity{100},
        Timestamp{0}
    );
    
    order.fill(Quantity{30});
    EXPECT_EQ(order.filled_quantity().value, 30);
    EXPECT_EQ(order.remaining_quantity().value, 70);
    EXPECT_FALSE(order.is_filled());
    
    order.fill(Quantity{70});
    EXPECT_EQ(order.filled_quantity().value, 100);
    EXPECT_EQ(order.remaining_quantity().value, 0);
    EXPECT_TRUE(order.is_filled());
    EXPECT_FALSE(order.is_active());
}

} // namespace test
} // namespace exchange

