#include <gtest/gtest.h>
#include "book/multi_level_book.h"
#include "core/records.h"

namespace qrsdp {
namespace test {

static void assertBookInvariants(const MultiLevelBook& book) {
    const Level bid = book.bestBid();
    const Level ask = book.bestAsk();
    EXPECT_GT(bid.price_ticks, 0) << "best bid price should be positive in test";
    EXPECT_GT(ask.price_ticks, 0) << "best ask price should be positive in test";
    EXPECT_LT(bid.price_ticks, ask.price_ticks) << "bid < ask";
    EXPECT_GE(ask.price_ticks - bid.price_ticks, 1) << "spread >= 1 tick";
    EXPECT_GE(bid.depth, 0u) << "no negative depth (bid)";
    EXPECT_GE(ask.depth, 0u) << "no negative depth (ask)";
}

TEST(QrsdpBook, SeedAndFeatures) {
    MultiLevelBook book;
    BookSeed s{10000, 5, 50, 2};
    book.seed(s);
    BookFeatures f = book.features();
    EXPECT_EQ(f.best_bid_ticks, 9999);
    EXPECT_EQ(f.best_ask_ticks, 10001);
    EXPECT_EQ(f.spread_ticks, 2);
    EXPECT_EQ(f.q_bid_best, 50u);
    EXPECT_EQ(f.q_ask_best, 50u);
    assertBookInvariants(book);
}

TEST(QrsdpBook, AddThenCancelBid) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 10, 2});
    assertBookInvariants(book);
    book.apply(SimEvent{EventType::ADD_BID, Side::BID, 9999, 1, 1});
    BookFeatures f = book.features();
    EXPECT_EQ(f.q_bid_best, 11u);
    book.apply(SimEvent{EventType::CANCEL_BID, Side::BID, 9999, 1, 2});
    f = book.features();
    EXPECT_EQ(f.q_bid_best, 10u);
    assertBookInvariants(book);
}

TEST(QrsdpBook, ExecuteBuyConsumesAsk) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 10, 2});
    EXPECT_EQ(book.bestAsk().depth, 10u);
    book.apply(SimEvent{EventType::EXECUTE_BUY, Side::ASK, 0, 1, 0});
    EXPECT_EQ(book.bestAsk().depth, 9u);
    assertBookInvariants(book);
}

TEST(QrsdpBook, ExecuteSellConsumesBid) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 10, 2});
    EXPECT_EQ(book.bestBid().depth, 10u);
    book.apply(SimEvent{EventType::EXECUTE_SELL, Side::BID, 0, 1, 0});
    EXPECT_EQ(book.bestBid().depth, 9u);
    assertBookInvariants(book);
}

TEST(QrsdpBook, InvariantsAfterManyEvents) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 5, 50, 2});
    uint64_t order_id = 1;
    for (int i = 0; i < 200; ++i) {
        book.apply(SimEvent{EventType::ADD_BID, Side::BID, 9999, 1, order_id++});
        assertBookInvariants(book);
        book.apply(SimEvent{EventType::ADD_ASK, Side::ASK, 10001, 1, order_id++});
        assertBookInvariants(book);
        book.apply(SimEvent{EventType::CANCEL_BID, Side::BID, 9999, 1, order_id++});
        assertBookInvariants(book);
        book.apply(SimEvent{EventType::EXECUTE_BUY, Side::ASK, 0, 1, order_id++});
        assertBookInvariants(book);
        book.apply(SimEvent{EventType::EXECUTE_SELL, Side::BID, 0, 1, order_id++});
        assertBookInvariants(book);
    }
}

TEST(QrsdpBook, ShiftWhenBestDepleted) {
    MultiLevelBook book;
    book.seed(BookSeed{10000, 3, 1, 2});
    EXPECT_EQ(book.bestBid().price_ticks, 9999);
    EXPECT_EQ(book.bestBid().depth, 1u);
    book.apply(SimEvent{EventType::EXECUTE_SELL, Side::BID, 0, 1, 0});
    EXPECT_EQ(book.bestBid().price_ticks, 9998) << "book shifts; new best bid one tick down";
    book.apply(SimEvent{EventType::EXECUTE_SELL, Side::BID, 0, 1, 0});
    Level bid = book.bestBid();
    EXPECT_EQ(bid.price_ticks, 9997) << "second shift moves best bid down again";
    EXPECT_GE(bid.depth, 1u);
    assertBookInvariants(book);
}

}  // namespace test
}  // namespace qrsdp
