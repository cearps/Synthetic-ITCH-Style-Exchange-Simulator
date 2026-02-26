#ifdef QRSDP_KAFKA_ENABLED

#include "itch/itch_stream_consumer.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

static qrsdp::itch::ItchStreamConsumer* g_consumer = nullptr;

static void signalHandler(int) {
    if (g_consumer)
        g_consumer->stop();
}

static void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "  --kafka-brokers <s>   Kafka bootstrap servers (default: localhost:9092)\n"
        "  --kafka-topic <s>     Kafka topic (default: exchange.events)\n"
        "  --consumer-group <s>  Consumer group ID (default: itch-streamer)\n"
        "  --multicast-group <s> Multicast address (default: 239.1.1.1)\n"
        "  --unicast-dest <h:p>  Send unicast to host:port instead of multicast\n"
        "  --port <n>            UDP port (default: 5001)\n"
        "  --tick-size <n>       Tick size in price-4 units (default: 100)\n"
        "  --help                Show this help\n",
        prog);
}

int main(int argc, char* argv[]) {
    qrsdp::itch::ItchStreamConfig config;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", arg);
                std::exit(1);
            }
            return argv[++i];
        };

        if (std::strcmp(arg, "--kafka-brokers") == 0)    config.kafka_brokers = next();
        else if (std::strcmp(arg, "--kafka-topic") == 0)  config.kafka_topic = next();
        else if (std::strcmp(arg, "--consumer-group") == 0) config.consumer_group = next();
        else if (std::strcmp(arg, "--multicast-group") == 0) config.multicast_group = next();
        else if (std::strcmp(arg, "--unicast-dest") == 0)  config.unicast_dest = next();
        else if (std::strcmp(arg, "--port") == 0)         config.port = static_cast<uint16_t>(std::atoi(next()));
        else if (std::strcmp(arg, "--tick-size") == 0)    config.tick_size = static_cast<uint32_t>(std::atoi(next()));
        else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    std::printf("=== qrsdp_itch_stream ===\n");
    std::printf("kafka=%s  topic=%s  group=%s\n",
                config.kafka_brokers.c_str(), config.kafka_topic.c_str(),
                config.consumer_group.c_str());
    if (!config.unicast_dest.empty())
        std::printf("unicast_dest=%s  tick_size=%u\n",
                    config.unicast_dest.c_str(), config.tick_size);
    else
        std::printf("multicast=%s:%u  tick_size=%u\n",
                    config.multicast_group.c_str(), config.port, config.tick_size);

    qrsdp::itch::ItchStreamConsumer consumer(config);
    g_consumer = &consumer;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    consumer.run();
    return 0;
}

#else

#include <cstdio>

int main() {
    std::fprintf(stderr,
        "qrsdp_itch_stream: built without Kafka support.\n"
        "Rebuild with -DBUILD_KAFKA_SUPPORT=ON to enable.\n");
    return 1;
}

#endif  // QRSDP_KAFKA_ENABLED
