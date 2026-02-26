#pragma once

/// ITCH 5.0 message structs (5-message subset).
/// All multi-byte fields are big-endian on the wire.
/// Structs are stored in **host byte order** — the encoder is responsible
/// for converting to big-endian before writing into these structs.

#include <cstdint>
#include <cstring>

namespace qrsdp {
namespace itch {

// --- Message type bytes ---
constexpr char kMsgTypeSystemEvent   = 'S';
constexpr char kMsgTypeStockDirectory = 'R';
constexpr char kMsgTypeAddOrder      = 'A';
constexpr char kMsgTypeOrderDelete   = 'D';
constexpr char kMsgTypeOrderExecuted = 'E';

// --- System Event codes ---
constexpr char kSystemEventStartOfMessages  = 'O';
constexpr char kSystemEventStartOfSystem    = 'S';
constexpr char kSystemEventStartOfMarket    = 'Q';
constexpr char kSystemEventEndOfMarket      = 'M';
constexpr char kSystemEventEndOfMessages    = 'E';
constexpr char kSystemEventHalt             = 'A';  // emergency halt

// -------------------------------------------------------------------------
// Wire-format structs. All fields are big-endian once populated by the
// encoder.  Sizes match the NASDAQ ITCH 5.0 specification.
// -------------------------------------------------------------------------

#pragma pack(push, 1)

/// System Event Message (12 bytes)
struct SystemEventMsg {
    char     message_type;        // 'S'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];        // nanoseconds since midnight, big-endian
    char     event_code;
};
static_assert(sizeof(SystemEventMsg) == 12, "SystemEventMsg must be 12 bytes");

/// Stock Directory Message (39 bytes)
struct StockDirectoryMsg {
    char     message_type;        // 'R'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    char     stock[8];            // right-padded with spaces
    char     market_category;
    char     financial_status;
    uint32_t round_lot_size;
    char     round_lots_only;
    char     issue_classification;
    char     issue_sub_type[2];
    char     authenticity;
    char     short_sale_threshold;
    char     ipo_flag;
    char     luld_ref_price_tier;
    char     etp_flag;
    uint32_t etp_leverage_factor;
    char     inverse_indicator;
};
static_assert(sizeof(StockDirectoryMsg) == 39, "StockDirectoryMsg must be 39 bytes");

/// Add Order Message — No MPID Attribution (36 bytes)
struct AddOrderMsg {
    char     message_type;        // 'A'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference;
    char     buy_sell;            // 'B' or 'S'
    uint32_t shares;
    char     stock[8];
    uint32_t price;               // 4 implied decimal places
};
static_assert(sizeof(AddOrderMsg) == 36, "AddOrderMsg must be 36 bytes");

/// Order Delete Message (19 bytes)
struct OrderDeleteMsg {
    char     message_type;        // 'D'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference;
};
static_assert(sizeof(OrderDeleteMsg) == 19, "OrderDeleteMsg must be 19 bytes");

/// Order Executed Message (31 bytes)
struct OrderExecutedMsg {
    char     message_type;        // 'E'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference;
    uint32_t executed_shares;
    uint64_t match_number;
};
static_assert(sizeof(OrderExecutedMsg) == 31, "OrderExecutedMsg must be 31 bytes");

#pragma pack(pop)

// -------------------------------------------------------------------------
// MoldUDP64 header (20 bytes)
// -------------------------------------------------------------------------

#pragma pack(push, 1)
struct MoldUDP64Header {
    char     session[10];
    uint64_t sequence_number;
    uint16_t message_count;
};
#pragma pack(pop)
static_assert(sizeof(MoldUDP64Header) == 20, "MoldUDP64Header must be 20 bytes");

constexpr size_t kMoldUDP64HeaderSize = sizeof(MoldUDP64Header);
constexpr size_t kMoldUDP64MaxPayload = 1400;  // leave room for IP + UDP headers

}  // namespace itch
}  // namespace qrsdp
