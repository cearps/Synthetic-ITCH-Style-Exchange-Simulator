#include <gtest/gtest.h>
#include "logging/event_log.h"
#include "core/events.h"

namespace exchange {
namespace test {

class DeterministicEventLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_ = std::make_unique<DeterministicEventLog>();
    }
    
    std::unique_ptr<DeterministicEventLog> log_;
};

TEST_F(DeterministicEventLogTest, InitializeWithSeed) {
    const uint64_t seed = 12345;
    log_->initialize(seed);
    
    EXPECT_EQ(log_->get_seed(), seed);
    EXPECT_EQ(log_->get_sequence_number(), 0);
}

TEST_F(DeterministicEventLogTest, AppendOrderEvent) {
    log_->initialize(42);
    
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = OrderId{1};
    event.symbol = Symbol{"AAPL"};
    event.side = OrderSide::BUY;
    event.order_type = OrderType::LIMIT;
    event.price = Price{10000};
    event.quantity = Quantity{100};
    event.timestamp = Timestamp{0};
    event.sequence_number = 1;
    
    log_->append_event(event);
    
    EXPECT_EQ(log_->get_sequence_number(), 1);
}

TEST_F(DeterministicEventLogTest, AppendTradeEvent) {
    log_->initialize(100);
    
    TradeEvent trade{};
    trade.buy_order_id = OrderId{1};
    trade.sell_order_id = OrderId{2};
    trade.symbol = Symbol{"AAPL"};
    trade.execution_price = Price{10000};
    trade.execution_quantity = Quantity{100};
    trade.timestamp = Timestamp{0};
    trade.sequence_number = 1;
    
    log_->append_trade(trade);
    
    // Sequence number should increment
    EXPECT_GT(log_->get_sequence_number(), 0);
}

TEST_F(DeterministicEventLogTest, AppendBookUpdateEvent) {
    log_->initialize(200);
    
    BookUpdateEvent update{};
    update.symbol = Symbol{"AAPL"};
    update.side = OrderSide::BUY;
    update.price_level = Price{10000};
    update.quantity_at_level = Quantity{100};
    update.timestamp = Timestamp{0};
    update.sequence_number = 1;
    
    log_->append_book_update(update);
    
    // Sequence number should increment
    EXPECT_GT(log_->get_sequence_number(), 0);
}

TEST_F(DeterministicEventLogTest, SequenceNumberIncrements) {
    log_->initialize(300);
    
    OrderEvent event1{};
    event1.type = EventType::ORDER_ADD;
    event1.order_id = OrderId{1};
    event1.symbol = Symbol{"AAPL"};
    event1.side = OrderSide::BUY;
    event1.order_type = OrderType::LIMIT;
    event1.price = Price{10000};
    event1.quantity = Quantity{100};
    event1.timestamp = Timestamp{0};
    event1.sequence_number = 1;
    log_->append_event(event1);
    
    OrderEvent event2{};
    event2.type = EventType::ORDER_ADD;
    event2.order_id = OrderId{2};
    event2.symbol = Symbol{"AAPL"};
    event2.side = OrderSide::SELL;
    event2.order_type = OrderType::LIMIT;
    event2.price = Price{10100};
    event2.quantity = Quantity{50};
    event2.timestamp = Timestamp{1};
    event2.sequence_number = 2;
    log_->append_event(event2);
    
    EXPECT_EQ(log_->get_sequence_number(), 2);
}

TEST_F(DeterministicEventLogTest, Reset) {
    log_->initialize(400);
    
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = OrderId{1};
    event.symbol = Symbol{"AAPL"};
    event.side = OrderSide::BUY;
    event.order_type = OrderType::LIMIT;
    event.price = Price{10000};
    event.quantity = Quantity{100};
    event.timestamp = Timestamp{0};
    event.sequence_number = 1;
    log_->append_event(event);
    
    log_->reset();
    
    EXPECT_EQ(log_->get_sequence_number(), 0);
}

TEST_F(DeterministicEventLogTest, Clear) {
    log_->initialize(500);
    
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = OrderId{1};
    event.symbol = Symbol{"AAPL"};
    event.side = OrderSide::BUY;
    event.order_type = OrderType::LIMIT;
    event.price = Price{10000};
    event.quantity = Quantity{100};
    event.timestamp = Timestamp{0};
    event.sequence_number = 1;
    log_->append_event(event);
    
    log_->clear();
    
    EXPECT_EQ(log_->get_sequence_number(), 0);
}

TEST_F(DeterministicEventLogTest, ReplayModeDisabledByDefault) {
    log_->initialize(600);
    EXPECT_FALSE(log_->is_replay_mode());
}

TEST_F(DeterministicEventLogTest, EnableReplayMode) {
    log_->initialize(700);
    log_->enable_replay_mode(true);
    
    EXPECT_TRUE(log_->is_replay_mode());
}

TEST_F(DeterministicEventLogTest, DisableReplayMode) {
    log_->initialize(800);
    log_->enable_replay_mode(true);
    log_->enable_replay_mode(false);
    
    EXPECT_FALSE(log_->is_replay_mode());
}

TEST_F(DeterministicEventLogTest, ReplayEvents) {
    log_->initialize(900);
    
    OrderEvent event1{};
    event1.type = EventType::ORDER_ADD;
    event1.order_id = OrderId{1};
    event1.symbol = Symbol{"AAPL"};
    event1.side = OrderSide::BUY;
    event1.order_type = OrderType::LIMIT;
    event1.price = Price{10000};
    event1.quantity = Quantity{100};
    event1.timestamp = Timestamp{0};
    event1.sequence_number = 1;
    log_->append_event(event1);
    
    OrderEvent event2{};
    event2.type = EventType::ORDER_ADD;
    event2.order_id = OrderId{2};
    event2.symbol = Symbol{"AAPL"};
    event2.side = OrderSide::SELL;
    event2.order_type = OrderType::LIMIT;
    event2.price = Price{10100};
    event2.quantity = Quantity{50};
    event2.timestamp = Timestamp{1};
    event2.sequence_number = 2;
    log_->append_event(event2);
    
    auto replayed = log_->replay_events();
    
    EXPECT_EQ(replayed.size(), 2);
    EXPECT_EQ(replayed[0].order_id.value, 1);
    EXPECT_EQ(replayed[1].order_id.value, 2);
}

TEST_F(DeterministicEventLogTest, DeterministicReplay) {
    // Test that same seed produces same replay sequence
    const uint64_t seed = 1000;
    
    log_->initialize(seed);
    
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = OrderId{1};
    event.symbol = Symbol{"AAPL"};
    event.side = OrderSide::BUY;
    event.order_type = OrderType::LIMIT;
    event.price = Price{10000};
    event.quantity = Quantity{100};
    event.timestamp = Timestamp{0};
    event.sequence_number = 1;
    log_->append_event(event);
    
    auto replay1 = log_->replay_events();
    
    log_->clear();  // Clear events explicitly
    log_->initialize(seed);
    log_->append_event(event);
    auto replay2 = log_->replay_events();
    
    // Replays should be identical
    EXPECT_EQ(replay1.size(), replay2.size());
    if (replay1.size() > 0 && replay2.size() > 0) {
        EXPECT_EQ(replay1[0].order_id.value, replay2[0].order_id.value);
    }
}

} // namespace test
} // namespace exchange

