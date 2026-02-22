#include "producer/qrsdp_producer.h"
#include "io/in_memory_sink.h"
#include "book/multi_level_book.h"
#include "model/simple_imbalance_intensity.h"
#include "rng/mt19937_rng.h"
#include "sampler/competing_intensity_sampler.h"
#include "sampler/unit_size_attribute_sampler.h"
#include "core/records.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    const uint64_t seed = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 42;
    const uint32_t seconds = (argc > 2) ? static_cast<uint32_t>(std::atoi(argv[2])) : 30;

    qrsdp::Mt19937Rng rng(seed);
    qrsdp::MultiLevelBook book;
    qrsdp::IntensityParams params{5.0, 0.1, 30.0, 1.0, 1.0, 0.2};
    qrsdp::SimpleImbalanceIntensity model(params);
    qrsdp::CompetingIntensitySampler sampler(rng);
    qrsdp::UnitSizeAttributeSampler attrs(rng, 0.5);
    qrsdp::QrsdpProducer producer(rng, book, model, sampler, attrs);
    qrsdp::InMemorySink sink;

    qrsdp::TradingSession session{};
    session.seed = seed;
    session.p0_ticks = 10000;
    session.session_seconds = seconds;
    session.levels_per_side = 5;
    session.tick_size = 100;
    session.initial_spread_ticks = 2;
    session.initial_depth = 2;
    session.intensity_params = params;

    qrsdp::SessionResult result = producer.runSession(session, sink);

    std::printf("seed=%llu  seconds=%u  events=%llu  close=%d  shifts=%llu\n",
                (unsigned long long)seed, seconds,
                (unsigned long long)result.events_written,
                result.close_ticks,
                (unsigned long long)producer.shiftCountThisSession());
    return 0;
}
