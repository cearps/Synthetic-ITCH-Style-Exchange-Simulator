#include <gtest/gtest.h>
#include "io/multiplex_sink.h"
#include "io/in_memory_sink.h"
#include "core/records.h"

namespace qrsdp {
namespace test {

static EventRecord makeRecord(uint64_t ts, uint8_t type = 0) {
    EventRecord r{};
    r.ts_ns = ts;
    r.type = type;
    r.side = 0;
    r.price_ticks = 100;
    r.qty = 10;
    r.order_id = ts;
    r.flags = 0;
    return r;
}

TEST(MultiplexSink, FanOutToMultipleSinks) {
    InMemorySink a, b, c;
    MultiplexSink mux;
    mux.addSink(&a);
    mux.addSink(&b);
    mux.addSink(&c);

    EXPECT_EQ(mux.sinkCount(), 3u);

    mux.append(makeRecord(1));
    mux.append(makeRecord(2));

    EXPECT_EQ(a.size(), 2u);
    EXPECT_EQ(b.size(), 2u);
    EXPECT_EQ(c.size(), 2u);
    EXPECT_EQ(a.events()[0].ts_ns, 1u);
    EXPECT_EQ(b.events()[1].ts_ns, 2u);
}

TEST(MultiplexSink, EmptyMuxDoesNotCrash) {
    MultiplexSink mux;
    EXPECT_EQ(mux.sinkCount(), 0u);
    mux.append(makeRecord(42));
    mux.flush();
    mux.close();
}

TEST(MultiplexSink, SingleSinkPassthrough) {
    InMemorySink mem;
    MultiplexSink mux;
    mux.addSink(&mem);

    for (uint64_t i = 0; i < 1000; ++i)
        mux.append(makeRecord(i));

    EXPECT_EQ(mem.size(), 1000u);
    EXPECT_EQ(mem.events()[999].ts_ns, 999u);
}

class ThrowingSink : public IEventSink {
public:
    void append(const EventRecord&) override {
        throw std::runtime_error("intentional test failure");
    }
};

TEST(MultiplexSink, BestEffortOnFailure) {
    ThrowingSink bad;
    InMemorySink good;
    MultiplexSink mux;
    mux.addSink(&bad);
    mux.addSink(&good);

    mux.append(makeRecord(1));
    EXPECT_EQ(good.size(), 1u);
}

}  // namespace test
}  // namespace qrsdp
