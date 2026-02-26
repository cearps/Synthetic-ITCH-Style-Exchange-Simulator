#ifdef QRSDP_KAFKA_ENABLED

#include "itch/itch_stream_consumer.h"
#include "itch/itch_encoder.h"
#include "itch/itch_messages.h"
#include "itch/moldudp64.h"
#include "itch/udp_sender.h"
#include "io/event_log_format.h"

#include <librdkafka/rdkafkacpp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace qrsdp {
namespace itch {

struct ItchStreamConsumer::Impl {
    ItchStreamConfig config;
    std::unique_ptr<RdKafka::KafkaConsumer> consumer;
    std::unique_ptr<UdpMulticastSender> sender;
    MoldUDP64Framer framer;
    std::unordered_map<std::string, std::unique_ptr<ItchEncoder>> encoders;
    uint16_t next_locate = 1;
    uint64_t last_ts_ns = 0;
    bool seen_first_event = false;

    explicit Impl(const ItchStreamConfig& cfg)
        : config(cfg)
        , framer("QRSDPITCH ")
    {}

    void emitSystemEvent(char code, uint64_t ts_ns) {
        ItchEncoder sys_enc("", 0, config.tick_size);
        auto msg = sys_enc.encodeSystemEvent(code, ts_ns);
        framer.addMessage(msg.data(), static_cast<uint16_t>(msg.size()));
    }

