#include <gtest/gtest.h>
#include "qrsdp/qrsdp_producer.h"
#include "qrsdp/in_memory_sink.h"
#include "qrsdp/multi_level_book.h"
#include "qrsdp/simple_imbalance_intensity.h"
#include "qrsdp/hlr_params.h"
#include "qrsdp/curve_intensity_model.h"
#include "qrsdp/mt19937_rng.h"
#include "qrsdp/competing_intensity_sampler.h"
#include "qrsdp/unit_size_attribute_sampler.h"
#include "qrsdp/records.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace qrsdp {
namespace test {

static TradingSession makeSession(uint64_t seed, uint32_t session_seconds = 10,
                                  uint32_t initial_depth = 0, uint32_t initial_spread_ticks = 2) {
    TradingSession s{};
    s.seed = seed;
    s.p0_ticks = 10000;
    s.session_seconds = session_seconds;
    s.levels_per_side = 5;
    s.tick_size = 100;
    s.initial_spread_ticks = initial_spread_ticks;
    s.initial_depth = initial_depth;
    s.intensity_params.base_L = 20.0;
    s.intensity_params.base_C = 0.1;
    s.intensity_params.base_M = 5.0;
    s.intensity_params.imbalance_sensitivity = 1.0;
    s.intensity_params.cancel_sensitivity = 1.0;
    s.intensity_params.epsilon_exec = 0.05;
    return s;
}

static bool eventRecordsEqual(const EventRecord& a, const EventRecord& b) {
    return a.ts_ns == b.ts_ns && a.type == b.type && a.side == b.side &&
           a.price_ticks == b.price_ticks && a.qty == b.qty && a.order_id == b.order_id &&
           a.flags == b.flags;
}

TEST(QrsdpProducer, DeterminismSameSeed) {
    const TradingSession session = makeSession(12345, 10);
    const size_t compareLimit = 10000;

    Mt19937Rng rng1(session.seed);
    Mt19937Rng rng2(session.seed);
    MultiLevelBook book1;
    MultiLevelBook book2;
    IntensityParams p = session.intensity_params;
    SimpleImbalanceIntensity model1(p);
    SimpleImbalanceIntensity model2(p);
    CompetingIntensitySampler sampler1(rng1);
    CompetingIntensitySampler sampler2(rng2);
    UnitSizeAttributeSampler attr1(rng1, 0.5);
    UnitSizeAttributeSampler attr2(rng2, 0.5);

    QrsdpProducer producer1(rng1, book1, model1, sampler1, attr1);
    QrsdpProducer producer2(rng2, book2, model2, sampler2, attr2);

    InMemorySink sink1;
    InMemorySink sink2;
    SessionResult r1 = producer1.runSession(session, sink1);
    SessionResult r2 = producer2.runSession(session, sink2);

    const size_t n = std::min({sink1.size(), sink2.size(), compareLimit});
    ASSERT_GT(n, 0u) << "expected at least one event";
    for (size_t i = 0; i < n; ++i) {
        EXPECT_TRUE(eventRecordsEqual(sink1.events()[i], sink2.events()[i]))
            << "record " << i << " differs";
    }
    EXPECT_EQ(r1.close_ticks, r2.close_ticks);
    EXPECT_EQ(r1.events_written, r2.events_written);
}

TEST(QrsdpProducer, DifferentSeedDifferentStream) {
    const TradingSession session1 = makeSession(111, 5);
    const TradingSession session2 = makeSession(222, 5);

    Mt19937Rng rng1(session1.seed);
    Mt19937Rng rng2(session2.seed);
    MultiLevelBook book1;
    MultiLevelBook book2;
    IntensityParams p = session1.intensity_params;
    SimpleImbalanceIntensity model1(p);
    SimpleImbalanceIntensity model2(p);
    CompetingIntensitySampler sampler1(rng1);
    CompetingIntensitySampler sampler2(rng2);
    UnitSizeAttributeSampler attr1(rng1, 0.5);
    UnitSizeAttributeSampler attr2(rng2, 0.5);

    QrsdpProducer producer1(rng1, book1, model1, sampler1, attr1);
    QrsdpProducer producer2(rng2, book2, model2, sampler2, attr2);

    InMemorySink sink1;
    InMemorySink sink2;
    producer1.runSession(session1, sink1);
    producer2.runSession(session2, sink2);

    const size_t n = std::min(sink1.size(), sink2.size());
    if (n == 0) return;
    bool diff = false;
    for (size_t i = 0; i < n && !diff; ++i) {
        if (!eventRecordsEqual(sink1.events()[i], sink2.events()[i])) diff = true;
    }
    EXPECT_TRUE(diff) << "streams should differ with different seeds";
}

TEST(QrsdpProducer, IntegrationEventsWrittenAndCloseValid) {
    const TradingSession session = makeSession(9999, 5);
    Mt19937Rng rng(session.seed);
    MultiLevelBook book;
    SimpleImbalanceIntensity model(session.intensity_params);
    CompetingIntensitySampler eventSampler(rng);
    UnitSizeAttributeSampler attrSampler(rng, 0.5);
    QrsdpProducer producer(rng, book, model, eventSampler, attrSampler);
    InMemorySink sink;

    SessionResult result = producer.runSession(session, sink);

    EXPECT_GT(result.events_written, 0u) << "expected at least one event in 5 seconds";
    const Level bid = book.bestBid();
    const Level ask = book.bestAsk();
    EXPECT_LT(bid.price_ticks, ask.price_ticks) << "bid < ask at session end";
    EXPECT_GE(ask.price_ticks - bid.price_ticks, 1) << "spread >= 1 tick";
    EXPECT_GE(result.close_ticks, bid.price_ticks);
    EXPECT_LE(result.close_ticks, ask.price_ticks);
    EXPECT_EQ(sink.size(), result.events_written);
}

TEST(QrsdpProducer, InMemorySinkAccumulates) {
    InMemorySink sink;
    EXPECT_EQ(sink.size(), 0u);
    EventRecord r1{1000, 0, 0, 9999, 1, 1, 0};
    sink.append(r1);
    EXPECT_EQ(sink.size(), 1u);
    EventRecord r2{2000, 1, 1, 10001, 1, 2, 0};
    sink.append(r2);
    EXPECT_EQ(sink.size(), 2u);
    EXPECT_EQ(sink.events()[0].ts_ns, 1000u);
    EXPECT_EQ(sink.events()[1].price_ticks, 10001);
}

static SimEvent recordToSimEvent(const EventRecord& r) {
    SimEvent ev;
    ev.type = static_cast<EventType>(r.type);
    ev.side = static_cast<Side>(r.side);
    ev.price_ticks = r.price_ticks;
    ev.qty = r.qty;
    ev.order_id = r.order_id;
    return ev;
}

// Runs a short session with initial_depth=1, replays events, prints first 20 events and
// best bid/ask after each for debugging. Asserts session produces events and replayed
// book keeps bid < ask and spread >= 1. Shift-on-depletion is unit-tested in
// QrsdpBook.ShiftWhenBestDepleted.
TEST(QrsdpProducer, TraceShiftAndPrintFirst20) {
    const uint32_t session_seconds = 2;
    const uint32_t initial_depth = 1;
    const uint32_t initial_spread_ticks = 2;
    const size_t print_limit = 20u;

    TradingSession session = makeSession(777, session_seconds, initial_depth, initial_spread_ticks);
    Mt19937Rng rng(session.seed);
    MultiLevelBook book;
    SimpleImbalanceIntensity model(session.intensity_params);
    CompetingIntensitySampler eventSampler(rng);
    UnitSizeAttributeSampler attrSampler(rng, 0.5);
    QrsdpProducer producer(rng, book, model, eventSampler, attrSampler);
    InMemorySink sink;

    SessionResult result = producer.runSession(session, sink);

    ASSERT_GT(sink.size(), 0u) << "need at least one event to trace";

    MultiLevelBook replayBook;
    BookSeed seed;
    seed.p0_ticks = session.p0_ticks;
    seed.levels_per_side = session.levels_per_side;
    seed.initial_depth = initial_depth;
    seed.initial_spread_ticks = session.initial_spread_ticks;
    replayBook.seed(seed);

    int shift_count = 0;
    for (size_t i = 0; i < sink.size(); ++i) {
        const EventRecord& rec = sink.events()[i];
        const int32_t prev_bid = replayBook.bestBid().price_ticks;
        const int32_t prev_ask = replayBook.bestAsk().price_ticks;
        if (i < print_limit) {
            std::printf("[%zu] ts_ns=%llu type=%u side=%u price=%d qty=%u order_id=%llu\n",
                        i, (unsigned long long)rec.ts_ns, rec.type, rec.side, rec.price_ticks, rec.qty, (unsigned long long)rec.order_id);
        }
        SimEvent ev = recordToSimEvent(rec);
        replayBook.apply(ev);
        const Level bid = replayBook.bestBid();
        const Level ask = replayBook.bestAsk();
        if (bid.price_ticks != prev_bid || ask.price_ticks != prev_ask) ++shift_count;
        if (i < print_limit) {
            std::printf("    -> best_bid=%d (depth=%u) best_ask=%d (depth=%u)\n",
                        bid.price_ticks, bid.depth, ask.price_ticks, ask.depth);
        }
        EXPECT_LT(bid.price_ticks, ask.price_ticks) << "bid < ask after event " << i;
        EXPECT_GE(ask.price_ticks - bid.price_ticks, 1) << "spread >= 1 after event " << i;
    }

    std::printf("events_written=%llu close_ticks=%d shift_count=%d\n",
                (unsigned long long)result.events_written, result.close_ticks, shift_count);
}

// --- HLR2014 Model I (curve intensity) tests ---
TEST(QrsdpProducer, CurveModelDeterminismSameSeed) {
    const uint64_t seed = 4242;
    TradingSession session = makeSession(seed, 5);
    session.levels_per_side = 3;

    HLRParams p = makeDefaultHLRParams(3, 50);
    CurveIntensityModel model(p);

    Mt19937Rng rng1(seed);
    Mt19937Rng rng2(seed);
    MultiLevelBook book1;
    MultiLevelBook book2;
    CompetingIntensitySampler sampler1(rng1);
    CompetingIntensitySampler sampler2(rng2);
    UnitSizeAttributeSampler attr1(rng1, 0.5);
    UnitSizeAttributeSampler attr2(rng2, 0.5);

    QrsdpProducer producer1(rng1, book1, model, sampler1, attr1);
    QrsdpProducer producer2(rng2, book2, model, sampler2, attr2);

    InMemorySink sink1;
    InMemorySink sink2;
    SessionResult r1 = producer1.runSession(session, sink1);
    SessionResult r2 = producer2.runSession(session, sink2);

    EXPECT_EQ(sink1.size(), sink2.size());
    EXPECT_EQ(r1.events_written, r2.events_written);
    const size_t n = std::min(sink1.size(), sink2.size());
    for (size_t i = 0; i < n && i < 200u; ++i) {
        EXPECT_TRUE(eventRecordsEqual(sink1.events()[i], sink2.events()[i]))
            << "curve model record " << i << " differs";
    }
}

TEST(QrsdpProducer, CurveModelInvariants) {
    const uint64_t seed = 1111;
    TradingSession session = makeSession(seed, 3);
    session.levels_per_side = 3;
    session.initial_depth = 10;

    HLRParams p = makeDefaultHLRParams(3, 50);
    CurveIntensityModel model(p);
    Mt19937Rng rng(seed);
    MultiLevelBook book;
    CompetingIntensitySampler eventSampler(rng);
    UnitSizeAttributeSampler attrSampler(rng, 0.5);
    QrsdpProducer producer(rng, book, model, eventSampler, attrSampler);
    InMemorySink sink;

    producer.runSession(session, sink);

    const Level bid = book.bestBid();
    const Level ask = book.bestAsk();
    EXPECT_LT(bid.price_ticks, ask.price_ticks) << "best bid < best ask";
    EXPECT_GE(ask.price_ticks - bid.price_ticks, 1) << "spread >= 1 tick";
    for (size_t k = 0; k < book.numLevels(); ++k) {
        EXPECT_GE(book.bidDepthAtLevel(k), 0u) << "bid depth nonneg at level " << k;
        EXPECT_GE(book.askDepthAtLevel(k), 0u) << "ask depth nonneg at level " << k;
    }
}

TEST(QrsdpProducer, QueueReactiveThetaReinitOneAlwaysReinit) {
    TradingSession session = makeSession(5555, 15);
    session.levels_per_side = 2;
    session.initial_depth = 1;
    session.intensity_params.base_M = 40.0;
    session.intensity_params.epsilon_exec = 0.5;
    session.queue_reactive.theta_reinit = 1.0;
    session.queue_reactive.reinit_depth_mean = 5.0;

    Mt19937Rng rng(session.seed);
    MultiLevelBook book;
    SimpleImbalanceIntensity model(session.intensity_params);
    CompetingIntensitySampler eventSampler(rng);
    UnitSizeAttributeSampler attrSampler(rng, 0.5);
    QrsdpProducer producer(rng, book, model, eventSampler, attrSampler);
    InMemorySink sink;

    producer.runSession(session, sink);

    const Level bid = book.bestBid();
    const Level ask = book.bestAsk();
    EXPECT_LT(bid.price_ticks, ask.price_ticks);
    EXPECT_GE(ask.price_ticks - bid.price_ticks, 1);
    EXPECT_GT(sink.size(), 0u);
}

TEST(QrsdpProducer, QueueReactiveThetaReinitZeroNoReinit) {
    TradingSession session = makeSession(6666, 3);
    session.levels_per_side = 2;
    session.initial_depth = 1;
    session.queue_reactive.theta_reinit = 0.0;

    Mt19937Rng rng(session.seed);
    MultiLevelBook book;
    SimpleImbalanceIntensity model(session.intensity_params);
    CompetingIntensitySampler eventSampler(rng);
    UnitSizeAttributeSampler attrSampler(rng, 0.5);
    QrsdpProducer producer(rng, book, model, eventSampler, attrSampler);
    InMemorySink sink;

    producer.runSession(session, sink);

    const Level bid = book.bestBid();
    const Level ask = book.bestAsk();
    EXPECT_LT(bid.price_ticks, ask.price_ticks);
    EXPECT_GE(ask.price_ticks - bid.price_ticks, 1);
}

TEST(QrsdpProducer, CurveModelSmoke) {
    const uint64_t seed = 9999;
    TradingSession session = makeSession(seed, 2);
    session.levels_per_side = 5;
    session.initial_depth = 20;

    HLRParams p = makeDefaultHLRParams(5, 100);
    CurveIntensityModel model(p);
    Mt19937Rng rng(seed);
    MultiLevelBook book;
    CompetingIntensitySampler eventSampler(rng);
    UnitSizeAttributeSampler attrSampler(rng, 0.5);
    QrsdpProducer producer(rng, book, model, eventSampler, attrSampler);
    InMemorySink sink;

    SessionResult result = producer.runSession(session, sink);

    EXPECT_GT(result.events_written, 0u) << "expected events in 2 seconds";
    EXPECT_LT(result.events_written, 50000u) << "no explosion";
    const Level bid = book.bestBid();
    const Level ask = book.bestAsk();
    EXPECT_LT(bid.price_ticks, ask.price_ticks);
    EXPECT_GE(ask.price_ticks - bid.price_ticks, 1);
}

}  // namespace test
}  // namespace qrsdp
