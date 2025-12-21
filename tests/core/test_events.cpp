#include <gtest/gtest.h>
#include "core/events.h"

namespace exchange {
namespace test {

TEST(EventsTest, OrderEventCreation) {
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = OrderId{1};
    event.symbol = Symbol{"AAPL"};
    event.side = OrderSide::BUY;
    event.price = Price{10000};
    event.quantity = Quantity{100};
    event.timestamp = Timestamp{0};
    event.sequence_number = 1;
    
    EXPECT_EQ(event.type, EventType::ORDER_ADD);
    EXPECT_EQ(event.order_id.value, 1);
    EXPECT_EQ(event.symbol.value, "AAPL");
    EXPECT_EQ(event.side, OrderSide::BUY);
    EXPECT_EQ(event.price.value, 10000);
    EXPECT_EQ(event.quantity.value, 100);
}

TEST(EventsTest, PriceComparison) {
    Price p1{100};
    Price p2{200};
    Price p3{100};
    
    EXPECT_LT(p1, p2);
    EXPECT_GT(p2, p1);
    EXPECT_EQ(p1, p3);
    EXPECT_LE(p1, p2);
    EXPECT_GE(p2, p1);
}

TEST(EventsTest, QuantitySubtraction) {
    Quantity q1{100};
    Quantity q2{30};
    Quantity result = q1 - q2;
    
    EXPECT_EQ(result.value, 70);
    
    Quantity q3{50};
    Quantity result2 = q2 - q3;
    EXPECT_EQ(result2.value, 0); // Should not go negative
}

} // namespace test
} // namespace exchange

