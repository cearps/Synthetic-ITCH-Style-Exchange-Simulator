#include <gtest/gtest.h>
#include "exchange_simulator.h"
#include "producer/event_producer.h"
#include "matching/matching_engine.h"
#include "matching/order_book.h"
#include "logging/event_log.h"
#include <memory>

namespace exchange {
namespace test {

class DeterminismTest : public ::testing::Test {
protected:
    void SetUp() override {
        symbol_ = Symbol{"TEST"};
    }
    
    Symbol symbol_;
};

TEST_F(DeterminismTest, SameSeedProducesIdenticalEventSequence) {
    // Test that running the simulator twice with the same seed produces identical events
    // Note: This is a basic test - full determinism requires identical book state progression
    const uint64_t seed = 12345;
    
    // First run - limit iterations for test speed
    ExchangeSimulator sim1;
    SimulatorConfig config1;
    config1.seed = seed;
    config1.enable_udp_streaming = false;
    config1.enable_replay_mode = false;
    sim1.configure(config1);
    ASSERT_TRUE(sim1.initialize());
    
    // Run for a limited number of events
    uint64_t iterations = 0;
    while (sim1.is_running() && iterations < 50) {
        sim1.run();
        iterations++;
    }
    sim1.shutdown();
    
    auto* log1_ptr = sim1.get_event_log();
    ASSERT_NE(log1_ptr, nullptr);
    auto* log1 = dynamic_cast<DeterministicEventLog*>(log1_ptr);
    ASSERT_NE(log1, nullptr);
    const auto& events1 = log1->get_order_events();
    
    // Second run with same seed
    ExchangeSimulator sim2;
    SimulatorConfig config2;
    config2.seed = seed;
    config2.enable_udp_streaming = false;
    config2.enable_replay_mode = false;
    sim2.configure(config2);
    ASSERT_TRUE(sim2.initialize());
    
    iterations = 0;
    while (sim2.is_running() && iterations < 50) {
        sim2.run();
        iterations++;
    }
    sim2.shutdown();
    
    auto* log2_ptr = sim2.get_event_log();
    ASSERT_NE(log2_ptr, nullptr);
    auto* log2 = dynamic_cast<DeterministicEventLog*>(log2_ptr);
    ASSERT_NE(log2, nullptr);
    const auto& events2 = log2->get_order_events();
    
    // Events should be identical (at least in count for now)
    // Full determinism test would compare all event fields
    EXPECT_EQ(events1.size(), events2.size());
    
    // Compare first few events if we have any
    if (!events1.empty() && !events2.empty()) {
        size_t compare_count = std::min(events1.size(), events2.size());
        compare_count = std::min(compare_count, size_t(10));  // Compare first 10
        
        for (size_t i = 0; i < compare_count; ++i) {
            EXPECT_EQ(events1[i].order_id.value, events2[i].order_id.value);
            EXPECT_EQ(events1[i].type, events2[i].type);
            EXPECT_EQ(events1[i].side, events2[i].side);
            EXPECT_EQ(events1[i].price.value, events2[i].price.value);
            EXPECT_EQ(events1[i].quantity.value, events2[i].quantity.value);
        }
    }
}

} // namespace test
} // namespace exchange

