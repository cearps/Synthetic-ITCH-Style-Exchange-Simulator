#pragma once

#include "qrsdp/records.h"

namespace qrsdp {

/// Event output. v1: in-memory stub with append only.
class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void append(const EventRecord&) = 0;
};

}  // namespace qrsdp
