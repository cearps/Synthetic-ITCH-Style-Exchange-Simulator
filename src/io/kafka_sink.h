#pragma once

#ifdef QRSDP_KAFKA_ENABLED

#include "io/i_event_sink.h"
#include "io/event_log_format.h"
#include "core/records.h"

#include <librdkafka/rdkafkacpp.h>

#include <cstdio>
#include <memory>
#include <string>

namespace qrsdp {

/// Kafka event sink: publishes each EventRecord as a 26-byte binary message
/// to a Kafka topic. Uses the symbol as message key for partition affinity.
/// Best-effort: delivery failures are logged but do not block the producer.
class KafkaSink : public IEventSink {
public:
    KafkaSink(const std::string& brokers,
              const std::string& topic,
              const std::string& symbol);

    ~KafkaSink() override;

    KafkaSink(const KafkaSink&) = delete;
    KafkaSink& operator=(const KafkaSink&) = delete;

    void append(const EventRecord& rec) override;
    void flush() override;
    void close() override;

private:
    std::string symbol_;
    std::unique_ptr<RdKafka::Producer> producer_;
    RdKafka::Topic* topic_ = nullptr;  // owned by producer_ lifetime

    class DeliveryReportCb : public RdKafka::DeliveryReportCb {
    public:
        void dr_cb(RdKafka::Message& message) override {
            if (message.err()) {
                std::fprintf(stderr, "KafkaSink: delivery failed: %s\n",
                             message.errstr().c_str());
            }
        }
    };

    DeliveryReportCb dr_cb_;
};

}  // namespace qrsdp

#endif  // QRSDP_KAFKA_ENABLED
