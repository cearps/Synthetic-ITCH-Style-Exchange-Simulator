#include <gtest/gtest.h>
#include "sampler/unit_size_attribute_sampler.h"
#include "book/multi_level_book.h"
#include "rng/mt19937_rng.h"
#include "core/records.h"

namespace qrsdp {
namespace test {

TEST(QrsdpAttributeSampler, QtyAlwaysOne) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 50, 2});
    Mt19937Rng rng(111);
    UnitSizeAttributeSampler sampler(rng, 0.5);
    BookFeatures f = book.features();
    for (int i = 0; i < 20; ++i) {
        EventAttrs a = sampler.sample(EventType::ADD_BID, book, f);
        EXPECT_EQ(a.qty, 1u);
        a = sampler.sample(EventType::EXECUTE_BUY, book, f);
        EXPECT_EQ(a.qty, 1u);
    }
}

TEST(QrsdpAttributeSampler, AddBidReturnsBidSideAndValidPrice) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 50, 2});
    Mt19937Rng rng(222);
    UnitSizeAttributeSampler sampler(rng, 0.5);
    BookFeatures f = book.features();
    for (int i = 0; i < 50; ++i) {
        EventAttrs a = sampler.sample(EventType::ADD_BID, book, f);
        EXPECT_EQ(a.side, Side::BID);
        EXPECT_GE(a.price_ticks, book.bidPriceAtLevel(4));
        EXPECT_LE(a.price_ticks, book.bidPriceAtLevel(0));
    }
}

TEST(QrsdpAttributeSampler, ExecuteBuyReturnsAskPrice) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 50, 2});
    Mt19937Rng rng(333);
    UnitSizeAttributeSampler sampler(rng, 0.5);
    BookFeatures f = book.features();
    EventAttrs a = sampler.sample(EventType::EXECUTE_BUY, book, f);
    EXPECT_EQ(a.side, Side::ASK);
    EXPECT_EQ(a.price_ticks, f.best_ask_ticks);
}

TEST(QrsdpAttributeSampler, ExecuteSellReturnsBidPrice) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 50, 2});
    Mt19937Rng rng(444);
    UnitSizeAttributeSampler sampler(rng, 0.5);
    BookFeatures f = book.features();
    EventAttrs a = sampler.sample(EventType::EXECUTE_SELL, book, f);
    EXPECT_EQ(a.side, Side::BID);
    EXPECT_EQ(a.price_ticks, f.best_bid_ticks);
}

TEST(QrsdpAttributeSampler, DeterminismSameSeed) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 50, 2});
    BookFeatures f = book.features();
    Mt19937Rng rng1(555);
    Mt19937Rng rng2(555);
    UnitSizeAttributeSampler s1(rng1, 0.3);
    UnitSizeAttributeSampler s2(rng2, 0.3);
    for (int i = 0; i < 20; ++i) {
        EventAttrs a1 = s1.sample(EventType::ADD_BID, book, f);
        EventAttrs a2 = s2.sample(EventType::ADD_BID, book, f);
        EXPECT_EQ(a1.side, a2.side);
        EXPECT_EQ(a1.price_ticks, a2.price_ticks);
        EXPECT_EQ(a1.qty, a2.qty);
    }
}

}  // namespace test
}  // namespace qrsdp
