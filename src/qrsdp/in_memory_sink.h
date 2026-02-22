#pragma once

#include "qrsdp/i_event_sink.h"
#include "qrsdp/records.h"
#include <cstddef>
#include <vector>

namespace qrsdp {

/// In-memory event sink: append stores into a vector (v1; no file I/O).
class InMemorySink : public IEventSink {
public:
    void append(const EventRecord&) override;
    const std::vector<EventRecord>& events() const { return events_; }
    size_t size() const { return events_.size(); }
    void clear() { events_.clear(); }

private:
    std::vector<EventRecord> events_;
};

}  // namespace qrsdp
