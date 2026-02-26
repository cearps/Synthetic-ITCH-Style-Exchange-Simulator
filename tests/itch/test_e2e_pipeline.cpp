#include <gtest/gtest.h>

#include "itch/itch_encoder.h"
#include "itch/itch_decoder.h"
#include "itch/itch_messages.h"
#include "itch/moldudp64.h"
#include "itch/endian.h"
#include "core/event_types.h"
#include "core/records.h"
#include "io/event_log_format.h"
#include "io/in_memory_sink.h"
#include "producer/qrsdp_producer.h"
#include "book/multi_level_book.h"
#include "model/simple_imbalance_intensity.h"
#include "rng/mt19937_rng.h"
#include "sampler/competing_intensity_sampler.h"
#include "sampler/unit_size_attribute_sampler.h"

#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    constexpr socket_t kBadSocket = INVALID_SOCKET;
    inline int close_sock(socket_t s) { return closesocket(s); }

    struct WsaGuard {
        WsaGuard()  { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
        ~WsaGuard() { WSACleanup(); }
    };
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using socket_t = int;
    constexpr socket_t kBadSocket = -1;
    inline int close_sock(socket_t s) { return close(s); }

    struct WsaGuard {};
#endif

namespace qrsdp {
namespace itch {
namespace test {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static TradingSession makeSession(uint64_t seed, uint32_t session_seconds = 2) {
    TradingSession s{};
    s.seed = seed;
    s.p0_ticks = 10000;
    s.session_seconds = session_seconds;
    s.levels_per_side = 5;
    s.tick_size = 100;
    s.initial_spread_ticks = 2;
    s.initial_depth = 0;
    s.intensity_params.base_L = 20.0;
    s.intensity_params.base_C = 0.1;
    s.intensity_params.base_M = 5.0;
    s.intensity_params.imbalance_sensitivity = 1.0;
    s.intensity_params.cancel_sensitivity = 1.0;
    s.intensity_params.epsilon_exec = 0.05;
    return s;
}

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

/// Verify that a decoded ITCH message carries the same payload as the
/// original EventRecord that was fed to the encoder.
static void assertFieldsMatch(const EventRecord& orig,
                               const DecodedItchMsg& decoded,
                               uint32_t tick_size,
                               const char* context) {
    SCOPED_TRACE(context);

    auto orig_type = static_cast<EventType>(orig.type);

    EXPECT_EQ(decoded.timestamp_ns, orig.ts_ns) << "timestamp mismatch";
    EXPECT_EQ(decoded.order_reference, orig.order_id) << "order_id mismatch";

    switch (orig_type) {
    case EventType::ADD_BID:
    case EventType::ADD_ASK: {
        EXPECT_EQ(decoded.msg_type, kMsgTypeAddOrder);
        char expected_side = (orig_type == EventType::ADD_BID) ? 'B' : 'S';
        EXPECT_EQ(decoded.buy_sell, expected_side) << "buy/sell mismatch";
        EXPECT_EQ(decoded.shares, orig.qty) << "shares mismatch";
        uint32_t expected_price = static_cast<uint32_t>(orig.price_ticks) * tick_size;
        EXPECT_EQ(decoded.price, expected_price) << "price mismatch";
        break;
    }
    case EventType::CANCEL_BID:
    case EventType::CANCEL_ASK:
        EXPECT_EQ(decoded.msg_type, kMsgTypeOrderDelete);
        break;
    case EventType::EXECUTE_BUY:
    case EventType::EXECUTE_SELL:
        EXPECT_EQ(decoded.msg_type, kMsgTypeOrderExecuted);
        EXPECT_EQ(decoded.shares, orig.qty) << "executed_shares mismatch";
        break;
    }
}

/// Expected ITCH message type for a given EventType.
static char expectedItchMsgType(EventType t) {
    switch (t) {
    case EventType::ADD_BID:
    case EventType::ADD_ASK:     return kMsgTypeAddOrder;
    case EventType::CANCEL_BID:
    case EventType::CANCEL_ASK:  return kMsgTypeOrderDelete;
    case EventType::EXECUTE_BUY:
    case EventType::EXECUTE_SELL: return kMsgTypeOrderExecuted;
    }
    return 0;
}

/// Run a short producer session and return the captured events.
static std::vector<EventRecord> generateEvents(uint64_t seed,
                                                uint32_t session_seconds = 2) {
    TradingSession session = makeSession(seed, session_seconds);
    Mt19937Rng rng(session.seed);
    MultiLevelBook book;
    SimpleImbalanceIntensity model(session.intensity_params);
    CompetingIntensitySampler eventSampler(rng);
    UnitSizeAttributeSampler attrSampler(rng, 0.5);
    QrsdpProducer producer(rng, book, model, eventSampler, attrSampler);
    InMemorySink sink;
    producer.runSession(session, sink);
    return sink.events();
}

// ---------------------------------------------------------------------------
// Test A: DiskEventRecord roundtrip
// ---------------------------------------------------------------------------

TEST(E2EPipeline, DiskEventRecordRoundtrip) {
    const EventType types[] = {
        EventType::ADD_BID, EventType::ADD_ASK,
        EventType::CANCEL_BID, EventType::CANCEL_ASK,
        EventType::EXECUTE_BUY, EventType::EXECUTE_SELL
    };

    for (auto t : types) {
        SCOPED_TRACE(static_cast<int>(t));
        EventRecord orig = makeRecord(t, 123456789ULL, 42, 5000, 17);
        orig.flags = 0x7;

        DiskEventRecord disk{};
        disk.ts_ns       = orig.ts_ns;
        disk.type        = orig.type;
        disk.side        = orig.side;
        disk.price_ticks = orig.price_ticks;
        disk.qty         = orig.qty;
        disk.order_id    = orig.order_id;

        EventRecord reconstructed{};
        reconstructed.ts_ns       = disk.ts_ns;
        reconstructed.type        = disk.type;
        reconstructed.side        = disk.side;
        reconstructed.price_ticks = disk.price_ticks;
        reconstructed.qty         = disk.qty;
        reconstructed.order_id    = disk.order_id;
        reconstructed.flags       = 0;

        EXPECT_EQ(reconstructed.ts_ns, orig.ts_ns);
        EXPECT_EQ(reconstructed.type, orig.type);
        EXPECT_EQ(reconstructed.side, orig.side);
        EXPECT_EQ(reconstructed.price_ticks, orig.price_ticks);
        EXPECT_EQ(reconstructed.qty, orig.qty);
        EXPECT_EQ(reconstructed.order_id, orig.order_id);
        EXPECT_EQ(reconstructed.flags, 0u) << "flags must be zero after roundtrip";
    }
}

// ---------------------------------------------------------------------------
// Test B: Encoder → Decoder field alignment for every event type
// ---------------------------------------------------------------------------

TEST(E2EPipeline, EncoderDecoderFieldAlignment) {
    const uint32_t tick_size = 100;

    struct Case {
        EventType type;
        uint64_t  ts;
        uint64_t  order_id;
        int32_t   price_ticks;
        uint32_t  qty;
    };

    const Case cases[] = {
        { EventType::ADD_BID,      1000000ULL,  42,  10050, 10 },
        { EventType::ADD_ASK,      2000000ULL,  99,  15000,  5 },
        { EventType::CANCEL_BID,   3000000ULL,  77,  20000,  1 },
        { EventType::CANCEL_ASK,   4000000ULL,  88,  20000,  1 },
        { EventType::EXECUTE_BUY,  5000000ULL,  55,  10000, 20 },
        { EventType::EXECUTE_SELL, 6000000ULL, 101,  12345,  3 },
    };

    ItchEncoder encoder("TEST", 1, tick_size);

    for (const auto& c : cases) {
        SCOPED_TRACE(static_cast<int>(c.type));
        EventRecord rec = makeRecord(c.type, c.ts, c.order_id, c.price_ticks, c.qty);
        auto bytes = encoder.encode(rec);

        DecodedItchMsg decoded;
        ASSERT_TRUE(decodeItchMessage(bytes.data(), bytes.size(), decoded))
            << "decode failed for type " << static_cast<int>(c.type);

        EXPECT_EQ(decoded.msg_type, expectedItchMsgType(c.type));
        assertFieldsMatch(rec, decoded, tick_size, "EncoderDecoderFieldAlignment");
    }
}

// ---------------------------------------------------------------------------
// Test C: Producer → ITCH encode → decode trace for first N events
// ---------------------------------------------------------------------------

TEST(E2EPipeline, ProducerToItchTraceFirstN) {
    const uint32_t tick_size = 100;
    const size_t trace_limit = 200;

    auto events = generateEvents(12345);
    ASSERT_GT(events.size(), 0u);

    ItchEncoder encoder("SYN", 1, tick_size);
    size_t n = std::min(events.size(), trace_limit);

    for (size_t i = 0; i < n; ++i) {
        SCOPED_TRACE(i);
        const EventRecord& rec = events[i];
        auto bytes = encoder.encode(rec);

        DecodedItchMsg decoded;
        ASSERT_TRUE(decodeItchMessage(bytes.data(), bytes.size(), decoded))
            << "decode failed at event " << i;

        assertFieldsMatch(rec, decoded, tick_size, "ProducerToItchTrace");
    }
}

// ---------------------------------------------------------------------------
// Test D: Producer → ITCH → MoldUDP64 frame → extract → decode
// ---------------------------------------------------------------------------

TEST(E2EPipeline, ProducerToMoldUDP64Roundtrip) {
    const uint32_t tick_size = 100;
    const size_t trace_limit = 100;

    auto events = generateEvents(54321);
    ASSERT_GT(events.size(), 0u);

    ItchEncoder encoder("MOLD", 2, tick_size);
    MoldUDP64Framer framer("E2E_MOLD  ");

    size_t n = std::min(events.size(), trace_limit);

    // Encode all events and collect raw ITCH bytes for later comparison
    std::vector<std::vector<uint8_t>> encoded_msgs;
    encoded_msgs.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto bytes = encoder.encode(events[i]);
        encoded_msgs.push_back(bytes);
        framer.addMessage(bytes.data(), static_cast<uint16_t>(bytes.size()));
    }

    // Flush remaining messages. The framer may have auto-flushed some
    // packets already, so we collect all packets via the send callback
    // plus the final flush.
    std::vector<std::vector<uint8_t>> packets;
    framer.setSendCallback([&](const uint8_t* data, size_t len) {
        packets.emplace_back(data, data + len);
    });

    // Re-encode to capture packets properly via callback
    MoldUDP64Framer framer2("E2E_MOLD  ");
    framer2.setSendCallback([&](const uint8_t* data, size_t len) {
        packets.emplace_back(data, data + len);
    });
    for (size_t i = 0; i < n; ++i) {
        framer2.addMessage(encoded_msgs[i].data(),
                           static_cast<uint16_t>(encoded_msgs[i].size()));
    }
    auto last_pkt = framer2.flush();
    if (!last_pkt.empty())
        packets.push_back(last_pkt);

    // Parse all packets and decode every message
    size_t decoded_idx = 0;
    for (const auto& pkt : packets) {
        MoldUDP64Parsed parsed;
        ASSERT_TRUE(parseMoldUDP64(pkt.data(), pkt.size(), parsed))
            << "MoldUDP64 parse failed";

        for (const auto& span : parsed.messages) {
            ASSERT_LT(decoded_idx, n) << "more decoded messages than encoded";
            SCOPED_TRACE(decoded_idx);

            DecodedItchMsg decoded;
            ASSERT_TRUE(decodeItchMessage(span.data, span.size, decoded));
            assertFieldsMatch(events[decoded_idx], decoded, tick_size,
                              "ProducerToMoldUDP64Roundtrip");
            ++decoded_idx;
        }
    }
    EXPECT_EQ(decoded_idx, n) << "message count mismatch after MoldUDP64 roundtrip";
}

// ---------------------------------------------------------------------------
// Test E: Full pipeline over localhost UDP
// ---------------------------------------------------------------------------

TEST(E2EPipeline, ProducerToUdpFullPipeline) {
    WsaGuard wsa; (void)wsa;

    const uint32_t tick_size = 100;
    const size_t trace_limit = 50;

    auto events = generateEvents(99999, 1);
    ASSERT_GT(events.size(), 0u);
    size_t n = std::min(events.size(), trace_limit);

    // Encode events
    ItchEncoder encoder("UDP", 3, tick_size);
    std::vector<std::vector<uint8_t>> encoded_msgs;
    for (size_t i = 0; i < n; ++i)
        encoded_msgs.push_back(encoder.encode(events[i]));

    // Frame into MoldUDP64 packets
    MoldUDP64Framer framer("E2E_UDP   ");
    std::vector<std::vector<uint8_t>> packets;
    framer.setSendCallback([&](const uint8_t* data, size_t len) {
        packets.emplace_back(data, data + len);
    });
    for (size_t i = 0; i < n; ++i) {
        framer.addMessage(encoded_msgs[i].data(),
                          static_cast<uint16_t>(encoded_msgs[i].size()));
    }
    auto last_pkt = framer.flush();
    if (!last_pkt.empty())
        packets.push_back(last_pkt);

    ASSERT_FALSE(packets.empty());

    // Set up receiver socket
    socket_t rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_NE(rx, kBadSocket);

    struct sockaddr_in rx_addr{};
    rx_addr.sin_family = AF_INET;
    rx_addr.sin_port = 0;
    rx_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(rx, reinterpret_cast<struct sockaddr*>(&rx_addr),
                   sizeof(rx_addr)), 0);

    socklen_t addr_len = sizeof(rx_addr);
    getsockname(rx, reinterpret_cast<struct sockaddr*>(&rx_addr), &addr_len);
    uint16_t port = ntohs(rx_addr.sin_port);

#ifdef _WIN32
    DWORD timeout_ms = 2000;
    setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    struct timeval tv { 2, 0 };
    setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    // Set up sender socket
    socket_t tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_NE(tx, kBadSocket);

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Send all packets
    for (const auto& pkt : packets) {
        auto sent = sendto(tx, reinterpret_cast<const char*>(pkt.data()),
                           static_cast<int>(pkt.size()), 0,
                           reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
        EXPECT_EQ(sent, static_cast<decltype(sent)>(pkt.size()));
    }

    // Receive and decode all packets
    size_t decoded_count = 0;
    uint8_t buf[2048];

    for (size_t p = 0; p < packets.size(); ++p) {
        auto recv_n = recv(rx, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        ASSERT_GT(recv_n, static_cast<decltype(recv_n)>(kMoldUDP64HeaderSize))
            << "recv failed for packet " << p;

        MoldUDP64Parsed parsed;
        ASSERT_TRUE(parseMoldUDP64(buf, static_cast<size_t>(recv_n), parsed));

        for (const auto& span : parsed.messages) {
            ASSERT_LT(decoded_count, n);
            SCOPED_TRACE(decoded_count);

            DecodedItchMsg decoded;
            ASSERT_TRUE(decodeItchMessage(span.data, span.size, decoded));
            assertFieldsMatch(events[decoded_count], decoded, tick_size,
                              "ProducerToUdpFullPipeline");
            ++decoded_count;
        }
    }

    EXPECT_EQ(decoded_count, n)
        << "total decoded messages should match total sent";

    close_sock(tx);
    close_sock(rx);
}

// ---------------------------------------------------------------------------
// Test F: Multi-symbol pipeline trace
// ---------------------------------------------------------------------------

TEST(E2EPipeline, MultiSymbolPipelineTrace) {
    const uint32_t tick_size_a = 100;
    const uint32_t tick_size_b = 50;

    ItchEncoder enc_a("AAPL", 1, tick_size_a);
    ItchEncoder enc_b("GOOG", 2, tick_size_b);

    EventRecord rec_a = makeRecord(EventType::ADD_BID, 1000, 10, 5000, 7);
    EventRecord rec_b = makeRecord(EventType::ADD_ASK, 2000, 20, 8000, 3);

    auto bytes_a = enc_a.encode(rec_a);
    auto bytes_b = enc_b.encode(rec_b);

    // Frame both in one MoldUDP64 packet
    MoldUDP64Framer framer("MULTI_SYM ");
    framer.addMessage(bytes_a.data(), static_cast<uint16_t>(bytes_a.size()));
    framer.addMessage(bytes_b.data(), static_cast<uint16_t>(bytes_b.size()));
    auto packet = framer.flush();
    ASSERT_FALSE(packet.empty());

    MoldUDP64Parsed parsed;
    ASSERT_TRUE(parseMoldUDP64(packet.data(), packet.size(), parsed));
    ASSERT_EQ(parsed.messages.size(), 2u);

    // Decode and verify symbol A
    {
        DecodedItchMsg d;
        ASSERT_TRUE(decodeItchMessage(parsed.messages[0].data,
                                       parsed.messages[0].size, d));
        EXPECT_EQ(d.stock_locate, 1u);
        EXPECT_EQ(d.timestamp_ns, 1000u);
        EXPECT_EQ(d.order_reference, 10u);
        EXPECT_EQ(d.buy_sell, 'B');
        EXPECT_EQ(d.shares, 7u);
        EXPECT_EQ(d.price, 5000u * tick_size_a);

        char stock[9] = {};
        std::memcpy(stock, d.stock, 8);
        EXPECT_STREQ(stock, "AAPL    ");
    }

    // Decode and verify symbol B
    {
        DecodedItchMsg d;
        ASSERT_TRUE(decodeItchMessage(parsed.messages[1].data,
                                       parsed.messages[1].size, d));
        EXPECT_EQ(d.stock_locate, 2u);
        EXPECT_EQ(d.timestamp_ns, 2000u);
        EXPECT_EQ(d.order_reference, 20u);
        EXPECT_EQ(d.buy_sell, 'S');
        EXPECT_EQ(d.shares, 3u);
        EXPECT_EQ(d.price, 8000u * tick_size_b);

        char stock[9] = {};
        std::memcpy(stock, d.stock, 8);
        EXPECT_STREQ(stock, "GOOG    ");
    }
}

// ---------------------------------------------------------------------------
// Test G: MoldUDP64 sequence number continuity across packets
// ---------------------------------------------------------------------------

TEST(E2EPipeline, SequenceNumberContinuity) {
    const uint32_t tick_size = 100;
    ItchEncoder encoder("SEQ", 1, tick_size);

    // Generate enough messages to force multiple MoldUDP64 packets.
    // Each AddOrderMsg is 36 bytes + 2-byte length prefix = 38 bytes.
    // With kMoldUDP64MaxPayload = 1400 and 20-byte header, that's
    // (1400 - 20) / 38 ≈ 36 messages per packet. Use 100 to get ~3 packets.
    const size_t total_msgs = 100;
    std::vector<std::vector<uint8_t>> encoded;
    for (size_t i = 0; i < total_msgs; ++i) {
        auto rec = makeRecord(EventType::ADD_BID,
                              static_cast<uint64_t>(i * 1000),
                              i + 1, 10000, 1);
        encoded.push_back(encoder.encode(rec));
    }

    std::vector<std::vector<uint8_t>> packets;
    MoldUDP64Framer framer("SEQ_TEST  ");
    framer.setSendCallback([&](const uint8_t* data, size_t len) {
        packets.emplace_back(data, data + len);
    });
    for (auto& msg : encoded) {
        framer.addMessage(msg.data(), static_cast<uint16_t>(msg.size()));
    }
    auto last = framer.flush();
    if (!last.empty())
        packets.push_back(last);

    ASSERT_GT(packets.size(), 1u)
        << "expected multiple packets to test sequence continuity";

    uint64_t expected_seq = 1;
    size_t total_decoded = 0;

    for (size_t p = 0; p < packets.size(); ++p) {
        SCOPED_TRACE(p);
        MoldUDP64Parsed parsed;
        ASSERT_TRUE(parseMoldUDP64(packets[p].data(), packets[p].size(), parsed));

        EXPECT_EQ(parsed.sequence_number, expected_seq)
            << "sequence gap at packet " << p;
        EXPECT_GT(parsed.message_count, 0u);

        expected_seq += parsed.message_count;
        total_decoded += parsed.message_count;
    }

    EXPECT_EQ(total_decoded, total_msgs)
        << "total messages across all packets should match input";
}

}  // namespace test
}  // namespace itch
}  // namespace qrsdp
