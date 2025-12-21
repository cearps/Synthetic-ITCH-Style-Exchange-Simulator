#include "logging/event_log.h"

namespace exchange {

DeterministicEventLog::DeterministicEventLog() 
    : seed_(0), sequence_counter_(0), replay_mode_(false) {
}

void DeterministicEventLog::initialize(uint64_t seed) {
    seed_ = seed;
    sequence_counter_ = 0;
}

void DeterministicEventLog::append_event(const OrderEvent& event) {
    (void)event;  // Unused in stub
    // TODO: Implement event logging
    sequence_counter_++;
}

void DeterministicEventLog::append_trade(const TradeEvent& trade) {
    (void)trade;  // Unused in stub
    // TODO: Implement trade logging
}

void DeterministicEventLog::append_book_update(const BookUpdateEvent& update) {
    (void)update;  // Unused in stub
    // TODO: Implement book update logging
}

uint64_t DeterministicEventLog::get_sequence_number() const {
    return sequence_counter_;
}

uint64_t DeterministicEventLog::get_seed() const {
    return seed_;
}

void DeterministicEventLog::reset() {
    sequence_counter_ = 0;
}

void DeterministicEventLog::clear() {
    order_events_.clear();
    trade_events_.clear();
    book_update_events_.clear();
    sequence_counter_ = 0;
}

bool DeterministicEventLog::is_replay_mode() const {
    return replay_mode_;
}

void DeterministicEventLog::enable_replay_mode(bool enabled) {
    replay_mode_ = enabled;
}

std::vector<OrderEvent> DeterministicEventLog::replay_events() const {
    // TODO: Implement event replay
    return order_events_;
}

} // namespace exchange

