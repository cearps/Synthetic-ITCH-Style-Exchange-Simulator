#pragma once

#include "../core/events.h"
#include <memory>
#include <vector>
#include <cstdint>

namespace exchange {

class IEventLog {
public:
    virtual ~IEventLog() = default;
    
    virtual void initialize(uint64_t seed) = 0;
    virtual void append_event(const OrderEvent& event) = 0;
    virtual void append_trade(const TradeEvent& trade) = 0;
    virtual void append_book_update(const BookUpdateEvent& update) = 0;
    
    virtual uint64_t get_sequence_number() const = 0;
    virtual uint64_t get_seed() const = 0;
    
    virtual void reset() = 0;
    virtual void clear() = 0;
    
    virtual bool is_replay_mode() const = 0;
    virtual void enable_replay_mode(bool enabled) = 0;
    
    virtual std::vector<OrderEvent> replay_events() const = 0;
};

class DeterministicEventLog : public IEventLog {
public:
    DeterministicEventLog();
    virtual ~DeterministicEventLog() = default;
    
    void initialize(uint64_t seed) override;
    void append_event(const OrderEvent& event) override;
    void append_trade(const TradeEvent& trade) override;
    void append_book_update(const BookUpdateEvent& update) override;
    
    uint64_t get_sequence_number() const override;
    uint64_t get_seed() const override;
    
    void reset() override;
    void clear() override;
    
    bool is_replay_mode() const override;
    void enable_replay_mode(bool enabled) override;
    
    std::vector<OrderEvent> replay_events() const override;
    
    // Accessors for visualization
    const std::vector<OrderEvent>& get_order_events() const { return order_events_; }
    const std::vector<TradeEvent>& get_trade_events() const { return trade_events_; }
    const std::vector<BookUpdateEvent>& get_book_update_events() const { return book_update_events_; }
    
private:
    uint64_t seed_;
    uint64_t sequence_counter_;
    bool replay_mode_;
    
    std::vector<OrderEvent> order_events_;
    std::vector<TradeEvent> trade_events_;
    std::vector<BookUpdateEvent> book_update_events_;
};

} // namespace exchange

