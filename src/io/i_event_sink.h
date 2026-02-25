#pragma once

#include "core/records.h"

namespace qrsdp {

/// Abstract event output interface.
/// Implementations: BinaryFileSink, InMemorySink, KafkaSink, MultiplexSink.
class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void append(const EventRecord&) = 0;
    virtual void flush() {}
    virtual void close() {}
};

}  // namespace qrsdp
