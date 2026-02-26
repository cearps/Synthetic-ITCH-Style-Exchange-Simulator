#pragma once

/// Reusable ITCH 5.0 message decoder and MoldUDP64 packet parser.
/// Converts big-endian wire-format ITCH messages back into host-order
/// structs suitable for programmatic comparison in tests and tooling.

#include "itch/itch_messages.h"
#include "itch/endian.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace qrsdp {
namespace itch {

/// Decoded representation of any supported ITCH message, with all
/// multi-byte fields converted to host byte order.
struct DecodedItchMsg {
    char     msg_type       = 0;
    uint16_t stock_locate   = 0;
    uint64_t timestamp_ns   = 0;
    uint64_t order_reference = 0;
    char     buy_sell       = 0;      // 'B' or 'S' (AddOrder only)
    uint32_t shares         = 0;
    uint32_t price          = 0;      // raw price-4 units (AddOrder only)
    char     stock[8]       = {};
    uint64_t match_number   = 0;      // OrderExecuted only
    char     event_code     = 0;      // SystemEvent only
};

/// Decode a single ITCH message from raw bytes.
/// Returns true on success, false if the message type is unknown or
/// the buffer is too short for the declared type.
inline bool decodeItchMessage(const uint8_t* data, size_t len, DecodedItchMsg& out) {
    out = DecodedItchMsg{};
    if (len < 1) return false;
    out.msg_type = static_cast<char>(data[0]);

    switch (out.msg_type) {
    case kMsgTypeSystemEvent: {
        if (len < sizeof(SystemEventMsg)) return false;
        const auto* m = reinterpret_cast<const SystemEventMsg*>(data);
        out.stock_locate = betoh16(m->stock_locate);
        out.timestamp_ns = load48be(m->timestamp);
        out.event_code   = m->event_code;
        return true;
    }
    case kMsgTypeStockDirectory: {
        if (len < sizeof(StockDirectoryMsg)) return false;
        const auto* m = reinterpret_cast<const StockDirectoryMsg*>(data);
        out.stock_locate = betoh16(m->stock_locate);
        out.timestamp_ns = load48be(m->timestamp);
        std::memcpy(out.stock, m->stock, 8);
        return true;
    }
    case kMsgTypeAddOrder: {
        if (len < sizeof(AddOrderMsg)) return false;
        const auto* m = reinterpret_cast<const AddOrderMsg*>(data);
        out.stock_locate    = betoh16(m->stock_locate);
        out.timestamp_ns    = load48be(m->timestamp);
        out.order_reference = betoh64(m->order_reference);
        out.buy_sell        = m->buy_sell;
        out.shares          = betoh32(m->shares);
        out.price           = betoh32(m->price);
        std::memcpy(out.stock, m->stock, 8);
        return true;
    }
    case kMsgTypeOrderDelete: {
        if (len < sizeof(OrderDeleteMsg)) return false;
        const auto* m = reinterpret_cast<const OrderDeleteMsg*>(data);
        out.stock_locate    = betoh16(m->stock_locate);
        out.timestamp_ns    = load48be(m->timestamp);
        out.order_reference = betoh64(m->order_reference);
        return true;
    }
    case kMsgTypeOrderExecuted: {
        if (len < sizeof(OrderExecutedMsg)) return false;
        const auto* m = reinterpret_cast<const OrderExecutedMsg*>(data);
        out.stock_locate    = betoh16(m->stock_locate);
        out.timestamp_ns    = load48be(m->timestamp);
        out.order_reference = betoh64(m->order_reference);
        out.shares          = betoh32(m->executed_shares);
        out.match_number    = betoh64(m->match_number);
        return true;
    }
    default:
        return false;
    }
}

/// A byte-range view into a buffer (non-owning).
struct ByteSpan {
    const uint8_t* data;
    size_t         size;
};

/// Parsed MoldUDP64 packet header fields (host byte order).
struct MoldUDP64Parsed {
    char     session[10];
    uint64_t sequence_number;
    uint16_t message_count;
    std::vector<ByteSpan> messages;
};

/// Parse a MoldUDP64 packet, extracting the header and individual
/// ITCH message byte spans.  Returns true if the header was valid
/// and all declared messages were extractable.
inline bool parseMoldUDP64(const uint8_t* data, size_t len, MoldUDP64Parsed& out) {
    out = MoldUDP64Parsed{};
    if (len < kMoldUDP64HeaderSize) return false;

    MoldUDP64Header hdr;
    std::memcpy(&hdr, data, kMoldUDP64HeaderSize);
    std::memcpy(out.session, hdr.session, 10);
    out.sequence_number = betoh64(hdr.sequence_number);
    out.message_count   = betoh16(hdr.message_count);

    size_t offset = kMoldUDP64HeaderSize;
    for (uint16_t i = 0; i < out.message_count; ++i) {
        if (offset + 2 > len) return false;
        uint16_t msg_len_be;
        std::memcpy(&msg_len_be, data + offset, 2);
        uint16_t msg_len = betoh16(msg_len_be);
        offset += 2;
        if (offset + msg_len > len) return false;
        out.messages.push_back({data + offset, msg_len});
        offset += msg_len;
    }

    return out.messages.size() == out.message_count;
}

}  // namespace itch
}  // namespace qrsdp
