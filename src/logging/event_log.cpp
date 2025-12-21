#include "logging/event_log.h"

namespace exchange {

DeterministicEventLog::DeterministicEventLog() 
    : seed_(0), sequence_counter_(0), replay_mode_(false) {
}

void DeterministicEventLog::initialize(uint64_t seed) {
    seed_ = seed;
    sequence_counter_ = 0;
    // Don't clear events here - use reset() or clear() explicitly
    // This allows initialize() to be called without losing events
}

void DeterministicEventLog::append_event(const OrderEvent& event) {
    OrderEvent logged_event = event;
    logged_event.sequence_number = sequence_counter_++;
    order_events_.push_back(logged_event);
}

void DeterministicEventLog::append_trade(const TradeEvent& trade) {
    TradeEvent logged_trade = trade;
    logged_trade.sequence_number = sequence_counter_++;
    trade_events_.push_back(logged_trade);
}

void DeterministicEventLog::append_book_update(const BookUpdateEvent& update) {
    BookUpdateEvent logged_update = update;
    logged_update.sequence_number = sequence_counter_++;
    book_update_events_.push_back(logged_update);
}

uint64_t DeterministicEventLog::get_sequence_number() const {
    return sequence_counter_;
}

uint64_t DeterministicEventLog::get_seed() const {
    return seed_;
}

void DeterministicEventLog::reset() {
    sequence_counter_ = 0;
    order_events_.clear();
    trade_events_.clear();
    book_update_events_.clear();
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

