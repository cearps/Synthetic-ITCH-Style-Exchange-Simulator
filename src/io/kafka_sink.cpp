#ifdef QRSDP_KAFKA_ENABLED

#include "io/kafka_sink.h"

#include <cstring>
#include <stdexcept>

namespace qrsdp {

KafkaSink::KafkaSink(const std::string& brokers,
                     const std::string& topic_name,
                     const std::string& symbol)
    : symbol_(symbol)
{
    std::string errstr;

    auto conf = std::unique_ptr<RdKafka::Conf>(
        RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

    if (conf->set("bootstrap.servers", brokers, errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("KafkaSink: " + errstr);
    if (conf->set("enable.idempotence", "true", errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("KafkaSink: " + errstr);
    if (conf->set("linger.ms", "5", errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("KafkaSink: " + errstr);
    if (conf->set("compression.type", "lz4", errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("KafkaSink: " + errstr);
    if (conf->set("dr_cb", &dr_cb_, errstr) != RdKafka::Conf::CONF_OK)
        throw std::runtime_error("KafkaSink: " + errstr);

    producer_.reset(RdKafka::Producer::create(conf.get(), errstr));
    if (!producer_)
        throw std::runtime_error("KafkaSink: failed to create producer: " + errstr);

    auto tconf = std::unique_ptr<RdKafka::Conf>(
        RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC));

    topic_ = RdKafka::Topic::create(producer_.get(), topic_name, tconf.get(), errstr);
    if (!topic_)
        throw std::runtime_error("KafkaSink: failed to create topic: " + errstr);
}

KafkaSink::~KafkaSink() {
    close();
    delete topic_;
}

void KafkaSink::append(const EventRecord& rec) {
    DiskEventRecord disk;
    disk.ts_ns       = rec.ts_ns;
    disk.type        = rec.type;
    disk.side        = rec.side;
    disk.price_ticks = rec.price_ticks;
    disk.qty         = rec.qty;
    disk.order_id    = rec.order_id;

    RdKafka::ErrorCode err = producer_->produce(
        topic_,
        RdKafka::Topic::PARTITION_UA,
        RdKafka::Producer::RK_MSG_COPY,
        &disk, sizeof(disk),
        symbol_.data(), symbol_.size(),
        nullptr);

    if (err == RdKafka::ERR__QUEUE_FULL) {
        producer_->poll(100);
        producer_->produce(
            topic_,
            RdKafka::Topic::PARTITION_UA,
            RdKafka::Producer::RK_MSG_COPY,
            &disk, sizeof(disk),
            symbol_.data(), symbol_.size(),
            nullptr);
    } else if (err != RdKafka::ERR_NO_ERROR) {
        std::fprintf(stderr, "KafkaSink: produce failed: %s\n",
                     RdKafka::err2str(err).c_str());
    }

    producer_->poll(0);
}

void KafkaSink::flush() {
    if (producer_)
        producer_->flush(5000);
}

void KafkaSink::close() {
    if (producer_)
        producer_->flush(10000);
}

}  // namespace qrsdp

#endif  // QRSDP_KAFKA_ENABLED
