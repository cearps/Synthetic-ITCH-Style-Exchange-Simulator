#pragma once

#ifdef QRSDP_KAFKA_ENABLED

#include <atomic>
#include <cstdint>
#include <string>

namespace qrsdp {
namespace itch {

struct ItchStreamConfig {
    std::string kafka_brokers  = "localhost:9092";
    std::string kafka_topic    = "exchange.events";
    std::string consumer_group = "itch-streamer";
    std::string multicast_group = "239.1.1.1";
    std::string unicast_dest;   // empty = multicast; "host:port" = unicast to specific destination
    uint16_t    port           = 5001;
    uint8_t     ttl            = 1;
    uint32_t    tick_size      = 100;
};

/// Kafka consumer that reads DiskEventRecords from a topic, encodes them
/// as ITCH 5.0 messages, frames in MoldUDP64 packets, and streams over
/// UDP multicast.
///
/// Each unique symbol (extracted from the Kafka message key) gets its own
/// ItchEncoder with a unique stock locate code. A single MoldUDP64Framer
/// and UdpMulticastSender are shared across all symbols.
class ItchStreamConsumer {
public:
    explicit ItchStreamConsumer(const ItchStreamConfig& config);
    ~ItchStreamConsumer();

    ItchStreamConsumer(const ItchStreamConsumer&) = delete;
    ItchStreamConsumer& operator=(const ItchStreamConsumer&) = delete;

    /// Blocking consume loop. Runs until stop() is called.
    void run();

    /// Signal the consumer to stop (thread-safe).
    void stop();

private:
    struct Impl;
    Impl* impl_;
    std::atomic<bool> running_{false};
};

}  // namespace itch
}  // namespace qrsdp

#endif  // QRSDP_KAFKA_ENABLED
