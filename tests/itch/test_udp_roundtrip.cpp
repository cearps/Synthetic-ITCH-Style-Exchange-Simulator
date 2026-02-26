#include <gtest/gtest.h>
#include "itch/itch_encoder.h"
#include "itch/itch_messages.h"
#include "itch/moldudp64.h"
#include "itch/endian.h"
#include "core/event_types.h"
#include "core/records.h"

#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    constexpr socket_t kBadSocket = INVALID_SOCKET;
    inline int close_sock(socket_t s) { return closesocket(s); }

    struct WsaGuard {
        WsaGuard() { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
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

/// Localhost (unicast) UDP roundtrip test.
/// Sends a MoldUDP64-framed ITCH packet on 127.0.0.1 and verifies we can
/// receive and decode it.  No multicast group needed.
TEST(UdpRoundtrip, LoopbackSendReceive) {
    WsaGuard wsa; (void)wsa;

    // -- Bind a receiver socket to an ephemeral port on localhost --
    socket_t rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_NE(rx, kBadSocket);

    struct sockaddr_in rx_addr{};
    rx_addr.sin_family = AF_INET;
    rx_addr.sin_port = 0;  // OS picks a free port
    rx_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(rx, reinterpret_cast<struct sockaddr*>(&rx_addr), sizeof(rx_addr)), 0);

    // Read back the assigned port
    socklen_t addr_len = sizeof(rx_addr);
    getsockname(rx, reinterpret_cast<struct sockaddr*>(&rx_addr), &addr_len);
    uint16_t port = ntohs(rx_addr.sin_port);

    // Set a receive timeout so the test doesn't hang
#ifdef _WIN32
    DWORD timeout_ms = 2000;
    setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    struct timeval tv { 2, 0 };
    setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    // -- Build an ITCH message and frame it in MoldUDP64 --
    ItchEncoder encoder("TEST", 1, 100);
    EventRecord rec{};
    rec.ts_ns = 999999;
    rec.type = static_cast<uint8_t>(EventType::ADD_BID);
    rec.side = static_cast<uint8_t>(Side::BID);
    rec.price_ticks = 5000;
    rec.qty = 7;
    rec.order_id = 123;
    rec.flags = 0;

    auto itch_msg = encoder.encode(rec);

    MoldUDP64Framer framer("ROUNDTRIP ");
    framer.addMessage(itch_msg.data(), static_cast<uint16_t>(itch_msg.size()));
    auto packet = framer.flush();
    ASSERT_FALSE(packet.empty());

    // -- Send via a sender socket --
    socket_t tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_NE(tx, kBadSocket);

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    auto sent = sendto(tx, reinterpret_cast<const char*>(packet.data()),
                       static_cast<int>(packet.size()), 0,
                       reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    EXPECT_EQ(sent, static_cast<decltype(sent)>(packet.size()));

    // -- Receive and decode --
    uint8_t buf[2048];
    auto n = recv(rx, reinterpret_cast<char*>(buf), sizeof(buf), 0);
    ASSERT_GT(n, static_cast<decltype(n)>(kMoldUDP64HeaderSize));

    // Verify MoldUDP64 header
    MoldUDP64Header hdr;
    std::memcpy(&hdr, buf, kMoldUDP64HeaderSize);
    EXPECT_EQ(betoh64(hdr.sequence_number), 1u);
    EXPECT_EQ(betoh16(hdr.message_count), 1u);

    // Extract ITCH message
    size_t offset = kMoldUDP64HeaderSize;
    uint16_t msg_len_be;
    std::memcpy(&msg_len_be, buf + offset, 2);
    uint16_t msg_len = betoh16(msg_len_be);
    offset += 2;

    ASSERT_EQ(msg_len, sizeof(AddOrderMsg));
    ASSERT_LE(offset + msg_len, static_cast<size_t>(n));

    AddOrderMsg decoded;
    std::memcpy(&decoded, buf + offset, sizeof(decoded));

    EXPECT_EQ(decoded.message_type, kMsgTypeAddOrder);
    EXPECT_EQ(betoh64(decoded.order_reference), 123u);
    EXPECT_EQ(decoded.buy_sell, 'B');
    EXPECT_EQ(betoh32(decoded.shares), 7u);
    EXPECT_EQ(betoh32(decoded.price), 5000u * 100);
    EXPECT_EQ(load48be(decoded.timestamp), 999999u);

    close_sock(tx);
    close_sock(rx);
}

/// Test that multiple ITCH messages in a single MoldUDP64 packet
/// survive a UDP roundtrip intact.
TEST(UdpRoundtrip, MultipleMessagesInPacket) {
    WsaGuard wsa; (void)wsa;

    socket_t rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_NE(rx, kBadSocket);

    struct sockaddr_in rx_addr{};
    rx_addr.sin_family = AF_INET;
    rx_addr.sin_port = 0;
    rx_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(rx, reinterpret_cast<struct sockaddr*>(&rx_addr), sizeof(rx_addr)), 0);

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

    ItchEncoder encoder("MULTI", 1, 100);

    EventRecord add_rec{};
    add_rec.ts_ns = 1000;
    add_rec.type = static_cast<uint8_t>(EventType::ADD_BID);
    add_rec.side = static_cast<uint8_t>(Side::BID);
    add_rec.price_ticks = 100;
    add_rec.qty = 1;
    add_rec.order_id = 1;

    EventRecord exec_rec{};
    exec_rec.ts_ns = 2000;
    exec_rec.type = static_cast<uint8_t>(EventType::EXECUTE_BUY);
    exec_rec.side = static_cast<uint8_t>(Side::BID);
    exec_rec.price_ticks = 100;
    exec_rec.qty = 1;
    exec_rec.order_id = 1;

    auto add_msg = encoder.encode(add_rec);
    auto exec_msg = encoder.encode(exec_rec);

    MoldUDP64Framer framer("MULTI_TEST");
    framer.addMessage(add_msg.data(), static_cast<uint16_t>(add_msg.size()));
    framer.addMessage(exec_msg.data(), static_cast<uint16_t>(exec_msg.size()));
    auto packet = framer.flush();

    socket_t tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    sendto(tx, reinterpret_cast<const char*>(packet.data()),
           static_cast<int>(packet.size()), 0,
           reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));

    uint8_t buf[2048];
    auto n = recv(rx, reinterpret_cast<char*>(buf), sizeof(buf), 0);
    ASSERT_GT(n, static_cast<decltype(n)>(kMoldUDP64HeaderSize));

    MoldUDP64Header hdr;
    std::memcpy(&hdr, buf, kMoldUDP64HeaderSize);
    EXPECT_EQ(betoh16(hdr.message_count), 2u);

    // Decode first message (Add Order)
    size_t offset = kMoldUDP64HeaderSize;
    uint16_t len1_be;
    std::memcpy(&len1_be, buf + offset, 2);
    EXPECT_EQ(betoh16(len1_be), sizeof(AddOrderMsg));
    offset += 2;
    EXPECT_EQ(buf[offset], static_cast<uint8_t>(kMsgTypeAddOrder));
    offset += betoh16(len1_be);

    // Decode second message (Order Executed)
    uint16_t len2_be;
    std::memcpy(&len2_be, buf + offset, 2);
    EXPECT_EQ(betoh16(len2_be), sizeof(OrderExecutedMsg));
    offset += 2;
    EXPECT_EQ(buf[offset], static_cast<uint8_t>(kMsgTypeOrderExecuted));

    close_sock(tx);
    close_sock(rx);
}

}  // namespace test
}  // namespace itch
}  // namespace qrsdp
