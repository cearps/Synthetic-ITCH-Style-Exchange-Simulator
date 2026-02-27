#pragma once

#include "io/i_event_sink.h"

#include <cstdio>
#include <exception>
#include <vector>

namespace qrsdp {

/// Fan-out sink: forwards every append to multiple downstream sinks.
/// Best-effort: if one sink throws, the error is logged and remaining
/// sinks still receive the event. Non-owning pointers — caller manages
/// the lifetime of downstream sinks.
class MultiplexSink : public IEventSink {
public:
    void addSink(IEventSink* sink) { sinks_.push_back(sink); }

    void append(const EventRecord& rec) override {
        for (auto* s : sinks_) {
            try {
                s->append(rec);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "MultiplexSink: sink error: %s\n", e.what());
            }
        }
    }

    void flush() override {
        for (auto* s : sinks_) {
            try {
                s->flush();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "MultiplexSink: flush error: %s\n", e.what());
            }
        }
    }

    void close() override {
        for (auto* s : sinks_) {
            try {
                s->close();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "MultiplexSink: close error: %s\n", e.what());
            }
        }
    }

    size_t sinkCount() const { return sinks_.size(); }

private:
    std::vector<IEventSink*> sinks_;
};

}  // namespace qrsdp
