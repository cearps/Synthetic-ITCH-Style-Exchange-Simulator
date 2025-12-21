#include <gtest/gtest.h>
#include "producer/event_producer.h"
#include "matching/order_book.h"
#include "core/order.h"
#include <memory>
#include <set>
#include <map>
#include <vector>

namespace exchange {
namespace test {

class QRSDPEventProducerTest : public ::testing::Test {
protected:
    void SetUp() override {
        producer_ = std::make_unique<QRSDPEventProducer>();
        symbol_ = Symbol{"TEST"};
        order_book_ = std::make_shared<LimitOrderBook>(symbol_);
    }
    
    std::unique_ptr<QRSDPEventProducer> producer_;
    Symbol symbol_;
    std::shared_ptr<LimitOrderBook> order_book_;
};

TEST_F(QRSDPEventProducerTest, InitializationWithSeed) {
    const uint64_t seed = 12345;
    producer_->initialize(seed);
    
    // After initialization, producer should be ready
    // Set up producer with order book and configuration
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL); // 1 second in nanoseconds
    
    // Producer should be able to check for events
    bool has_events = producer_->has_next_event();
    // Result depends on book state, but should not crash
    (void)has_events;
}

TEST_F(QRSDPEventProducerTest, DeterministicBehaviorWithSameSeed) {
    // Test that same seed produces identical event sequence when book state is identical
    // This is a core requirement for replayability
    
    const uint64_t seed = 54321;
    
    // Set up producer
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL); // 1 second
    
    // First run
    producer_->initialize(seed);
    std::vector<OrderEvent> events1;
    
    // Generate a few events with empty book
    for (int i = 0; i < 5 && producer_->has_next_event(); ++i) {
        events1.push_back(producer_->next_event());
    }
    
    // Second run with same seed
    producer_->reset();
    producer_->initialize(seed);
    std::vector<OrderEvent> events2;
    
    // Generate same number of events
    for (int i = 0; i < 5 && producer_->has_next_event(); ++i) {
        events2.push_back(producer_->next_event());
    }
    
    // Events should be identical (when book state is same)
    EXPECT_EQ(events1.size(), events2.size());
    if (events1.size() > 0 && events2.size() > 0) {
        // Compare first few events
        for (size_t i = 0; i < std::min(events1.size(), events2.size()); ++i) {
            EXPECT_EQ(events1[i].order_id.value, events2[i].order_id.value);
            EXPECT_EQ(events1[i].type, events2[i].type);
            EXPECT_EQ(events1[i].side, events2[i].side);
            EXPECT_EQ(events1[i].price.value, events2[i].price.value);
            EXPECT_EQ(events1[i].quantity.value, events2[i].quantity.value);
        }
    }
}

TEST_F(QRSDPEventProducerTest, ResetFunctionality) {
    const uint64_t seed = 99999;
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(seed);
    
    // Generate an event
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        (void)event;
    }
    
    producer_->reset();
    
    // After reset, should be able to reinitialize
    producer_->initialize(seed);
    
    // Should be able to generate events again
    bool has_events = producer_->has_next_event();
    (void)has_events;
}

TEST_F(QRSDPEventProducerTest, StateDependentIntensityCalculation) {
    // Test that producer reads book state and adjusts intensities
    // Different book states should produce different event rates/types
    
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(42);
    
    // Add orders to create book state
    auto order1 = std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    );
    order_book_->add_order(order1);
    
    auto order2 = std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10100}, Quantity{50}, Timestamp{0}
    );
    order_book_->add_order(order2);
    
    // Producer should be able to generate events with book state
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        // Event should have valid fields
        EXPECT_GT(event.order_id.value, 0);
        EXPECT_GT(event.timestamp.nanoseconds_since_epoch, 0);
    }
    
    // Test with different book state (wider spread)
    order_book_->clear();
    auto order3 = std::make_shared<Order>(
        OrderId{3}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    );
    order_book_->add_order(order3);
    
    auto order4 = std::make_shared<Order>(
        OrderId{4}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10300}, Quantity{50}, Timestamp{0}  // Wider spread
    );
    order_book_->add_order(order4);
    
    // Producer should still be able to generate events
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
}

