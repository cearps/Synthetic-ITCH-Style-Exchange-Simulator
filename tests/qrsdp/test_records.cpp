#include <gtest/gtest.h>
#include "qrsdp/records.h"
#include "qrsdp/event_types.h"
#include <type_traits>

namespace qrsdp {
namespace test {

TEST(QrsdpRecords, EventRecordIsTriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<EventRecord>);
}

TEST(QrsdpRecords, EventRecordFixedSize) {
    static_assert(sizeof(EventRecord) == 30, "EventRecord must be 30 bytes packed");
    EXPECT_EQ(sizeof(EventRecord), 30u);
}

TEST(QrsdpRecords, EventRecordNoPointers) {
    static_assert(sizeof(EventRecord) == 30u);
}

TEST(QrsdpRecords, IntensitiesTotalAndAt) {
    Intensities i{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    EXPECT_DOUBLE_EQ(i.total(), 21.0);
    EXPECT_DOUBLE_EQ(i.at(EventType::ADD_BID), 1.0);
    EXPECT_DOUBLE_EQ(i.at(EventType::EXECUTE_SELL), 6.0);
}

TEST(QrsdpRecords, TradingSessionAndSessionResult) {
    TradingSession s{};
    s.seed = 42;
    s.p0_ticks = 10000;
    s.session_seconds = 3600;
    s.levels_per_side = 5;
    s.tick_size = 100;
    EXPECT_EQ(s.p0_ticks, 10000);
    EXPECT_EQ(s.levels_per_side, 5u);

    SessionResult r{10001, 1000};
    EXPECT_EQ(r.close_ticks, 10001);
    EXPECT_EQ(r.events_written, 1000u);
}

TEST(QrsdpRecords, BookFeaturesAndLevel) {
    BookFeatures f{9999, 10001, 50, 50, 2, 0.0};
    EXPECT_EQ(f.spread_ticks, 2);
    EXPECT_EQ(f.q_bid_best, 50u);

    Level l{9999, 10};
    EXPECT_EQ(l.price_ticks, 9999);
    EXPECT_EQ(l.depth, 10u);
}

TEST(QrsdpRecords, SimEventAndEventAttrs) {
    SimEvent e{EventType::ADD_BID, Side::BID, 9999, 1, 1};
    EXPECT_EQ(e.type, EventType::ADD_BID);
    EXPECT_EQ(e.price_ticks, 9999);

    EventAttrs a{Side::ASK, 10001, 1, 2};
    EXPECT_EQ(a.side, Side::ASK);
    EXPECT_EQ(a.order_id, 2u);
}

}  // namespace test
}  // namespace qrsdp
