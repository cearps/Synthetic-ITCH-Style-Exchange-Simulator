#pragma once

#include "../core/events.h"
#include <memory>
#include <functional>

namespace exchange {

class IEventProducer {
public:
    virtual ~IEventProducer() = default;
    
    virtual void initialize(uint64_t seed) = 0;
    virtual bool has_next_event() const = 0;
    virtual OrderEvent next_event() = 0;
    virtual void reset() = 0;
};

class QRSDPEventProducer : public IEventProducer {
public:
    QRSDPEventProducer();
    virtual ~QRSDPEventProducer() = default;
    
    void initialize(uint64_t seed) override;
    bool has_next_event() const override;
    OrderEvent next_event() override;
    void reset() override;
    
private:
    uint64_t seed_;
    uint64_t sequence_counter_;
    uint64_t current_state_;
};

} // namespace exchange