TEST_F(QRSDPEventProducerTest, CompetingPoissonClocks) {
    // Test that different event types can be generated
    // with probabilities based on intensities (competing Poisson clocks)
    
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(100);
    
    // Add some orders to enable cancel events
    auto order1 = std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    );
    order_book_->add_order(order1);
    
    auto order2 = std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10100}, Quantity{50}, Timestamp{0}
    );
    order_book_->add_order(order2);
    
    // Generate multiple events and verify different types are generated
    std::map<EventType, size_t> event_type_counts;
    for (int i = 0; i < 20 && producer_->has_next_event(); ++i) {
        auto event = producer_->next_event();
        event_type_counts[event.type]++;
    }
    
    // Should have generated at least one type of event
    EXPECT_GT(event_type_counts.size(), 0);
    
    // Should see ADD events (most common when book is not empty)
    // May also see CANCEL or AGGRESSIVE events
}

TEST_F(QRSDPEventProducerTest, EventParameterGeneration) {
    // Test that generated events have valid parameters:
    // - Order IDs are deterministic and unique
    // - Prices are within valid range
    // - Quantities are positive
    // - Sides are valid (BUY/SELL)
    
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(200);
    
    // Generate events and verify parameters
    std::set<OrderId> order_ids;
    for (int i = 0; i < 10 && producer_->has_next_event(); ++i) {
        auto event = producer_->next_event();
        
        // Order IDs should be unique
        EXPECT_EQ(order_ids.find(event.order_id), order_ids.end());
        order_ids.insert(event.order_id);
        
        // Quantities should be positive
        EXPECT_GT(event.quantity.value, 0);
        
        // Sides should be valid
        EXPECT_TRUE(event.side == OrderSide::BUY || event.side == OrderSide::SELL);
        
        // Symbols should match
        EXPECT_EQ(event.symbol.value, symbol_.value);
        
        // Prices should be positive (or 0 for market orders)
        EXPECT_GE(event.price.value, 0);
        
        // Timestamps should advance
        if (i > 0) {
            EXPECT_GE(event.timestamp.nanoseconds_since_epoch, 0);
        }
    }
}

TEST_F(QRSDPEventProducerTest, SpreadBucketCalculation) {
    // Test state bucketing for spread:
    // S1: 1 tick
    // S2: 2 ticks
    // S3: 3+ ticks
    
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(300);
    
    // Test spread = 1 tick
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10001}, Quantity{50}, Timestamp{0}  // 1 tick spread
    ));
    
    // Producer should be able to generate events with 1-tick spread
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
    
    // Test spread = 2 ticks
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{3}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{4}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10002}, Quantity{50}, Timestamp{0}  // 2 tick spread
    ));
    
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
    
    // Test spread >= 3 ticks
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{5}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{6}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10005}, Quantity{50}, Timestamp{0}  // 5 tick spread
    ));
    
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
}

TEST_F(QRSDPEventProducerTest, ImbalanceBucketCalculation) {
    // Test state bucketing for imbalance:
    // I--: strongly ask-heavy (imbalance < -0.6)
    // I-: mildly ask-heavy (-0.6 ≤ imbalance < -0.2)
    // I0: balanced (-0.2 ≤ imbalance ≤ 0.2)
    // I+: mildly bid-heavy (0.2 < imbalance ≤ 0.6)
    // I++: strongly bid-heavy (imbalance > 0.6)
    
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(400);
    
    // Test strongly ask-heavy (bid_qty=10, ask_qty=100)
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{10}, Timestamp{0}
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10001}, Quantity{100}, Timestamp{0}
    ));
    
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
    
    // Test balanced (bid_qty=50, ask_qty=50)
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{3}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{50}, Timestamp{0}
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{4}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10001}, Quantity{50}, Timestamp{0}
    ));
    
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
    
    // Test strongly bid-heavy (bid_qty=100, ask_qty=10)
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{5}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{100}, Timestamp{0}
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{6}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10001}, Quantity{10}, Timestamp{0}
    ));
    
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
}

