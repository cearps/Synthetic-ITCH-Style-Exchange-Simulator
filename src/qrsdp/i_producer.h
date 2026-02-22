#pragma once

#include "qrsdp/records.h"
#include "qrsdp/i_event_sink.h"

namespace qrsdp {

/// Runs one intraday session: generates events and appends to sink.
class IProducer {
public:
    virtual ~IProducer() = default;
    virtual SessionResult runSession(const TradingSession&, IEventSink&) = 0;
};

}  // namespace qrsdp
