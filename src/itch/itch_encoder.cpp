#include "itch/itch_encoder.h"
#include "itch/itch_messages.h"
#include "itch/endian.h"
#include "core/event_types.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace qrsdp {
namespace itch {

ItchEncoder::ItchEncoder(const std::string& symbol, uint16_t locate, uint32_t tick_size)
    : locate_(locate)
    , tick_size_(tick_size)
{
    std::memset(symbol_, ' ', sizeof(symbol_));
    size_t len = std::min(symbol.size(), sizeof(symbol_));
    std::memcpy(symbol_, symbol.data(), len);
}

std::vector<uint8_t> ItchEncoder::encode(const EventRecord& rec) const {
    auto type = static_cast<EventType>(rec.type);

    switch (type) {
    case EventType::ADD_BID:
    case EventType::ADD_ASK: {
        AddOrderMsg msg{};
        msg.message_type   = kMsgTypeAddOrder;
        msg.stock_locate   = htobe16(locate_);
        msg.tracking_number = 0;
        store48be(msg.timestamp, rec.ts_ns);
        msg.order_reference = htobe64(rec.order_id);
        msg.buy_sell       = (type == EventType::ADD_BID) ? 'B' : 'S';
        msg.shares         = htobe32(rec.qty);
        std::memcpy(msg.stock, symbol_, 8);
        msg.price          = htobe32(static_cast<uint32_t>(rec.price_ticks) * tick_size_);

        std::vector<uint8_t> out(sizeof(msg));
        std::memcpy(out.data(), &msg, sizeof(msg));
        return out;
    }

    case EventType::CANCEL_BID:
    case EventType::CANCEL_ASK: {
        OrderDeleteMsg msg{};
        msg.message_type   = kMsgTypeOrderDelete;
        msg.stock_locate   = htobe16(locate_);
        msg.tracking_number = 0;
        store48be(msg.timestamp, rec.ts_ns);
        msg.order_reference = htobe64(rec.order_id);

        std::vector<uint8_t> out(sizeof(msg));
        std::memcpy(out.data(), &msg, sizeof(msg));
        return out;
    }

    case EventType::EXECUTE_BUY:
    case EventType::EXECUTE_SELL: {
        OrderExecutedMsg msg{};
        msg.message_type   = kMsgTypeOrderExecuted;
        msg.stock_locate   = htobe16(locate_);
        msg.tracking_number = 0;
        store48be(msg.timestamp, rec.ts_ns);
        msg.order_reference = htobe64(rec.order_id);
        msg.executed_shares = htobe32(rec.qty);
        msg.match_number   = htobe64(match_number_++);

        std::vector<uint8_t> out(sizeof(msg));
        std::memcpy(out.data(), &msg, sizeof(msg));
        return out;
    }

    default:
        throw std::runtime_error("ItchEncoder: unknown event type");
    }
}

std::vector<uint8_t> ItchEncoder::encodeSystemEvent(char event_code, uint64_t ts_ns) const {
    SystemEventMsg msg{};
    msg.message_type    = kMsgTypeSystemEvent;
    msg.stock_locate    = htobe16(locate_);
    msg.tracking_number = 0;
    store48be(msg.timestamp, ts_ns);
    msg.event_code      = event_code;

    std::vector<uint8_t> out(sizeof(msg));
    std::memcpy(out.data(), &msg, sizeof(msg));
    return out;
}

std::vector<uint8_t> ItchEncoder::encodeStockDirectory(uint64_t ts_ns) const {
    StockDirectoryMsg msg{};
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type    = kMsgTypeStockDirectory;
    msg.stock_locate    = htobe16(locate_);
    msg.tracking_number = 0;
    store48be(msg.timestamp, ts_ns);
    std::memcpy(msg.stock, symbol_, 8);
    msg.market_category    = 'Q';  // NASDAQ Global Select
    msg.financial_status   = 'N';  // Normal
    msg.round_lot_size     = htobe32(100);
    msg.round_lots_only    = 'N';
    msg.issue_classification = 'A';  // American Depositary Share (placeholder)
    msg.issue_sub_type[0]  = 'Z';
    msg.issue_sub_type[1]  = ' ';
    msg.authenticity        = 'P';  // Production
    msg.short_sale_threshold = 'N';
    msg.ipo_flag            = ' ';
    msg.luld_ref_price_tier = ' ';
    msg.etp_flag            = 'N';
    msg.etp_leverage_factor = 0;
    msg.inverse_indicator   = 'N';

    std::vector<uint8_t> out(sizeof(msg));
    std::memcpy(out.data(), &msg, sizeof(msg));
    return out;
}

}  // namespace itch
}  // namespace qrsdp
