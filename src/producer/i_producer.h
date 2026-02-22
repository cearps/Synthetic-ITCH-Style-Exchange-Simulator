#pragma once

#include "core/records.h"
#include "io/i_event_sink.h"

namespace qrsdp {

/// Runs one intraday session: generates events and appends to sink.
class IProducer {
public:
    virtual ~IProducer() = default;
    virtual SessionResult runSession(const TradingSession&, IEventSink&) = 0;
};

}  // namespace qrsdp
