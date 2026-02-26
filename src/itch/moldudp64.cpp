#include "itch/moldudp64.h"
#include "itch/itch_messages.h"
#include "itch/endian.h"

#include <algorithm>
#include <cstring>

namespace qrsdp {
namespace itch {

MoldUDP64Framer::MoldUDP64Framer(const std::string& session_id) {
    std::memset(session_, ' ', sizeof(session_));
    size_t len = std::min(session_id.size(), sizeof(session_));
    std::memcpy(session_, session_id.data(), len);
    buffer_.reserve(kMoldUDP64MaxPayload + kMoldUDP64HeaderSize);
}

void MoldUDP64Framer::addMessage(const uint8_t* data, uint16_t len) {
    size_t block_size = 2 + len;  // 2-byte length prefix + payload
    size_t current_payload = buffer_.size();

    if (message_count_ > 0 &&
        (kMoldUDP64HeaderSize + current_payload + block_size) > kMoldUDP64MaxPayload)
    {
        emitPacket();
    }

    // Append 2-byte big-endian length prefix
    uint16_t be_len = htobe16(len);
    auto* len_bytes = reinterpret_cast<const uint8_t*>(&be_len);
    buffer_.push_back(len_bytes[0]);
    buffer_.push_back(len_bytes[1]);

    // Append message payload
    buffer_.insert(buffer_.end(), data, data + len);
    ++message_count_;
}

std::vector<uint8_t> MoldUDP64Framer::flush() {
    if (message_count_ == 0)
        return {};

    // Build the complete packet: header + buffered message blocks
    std::vector<uint8_t> packet(kMoldUDP64HeaderSize + buffer_.size());

    MoldUDP64Header hdr{};
    std::memcpy(hdr.session, session_, 10);
    hdr.sequence_number = htobe64(sequence_number_);
    hdr.message_count   = htobe16(message_count_);
    std::memcpy(packet.data(), &hdr, kMoldUDP64HeaderSize);
    std::memcpy(packet.data() + kMoldUDP64HeaderSize, buffer_.data(), buffer_.size());

    sequence_number_ += message_count_;
    message_count_ = 0;
    buffer_.clear();

    return packet;
}

void MoldUDP64Framer::emitPacket() {
    auto packet = flush();
    if (!packet.empty() && send_cb_) {
        send_cb_(packet.data(), packet.size());
    }
}

}  // namespace itch
}  // namespace qrsdp
