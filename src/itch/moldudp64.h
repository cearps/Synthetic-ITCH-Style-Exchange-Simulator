#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace qrsdp {
namespace itch {

/// Frames ITCH messages into MoldUDP64 packets.
///
/// Each packet has a 20-byte header (10-char session, 8-byte sequence number,
/// 2-byte message count) followed by length-prefixed message blocks
/// (2-byte big-endian length + payload).
///
/// Auto-flushes when the accumulated payload approaches the MTU limit.
class MoldUDP64Framer {
public:
    /// Callback invoked when a complete packet is ready to send.
    using SendCallback = std::function<void(const uint8_t* data, size_t len)>;

    /// @param session_id  10-character session identifier (truncated/padded).
    explicit MoldUDP64Framer(const std::string& session_id);

    /// Add a single ITCH message to the current packet.
    /// If the packet would exceed the MTU limit, it is flushed first via
    /// the send callback, then the message is placed in a new packet.
    void addMessage(const uint8_t* data, uint16_t len);

    /// Flush the current packet (if non-empty) via the send callback.
    /// Returns the flushed packet bytes, or empty if nothing to flush.
    std::vector<uint8_t> flush();

    /// Set the callback that receives complete packets.
    void setSendCallback(SendCallback cb) { send_cb_ = std::move(cb); }

    uint64_t nextSequenceNumber() const { return sequence_number_; }
    uint16_t pendingMessageCount() const { return message_count_; }

private:
    void emitPacket();

    char     session_[10];
    uint64_t sequence_number_ = 1;
    uint16_t message_count_   = 0;
    std::vector<uint8_t> buffer_;
    SendCallback send_cb_;
};

}  // namespace itch
}  // namespace qrsdp