    ItchEncoder& getEncoder(const std::string& symbol) {
        auto it = encoders.find(symbol);
        if (it != encoders.end())
            return *it->second;

        uint16_t locate = next_locate++;
        auto enc = std::make_unique<ItchEncoder>(symbol, locate, config.tick_size);

        // Emit Stock Directory for the new symbol
        auto dir_msg = enc->encodeStockDirectory(0);
        framer.addMessage(dir_msg.data(), static_cast<uint16_t>(dir_msg.size()));

        auto [inserted, ok] = encoders.emplace(symbol, std::move(enc));
        return *inserted->second;
    }
};

ItchStreamConsumer::ItchStreamConsumer(const ItchStreamConfig& config)
    : impl_(new Impl(config))
{
    std::string errstr;

    auto conf = std::unique_ptr<RdKafka::Conf>(
        RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

    if (conf->set("bootstrap.servers", config.kafka_brokers, errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("ItchStreamConsumer: " + errstr);
    if (conf->set("group.id", config.consumer_group, errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("ItchStreamConsumer: " + errstr);
    if (conf->set("auto.offset.reset", "earliest", errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("ItchStreamConsumer: " + errstr);
    if (conf->set("enable.auto.commit", "true", errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("ItchStreamConsumer: " + errstr);

    impl_->consumer.reset(RdKafka::KafkaConsumer::create(conf.get(), errstr));
    if (!impl_->consumer)
        throw std::runtime_error("ItchStreamConsumer: failed to create consumer: " + errstr);

    std::vector<std::string> topics = { config.kafka_topic };
    auto err = impl_->consumer->subscribe(topics);
    if (err != RdKafka::ERR_NO_ERROR)
        throw std::runtime_error("ItchStreamConsumer: subscribe failed: " +
                                 RdKafka::err2str(err));

    if (!config.unicast_dest.empty()) {
        auto colon = config.unicast_dest.rfind(':');
        if (colon == std::string::npos || colon == 0)
            throw std::runtime_error("ItchStreamConsumer: bad --unicast-dest, expected host:port");
        std::string host = config.unicast_dest.substr(0, colon);
        uint16_t uport = static_cast<uint16_t>(std::atoi(
            config.unicast_dest.substr(colon + 1).c_str()));
        impl_->sender = UdpMulticastSender::createUnicast(host, uport);
        std::printf("ItchStreamConsumer: consuming %s from %s, unicast to %s\n",
                    config.kafka_topic.c_str(), config.kafka_brokers.c_str(),
                    config.unicast_dest.c_str());
    } else {
        impl_->sender = std::make_unique<UdpMulticastSender>(
            config.multicast_group, config.port, config.ttl);
        std::printf("ItchStreamConsumer: consuming %s from %s, multicast to %s:%u\n",
                    config.kafka_topic.c_str(), config.kafka_brokers.c_str(),
                    config.multicast_group.c_str(), config.port);
    }

    impl_->framer.setSendCallback([this](const uint8_t* data, size_t len) {
        impl_->sender->send(data, len);
    });
}

ItchStreamConsumer::~ItchStreamConsumer() {
    if (impl_->consumer)
        impl_->consumer->close();
    delete impl_;
}

void ItchStreamConsumer::run() {
    running_ = true;

    // Emit System Event: start of messages
    {
        ItchEncoder sys_enc("", 0, impl_->config.tick_size);
        auto msg = sys_enc.encodeSystemEvent(kSystemEventStartOfMessages, 0);
        impl_->framer.addMessage(msg.data(), static_cast<uint16_t>(msg.size()));
        auto pkt = impl_->framer.flush();
        if (!pkt.empty())
            impl_->sender->send(pkt.data(), pkt.size());
    }

    uint64_t total_messages = 0;

    while (running_) {
        std::unique_ptr<RdKafka::Message> kafka_msg(impl_->consumer->consume(100));

        if (kafka_msg->err() == RdKafka::ERR__TIMED_OUT)
            continue;

        if (kafka_msg->err() == RdKafka::ERR__PARTITION_EOF)
            continue;

        if (kafka_msg->err() != RdKafka::ERR_NO_ERROR) {
            std::fprintf(stderr, "ItchStreamConsumer: consumer error: %s\n",
                         kafka_msg->errstr().c_str());
            continue;
        }

        if (kafka_msg->len() != sizeof(DiskEventRecord)) {
            std::fprintf(stderr, "ItchStreamConsumer: unexpected message size %zu (expected %zu)\n",
                         kafka_msg->len(), sizeof(DiskEventRecord));
            continue;
        }

        // Extract symbol from message key
        std::string symbol;
        if (kafka_msg->key() && !kafka_msg->key()->empty())
            symbol = *kafka_msg->key();
        else
            symbol = "UNKNOWN";

        // Deserialize
        DiskEventRecord disk;
        std::memcpy(&disk, kafka_msg->payload(), sizeof(disk));

        EventRecord rec{};
        rec.ts_ns       = disk.ts_ns;
        rec.type        = disk.type;
        rec.side        = disk.side;
        rec.price_ticks = disk.price_ticks;
        rec.qty         = disk.qty;
        rec.order_id    = disk.order_id;
        rec.flags       = 0;

        // Day-boundary detection: timestamp going backward indicates a new trading day
        if (!impl_->seen_first_event) {
            impl_->emitSystemEvent(kSystemEventStartOfMarket, rec.ts_ns);
            impl_->seen_first_event = true;
        } else if (rec.ts_ns < impl_->last_ts_ns) {
            impl_->emitSystemEvent(kSystemEventEndOfMarket, impl_->last_ts_ns);
            impl_->emitSystemEvent(kSystemEventStartOfMarket, rec.ts_ns);
        }
        impl_->last_ts_ns = rec.ts_ns;

        // Encode to ITCH
        auto& encoder = impl_->getEncoder(symbol);
        auto itch_msg = encoder.encode(rec);
        impl_->framer.addMessage(itch_msg.data(), static_cast<uint16_t>(itch_msg.size()));

        ++total_messages;
        if ((total_messages & 0xFFFFF) == 0) {
            std::printf("ItchStreamConsumer: streamed %llu messages\n",
                        static_cast<unsigned long long>(total_messages));
        }
    }

    // Flush remaining buffered messages
    auto pkt = impl_->framer.flush();
    if (!pkt.empty())
        impl_->sender->send(pkt.data(), pkt.size());

    // Emit end-of-market for the last day (if we saw any events)
    if (impl_->seen_first_event) {
        impl_->emitSystemEvent(kSystemEventEndOfMarket, impl_->last_ts_ns);
        pkt = impl_->framer.flush();
        if (!pkt.empty())
            impl_->sender->send(pkt.data(), pkt.size());
    }

    // Emit System Event: end of messages
    {
        ItchEncoder sys_enc("", 0, impl_->config.tick_size);
        auto msg = sys_enc.encodeSystemEvent(kSystemEventEndOfMessages, 0);
        impl_->framer.addMessage(msg.data(), static_cast<uint16_t>(msg.size()));
        pkt = impl_->framer.flush();
        if (!pkt.empty())
            impl_->sender->send(pkt.data(), pkt.size());
    }

    std::printf("ItchStreamConsumer: stopped after %llu messages\n",
                static_cast<unsigned long long>(total_messages));
}

void ItchStreamConsumer::stop() {
    running_ = false;
}

}  // namespace itch
}  // namespace qrsdp

#endif  // QRSDP_KAFKA_ENABLED
