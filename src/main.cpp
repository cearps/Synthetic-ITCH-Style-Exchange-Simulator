#include "producer/qrsdp_producer.h"
#include "io/in_memory_sink.h"
#include "io/binary_file_sink.h"
#include "book/multi_level_book.h"
#include "model/simple_imbalance_intensity.h"
#include "rng/mt19937_rng.h"
#include "sampler/competing_intensity_sampler.h"
#include "sampler/unit_size_attribute_sampler.h"
#include "core/records.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

static void printUsage(const char* prog) {
    std::fprintf(stderr, "Usage: %s [seed] [seconds] [output.qrsdp]\n", prog);
}

int main(int argc, char* argv[]) {
    const uint64_t seed = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 42;
    const uint32_t seconds = (argc > 2) ? static_cast<uint32_t>(std::atoi(argv[2])) : 30;
    const char* output_path = (argc > 3) ? argv[3] : nullptr;

    if (argc > 4) {
        printUsage(argv[0]);
        return 1;
    }

    qrsdp::Mt19937Rng rng(seed);
    qrsdp::MultiLevelBook book;
    qrsdp::IntensityParams params{22.0, 0.2, 30.0, 1.0, 1.0, 0.5, 0.0};
    qrsdp::SimpleImbalanceIntensity model(params);
    qrsdp::CompetingIntensitySampler sampler(rng);
    qrsdp::UnitSizeAttributeSampler attrs(rng, 0.5);
    qrsdp::QrsdpProducer producer(rng, book, model, sampler, attrs);

    qrsdp::TradingSession session{};
    session.seed = seed;
    session.p0_ticks = 10000;
    session.session_seconds = seconds;
    session.levels_per_side = 5;
    session.tick_size = 100;
    session.initial_spread_ticks = 2;
    session.initial_depth = 5;
    session.intensity_params = params;

    std::unique_ptr<qrsdp::IEventSink> sink;
    if (output_path) {
        sink = std::make_unique<qrsdp::BinaryFileSink>(output_path, session);
    } else {
        sink = std::make_unique<qrsdp::InMemorySink>();
    }

    qrsdp::SessionResult result = producer.runSession(session, *sink);

    std::printf("seed=%llu  seconds=%u  events=%llu  close=%d  shifts=%llu\n",
                (unsigned long long)seed, seconds,
                (unsigned long long)result.events_written,
                result.close_ticks,
                (unsigned long long)producer.shiftCountThisSession());

    if (output_path) {
        auto* file_sink = static_cast<qrsdp::BinaryFileSink*>(sink.get());
        file_sink->close();
        std::printf("wrote %s  (%u chunks)\n", output_path, file_sink->chunksWritten());
    }

    return 0;
}