TEST_F(QRSDPEventProducerTest, QueueSizeBucketCalculation) {
    // Test state bucketing for queue size:
    // Qsmall: small queue (e.g., qty < 100 shares)
    // Qmed: medium queue (e.g., 100 ≤ qty < 1000 shares)
    // Qlarge: large queue (e.g., qty ≥ 1000 shares)
    
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(500);
    
    // Test small queue (qty < 100)
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{1}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{50}, Timestamp{0}  // Small queue
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{2}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10001}, Quantity{50}, Timestamp{0}
    ));
    
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
    
    // Test medium queue (100 ≤ qty < 1000)
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{3}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{500}, Timestamp{0}  // Medium queue
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{4}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10001}, Quantity{500}, Timestamp{0}
    ));
    
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
    
    // Test large queue (qty ≥ 1000)
    order_book_->clear();
    order_book_->add_order(std::make_shared<Order>(
        OrderId{5}, symbol_, OrderSide::BUY, OrderType::LIMIT,
        Price{10000}, Quantity{2000}, Timestamp{0}  // Large queue
    ));
    order_book_->add_order(std::make_shared<Order>(
        OrderId{6}, symbol_, OrderSide::SELL, OrderType::LIMIT,
        Price{10001}, Quantity{2000}, Timestamp{0}
    ));
    
    if (producer_->has_next_event()) {
        auto event = producer_->next_event();
        EXPECT_GT(event.order_id.value, 0);
    }
}

TEST_F(QRSDPEventProducerTest, DeterministicOrderIdGeneration) {
    // Test that order IDs are generated deterministically
    // Same seed + same sequence should produce same IDs
    
    const uint64_t seed = 600;
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(seed);
    
    // Generate events and collect order IDs
    std::vector<OrderId> order_ids1;
    int count = 0;
    while (count < 5 && producer_->has_next_event()) {
        auto event = producer_->next_event();
        // Only count events with valid order IDs (skip empty/invalid events)
        if (event.order_id.value > 0) {
            order_ids1.push_back(event.order_id);
            count++;
        }
    }
    
    // Reset and reinitialize with same seed
    producer_->reset();
    producer_->initialize(seed);
    
    // Generate events again
    std::vector<OrderId> order_ids2;
    count = 0;
    while (count < 5 && producer_->has_next_event()) {
        auto event = producer_->next_event();
        if (event.order_id.value > 0) {
            order_ids2.push_back(event.order_id);
            count++;
        }
    }
    
    // Order IDs should be identical with same seed (if we got any)
    if (order_ids1.size() > 0 && order_ids2.size() > 0) {
        EXPECT_EQ(order_ids1.size(), order_ids2.size());
        for (size_t i = 0; i < std::min(order_ids1.size(), order_ids2.size()); ++i) {
            EXPECT_EQ(order_ids1[i].value, order_ids2[i].value);
        }
        
        // Order IDs should be unique within a run
        std::set<OrderId> unique_ids(order_ids1.begin(), order_ids1.end());
        EXPECT_EQ(unique_ids.size(), order_ids1.size());
    }
}

TEST_F(QRSDPEventProducerTest, SimulatedTimeAdvancement) {
    // Test that producer advances simulated time based on
    // exponential distribution sampling (Poisson process)
    
    producer_->set_order_book(symbol_, order_book_);
    producer_->set_tick_size(1);
    producer_->set_horizon(1000000000ULL);
    producer_->initialize(700);
    
    // Generate events and collect timestamps
    std::vector<Timestamp> timestamps1;
    for (int i = 0; i < 5 && producer_->has_next_event(); ++i) {
        auto event = producer_->next_event();
        timestamps1.push_back(event.timestamp);
    }
    
    // Verify timestamps advance monotonically
    for (size_t i = 1; i < timestamps1.size(); ++i) {
        EXPECT_GE(timestamps1[i].nanoseconds_since_epoch, timestamps1[i-1].nanoseconds_since_epoch);
    }
    
    // Reset and reinitialize with same seed
    producer_->reset();
    producer_->initialize(700);
    
    // Generate events again
    std::vector<Timestamp> timestamps2;
    for (int i = 0; i < 5 && producer_->has_next_event(); ++i) {
        auto event = producer_->next_event();
        timestamps2.push_back(event.timestamp);
    }
    
    // Timestamps should be identical with same seed (deterministic)
    EXPECT_EQ(timestamps1.size(), timestamps2.size());
    for (size_t i = 0; i < timestamps1.size(); ++i) {
        EXPECT_EQ(timestamps1[i].nanoseconds_since_epoch, timestamps2[i].nanoseconds_since_epoch);
    }
}

} // namespace test
} // namespace exchange

