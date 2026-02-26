#include <gtest/gtest.h>
#include "itch/itch_encoder.h"
#include "itch/itch_messages.h"
#include "itch/endian.h"
#include "core/event_types.h"
#include "core/records.h"

#include <cstring>

namespace qrsdp {
namespace itch {
namespace test {

static EventRecord makeRecord(EventType type, uint64_t ts, uint64_t order_id,
                              int32_t price_ticks, uint32_t qty) {
    EventRecord r{};
    r.ts_ns = ts;
    r.type = static_cast<uint8_t>(type);
    r.side = (type == EventType::ADD_BID || type == EventType::CANCEL_BID ||
              type == EventType::EXECUTE_BUY)
                 ? static_cast<uint8_t>(Side::BID)
                 : static_cast<uint8_t>(Side::ASK);
    r.price_ticks = price_ticks;
    r.qty = qty;
    r.order_id = order_id;
    r.flags = 0;
    return r;
}

TEST(ItchEncoder, AddBidProduces36ByteAddOrder) {
    ItchEncoder enc("AAPL", 1, 100);
    auto rec = makeRecord(EventType::ADD_BID, 1000000, 42, 10050, 10);
    auto bytes = enc.encode(rec);

    ASSERT_EQ(bytes.size(), sizeof(AddOrderMsg));

    AddOrderMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));

    EXPECT_EQ(msg.message_type, kMsgTypeAddOrder);
    EXPECT_EQ(betoh16(msg.stock_locate), 1);
    EXPECT_EQ(betoh64(msg.order_reference), 42u);
    EXPECT_EQ(msg.buy_sell, 'B');
    EXPECT_EQ(betoh32(msg.shares), 10u);
    EXPECT_EQ(betoh32(msg.price), 10050u * 100);

    char stock[9] = {};
    std::memcpy(stock, msg.stock, 8);
    EXPECT_STREQ(stock, "AAPL    ");
}

TEST(ItchEncoder, AddAskSetsSellindicator) {
    ItchEncoder enc("MSFT", 2, 100);
    auto rec = makeRecord(EventType::ADD_ASK, 2000000, 99, 15000, 5);
    auto bytes = enc.encode(rec);

    AddOrderMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));

    EXPECT_EQ(msg.buy_sell, 'S');
    EXPECT_EQ(betoh64(msg.order_reference), 99u);
}

TEST(ItchEncoder, CancelProduces19ByteOrderDelete) {
    ItchEncoder enc("GOOG", 3, 100);
    auto rec = makeRecord(EventType::CANCEL_BID, 3000000, 77, 20000, 1);
    auto bytes = enc.encode(rec);

    ASSERT_EQ(bytes.size(), sizeof(OrderDeleteMsg));

    OrderDeleteMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));

    EXPECT_EQ(msg.message_type, kMsgTypeOrderDelete);
    EXPECT_EQ(betoh64(msg.order_reference), 77u);
}

TEST(ItchEncoder, CancelAskAlsoProducesOrderDelete) {
    ItchEncoder enc("GOOG", 3, 100);
    auto rec = makeRecord(EventType::CANCEL_ASK, 3000000, 88, 20000, 1);
    auto bytes = enc.encode(rec);

    ASSERT_EQ(bytes.size(), sizeof(OrderDeleteMsg));

    OrderDeleteMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));
    EXPECT_EQ(msg.message_type, kMsgTypeOrderDelete);
}

TEST(ItchEncoder, ExecuteProduces31ByteOrderExecuted) {
    ItchEncoder enc("AAPL", 1, 100);
    auto rec = makeRecord(EventType::EXECUTE_BUY, 5000000, 55, 10000, 20);
    auto bytes = enc.encode(rec);

    ASSERT_EQ(bytes.size(), sizeof(OrderExecutedMsg));

    OrderExecutedMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));

    EXPECT_EQ(msg.message_type, kMsgTypeOrderExecuted);
    EXPECT_EQ(betoh64(msg.order_reference), 55u);
    EXPECT_EQ(betoh32(msg.executed_shares), 20u);
    EXPECT_EQ(betoh64(msg.match_number), 1u);
}

TEST(ItchEncoder, MatchNumberIncrements) {
    ItchEncoder enc("AAPL", 1, 100);

    auto rec1 = makeRecord(EventType::EXECUTE_BUY, 100, 1, 10000, 1);
    auto rec2 = makeRecord(EventType::EXECUTE_SELL, 200, 2, 10000, 1);
    auto rec3 = makeRecord(EventType::EXECUTE_BUY, 300, 3, 10000, 1);

    enc.encode(rec1);
    enc.encode(rec2);
    auto bytes3 = enc.encode(rec3);

    OrderExecutedMsg msg;
    std::memcpy(&msg, bytes3.data(), sizeof(msg));
    EXPECT_EQ(betoh64(msg.match_number), 3u);
    EXPECT_EQ(enc.nextMatchNumber(), 4u);
}

TEST(ItchEncoder, TimestampBigEndian6Bytes) {
    ItchEncoder enc("TEST", 1, 100);
    uint64_t ts = 0x0000AABBCCDDEEFF;
    auto rec = makeRecord(EventType::ADD_BID, ts, 1, 100, 1);
    auto bytes = enc.encode(rec);

    AddOrderMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));

    uint64_t decoded_ts = load48be(msg.timestamp);
    EXPECT_EQ(decoded_ts, ts);
}

TEST(ItchEncoder, SystemEventMessage) {
    ItchEncoder enc("", 0, 100);
    auto bytes = enc.encodeSystemEvent(kSystemEventStartOfMessages, 42000);

    ASSERT_EQ(bytes.size(), sizeof(SystemEventMsg));

    SystemEventMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));
    EXPECT_EQ(msg.message_type, kMsgTypeSystemEvent);
    EXPECT_EQ(msg.event_code, kSystemEventStartOfMessages);
    EXPECT_EQ(load48be(msg.timestamp), 42000u);
}

TEST(ItchEncoder, StockDirectoryMessage) {
    ItchEncoder enc("AAPL", 5, 100);
    auto bytes = enc.encodeStockDirectory(100000);

    ASSERT_EQ(bytes.size(), sizeof(StockDirectoryMsg));

    StockDirectoryMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));
    EXPECT_EQ(msg.message_type, kMsgTypeStockDirectory);
    EXPECT_EQ(betoh16(msg.stock_locate), 5);

    char stock[9] = {};
    std::memcpy(stock, msg.stock, 8);
    EXPECT_STREQ(stock, "AAPL    ");
}

TEST(ItchEncoder, SymbolPaddedWithSpaces) {
    ItchEncoder enc("AB", 1, 100);
    auto rec = makeRecord(EventType::ADD_BID, 100, 1, 100, 1);
    auto bytes = enc.encode(rec);

    AddOrderMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));

    char stock[9] = {};
    std::memcpy(stock, msg.stock, 8);
    EXPECT_STREQ(stock, "AB      ");
}

TEST(ItchEncoder, SymbolTruncatedAt8Chars) {
    ItchEncoder enc("ABCDEFGHIJKLMNOP", 1, 100);
    auto rec = makeRecord(EventType::ADD_BID, 100, 1, 100, 1);
    auto bytes = enc.encode(rec);

    AddOrderMsg msg;
    std::memcpy(&msg, bytes.data(), sizeof(msg));

    char stock[9] = {};
    std::memcpy(stock, msg.stock, 8);
    EXPECT_STREQ(stock, "ABCDEFGH");
}

}  // namespace test
}  // namespace itch
}  // namespace qrsdp
