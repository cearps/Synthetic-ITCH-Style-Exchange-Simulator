#include <gtest/gtest.h>
#include "itch/moldudp64.h"
#include "itch/itch_messages.h"
#include "itch/endian.h"

#include <cstring>

namespace qrsdp {
namespace itch {
namespace test {

TEST(MoldUDP64, EmptyFlushReturnsEmpty) {
    MoldUDP64Framer framer("SESS000001");
    auto pkt = framer.flush();
    EXPECT_TRUE(pkt.empty());
}

TEST(MoldUDP64, SingleMessagePacket) {
    MoldUDP64Framer framer("SESS000001");

    uint8_t msg[] = {0xAA, 0xBB, 0xCC};
    framer.addMessage(msg, 3);
    auto pkt = framer.flush();

    ASSERT_GE(pkt.size(), kMoldUDP64HeaderSize + 2 + 3);

    MoldUDP64Header hdr;
    std::memcpy(&hdr, pkt.data(), kMoldUDP64HeaderSize);

    EXPECT_EQ(std::string(hdr.session, 10), "SESS000001");
    EXPECT_EQ(betoh64(hdr.sequence_number), 1u);
    EXPECT_EQ(betoh16(hdr.message_count), 1u);

    // Length-prefixed message block
    uint16_t msg_len_be;
    std::memcpy(&msg_len_be, pkt.data() + kMoldUDP64HeaderSize, 2);
    EXPECT_EQ(betoh16(msg_len_be), 3u);

    EXPECT_EQ(pkt[kMoldUDP64HeaderSize + 2], 0xAA);
    EXPECT_EQ(pkt[kMoldUDP64HeaderSize + 3], 0xBB);
    EXPECT_EQ(pkt[kMoldUDP64HeaderSize + 4], 0xCC);
}

TEST(MoldUDP64, MultipleMessagesInOnePacket) {
    MoldUDP64Framer framer("TEST123456");

    uint8_t msg1[] = {0x01, 0x02};
    uint8_t msg2[] = {0x03, 0x04, 0x05};
    framer.addMessage(msg1, 2);
    framer.addMessage(msg2, 3);
    auto pkt = framer.flush();

    MoldUDP64Header hdr;
    std::memcpy(&hdr, pkt.data(), kMoldUDP64HeaderSize);
    EXPECT_EQ(betoh16(hdr.message_count), 2u);
    EXPECT_EQ(betoh64(hdr.sequence_number), 1u);

    size_t expected_size = kMoldUDP64HeaderSize + (2 + 2) + (2 + 3);
    EXPECT_EQ(pkt.size(), expected_size);
}

TEST(MoldUDP64, SequenceNumberProgresses) {
    MoldUDP64Framer framer("SEQ_TEST  ");

    uint8_t msg[] = {0x00};

    framer.addMessage(msg, 1);
    framer.addMessage(msg, 1);
    auto pkt1 = framer.flush();

    MoldUDP64Header hdr1;
    std::memcpy(&hdr1, pkt1.data(), kMoldUDP64HeaderSize);
    EXPECT_EQ(betoh64(hdr1.sequence_number), 1u);
    EXPECT_EQ(betoh16(hdr1.message_count), 2u);

    framer.addMessage(msg, 1);
    auto pkt2 = framer.flush();

    MoldUDP64Header hdr2;
    std::memcpy(&hdr2, pkt2.data(), kMoldUDP64HeaderSize);
    EXPECT_EQ(betoh64(hdr2.sequence_number), 3u);
    EXPECT_EQ(betoh16(hdr2.message_count), 1u);

    EXPECT_EQ(framer.nextSequenceNumber(), 4u);
}

TEST(MoldUDP64, SessionIdPaddedOrTruncated) {
    // Short session ID gets padded with spaces
    MoldUDP64Framer framer_short("ABC");
    uint8_t msg[] = {0x01};
    framer_short.addMessage(msg, 1);
    auto pkt = framer_short.flush();

    MoldUDP64Header hdr;
    std::memcpy(&hdr, pkt.data(), kMoldUDP64HeaderSize);
    EXPECT_EQ(std::string(hdr.session, 10), "ABC       ");

    // Long session ID gets truncated to 10 chars
    MoldUDP64Framer framer_long("ABCDEFGHIJKLMNOP");
    framer_long.addMessage(msg, 1);
    pkt = framer_long.flush();
    std::memcpy(&hdr, pkt.data(), kMoldUDP64HeaderSize);
    EXPECT_EQ(std::string(hdr.session, 10), "ABCDEFGHIJ");
}

TEST(MoldUDP64, MTUAutoFlush) {
    std::vector<std::vector<uint8_t>> sent_packets;

    MoldUDP64Framer framer("MTU_TEST  ");
    framer.setSendCallback([&](const uint8_t* data, size_t len) {
        sent_packets.emplace_back(data, data + len);
    });

    // Fill with large messages that will exceed the MTU limit.
    // kMoldUDP64MaxPayload is 1400. Each message block: 2-byte prefix + payload.
    // A message of 500 bytes = 502 bytes per block.
    // 3 such messages = 1506 bytes of blocks > 1400 - 20 = 1380 usable payload.
    // So the 3rd message should trigger an auto-flush of the first two.
    std::vector<uint8_t> big_msg(500, 0xFF);

    framer.addMessage(big_msg.data(), static_cast<uint16_t>(big_msg.size()));
    EXPECT_EQ(sent_packets.size(), 0u);

    framer.addMessage(big_msg.data(), static_cast<uint16_t>(big_msg.size()));
    EXPECT_EQ(sent_packets.size(), 0u);

    framer.addMessage(big_msg.data(), static_cast<uint16_t>(big_msg.size()));
    // The third add should have triggered a flush of the first two
    EXPECT_EQ(sent_packets.size(), 1u);

    // The auto-flushed packet should contain 2 messages
    if (!sent_packets.empty()) {
        MoldUDP64Header hdr;
        std::memcpy(&hdr, sent_packets[0].data(), kMoldUDP64HeaderSize);
        EXPECT_EQ(betoh16(hdr.message_count), 2u);
    }

    // Manually flush the remaining message
    auto remaining = framer.flush();
    ASSERT_FALSE(remaining.empty());
    MoldUDP64Header hdr_rem;
    std::memcpy(&hdr_rem, remaining.data(), kMoldUDP64HeaderSize);
    EXPECT_EQ(betoh16(hdr_rem.message_count), 1u);
}

TEST(MoldUDP64, PendingMessageCount) {
    MoldUDP64Framer framer("PENDING   ");
    EXPECT_EQ(framer.pendingMessageCount(), 0u);

    uint8_t msg[] = {0x01};
    framer.addMessage(msg, 1);
    EXPECT_EQ(framer.pendingMessageCount(), 1u);

    framer.addMessage(msg, 1);
    EXPECT_EQ(framer.pendingMessageCount(), 2u);

    framer.flush();
    EXPECT_EQ(framer.pendingMessageCount(), 0u);
}

}  // namespace test
}  // namespace itch
}  // namespace qrsdp
