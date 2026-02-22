#include "qrsdp/event_log_parser.h"
#include <algorithm>
#include <cstdint>

namespace qrsdp {

void EventLogParser::reset() {
    bid_depths.clear();
    ask_depths.clear();
    best_bid_ticks = 0;
    best_ask_ticks = 0;
    event_count = 0;
}

bool EventLogParser::push(const EventRecord& rec) {
    ++event_count;
    const EventType type = static_cast<EventType>(rec.type);
    const int32_t price = rec.price_ticks;
    const uint32_t qty = rec.qty;

    if (bid_depths.empty()) {
        bid_depths.resize(1, 0);
        ask_depths.resize(1, 0);
    }

    switch (type) {
        case EventType::ADD_BID: {
            if (best_bid_ticks == 0) best_bid_ticks = price;
            const int32_t best = best_bid_ticks;
            const int idx = best - price;
            if (idx >= 0) {
                const size_t i = static_cast<size_t>(idx);
                if (i >= bid_depths.size()) bid_depths.resize(i + 1, 0);
                bid_depths[i] += qty;
            }
            break;
        }
        case EventType::ADD_ASK: {
            if (best_ask_ticks == 0) best_ask_ticks = price;
            const int idx = price - best_ask_ticks;
            if (idx >= 0) {
                const size_t i = static_cast<size_t>(idx);
                if (i >= ask_depths.size()) ask_depths.resize(i + 1, 0);
                ask_depths[i] += qty;
            }
            break;
        }
        case EventType::CANCEL_BID: {
            const int idx = best_bid_ticks - price;
            if (idx >= 0 && static_cast<size_t>(idx) < bid_depths.size()) {
                auto& d = bid_depths[static_cast<size_t>(idx)];
                if (d >= qty) d -= qty;
                else d = 0;
            }
            break;
        }
        case EventType::CANCEL_ASK: {
            const int idx = price - best_ask_ticks;
            if (idx >= 0 && static_cast<size_t>(idx) < ask_depths.size()) {
                auto& d = ask_depths[static_cast<size_t>(idx)];
                if (d >= qty) d -= qty;
                else d = 0;
            }
            break;
        }
        case EventType::EXECUTE_BUY: {
            if (best_ask_ticks != 0 && price == best_ask_ticks && !ask_depths.empty()) {
                if (ask_depths[0] >= qty) ask_depths[0] -= qty;
                else ask_depths[0] = 0;
            }
            break;
        }
        case EventType::EXECUTE_SELL: {
            if (best_bid_ticks != 0 && price == best_bid_ticks && !bid_depths.empty()) {
                if (bid_depths[0] >= qty) bid_depths[0] -= qty;
                else bid_depths[0] = 0;
            }
            break;
        }
        default:
            break;
    }
    return true;
}

}  // namespace qrsdp
