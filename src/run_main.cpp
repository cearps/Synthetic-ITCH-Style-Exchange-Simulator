#include "producer/session_runner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "  --seed <n>          Base seed (default: 42)\n"
        "  --days <n>          Number of trading days (default: 5)\n"
        "  --seconds <n>       Seconds per session (default: 23400)\n"
        "  --p0 <ticks>        Opening price in ticks (default: 10000)\n"
        "  --output <dir>      Output directory (default: output/run_<seed>)\n"
        "  --start-date <str>  First trading date (default: 2026-01-02)\n"
        "  --chunk-size <n>    Records per chunk (default: 4096)\n"
        "  --perf-doc <path>   Write performance doc (default: <output>/performance-results.md)\n"
        "  --depth <n>         Initial depth per level (default: 5)\n"
        "  --levels <n>        Levels per side (default: 5)\n"
        "  --help              Show this help\n",
        prog);
}

int main(int argc, char* argv[]) {
    uint64_t seed = 42;
    uint32_t days = 5;
    uint32_t seconds = 23400;
    int32_t  p0 = 10000;
    std::string output_dir;
    std::string start_date = "2026-01-02";
    uint32_t chunk_size = 0;
    std::string perf_doc;
    uint32_t depth = 5;
    uint32_t levels = 5;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", arg);
                std::exit(1);
            }
            return argv[++i];
        };

        if (std::strcmp(arg, "--seed") == 0)        seed = std::strtoull(next(), nullptr, 10);
        else if (std::strcmp(arg, "--days") == 0)    days = static_cast<uint32_t>(std::atoi(next()));
        else if (std::strcmp(arg, "--seconds") == 0) seconds = static_cast<uint32_t>(std::atoi(next()));
        else if (std::strcmp(arg, "--p0") == 0)      p0 = std::atoi(next());
        else if (std::strcmp(arg, "--output") == 0)  output_dir = next();
        else if (std::strcmp(arg, "--start-date") == 0) start_date = next();
        else if (std::strcmp(arg, "--chunk-size") == 0)  chunk_size = static_cast<uint32_t>(std::atoi(next()));
        else if (std::strcmp(arg, "--perf-doc") == 0)    perf_doc = next();
        else if (std::strcmp(arg, "--depth") == 0)   depth = static_cast<uint32_t>(std::atoi(next()));
        else if (std::strcmp(arg, "--levels") == 0)  levels = static_cast<uint32_t>(std::atoi(next()));
        else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (output_dir.empty()) {
        output_dir = "output/run_" + std::to_string(seed);
    }
    if (perf_doc.empty()) {
        perf_doc = output_dir + "/performance-results.md";
    }

    char run_id[64];
    std::snprintf(run_id, sizeof(run_id), "run_%llu", (unsigned long long)seed);

    qrsdp::RunConfig config{};
    config.run_id = run_id;
    config.output_dir = output_dir;
    config.base_seed = seed;
    config.p0_ticks = p0;
    config.session_seconds = seconds;
    config.levels_per_side = levels;
    config.tick_size = 100;
    config.initial_spread_ticks = 2;
    config.initial_depth = depth;
    config.intensity_params = {22.0, 0.2, 30.0, 1.0, 1.0, 0.5};
    config.num_days = days;
    config.chunk_capacity = chunk_size;
    config.start_date = start_date;

    std::printf("=== qrsdp_run ===\n");
    std::printf("seed=%llu  days=%u  seconds=%u  p0=%d  output=%s\n",
                (unsigned long long)seed, days, seconds, p0, output_dir.c_str());

    qrsdp::SessionRunner runner;
    qrsdp::RunResult result = runner.run(config);

    std::printf("\n--- Summary ---\n");
    for (const auto& d : result.days) {
        const double w_eps = d.write_seconds > 0.0
            ? static_cast<double>(d.events_written) / d.write_seconds : 0.0;
        const double r_eps = d.read_seconds > 0.0
            ? static_cast<double>(d.events_written) / d.read_seconds : 0.0;
        const double raw = static_cast<double>(d.events_written * 26);
        const double ratio = d.file_size_bytes > 0 ? raw / static_cast<double>(d.file_size_bytes) : 0.0;
        std::printf("  %s  seed=%llu  events=%llu  chunks=%u  file=%llu B  "
                    "ratio=%.2fx  W:%.0f ev/s (%.2fs)  R:%.0f ev/s (%.2fs)  "
                    "open=%d close=%d\n",
                    d.date.c_str(),
                    (unsigned long long)d.seed,
                    (unsigned long long)d.events_written,
                    d.chunks_written,
                    (unsigned long long)d.file_size_bytes,
                    ratio, w_eps, d.write_seconds,
                    r_eps, d.read_seconds,
                    d.open_ticks, d.close_ticks);
    }
    std::printf("\nTotal: %llu events in %.2f s\n",
                (unsigned long long)result.total_events,
                result.total_elapsed_seconds);

    qrsdp::SessionRunner::writePerformanceResults(config, result, perf_doc);
    std::printf("Wrote %s\n", perf_doc.c_str());
    std::printf("Wrote %s/manifest.json\n", output_dir.c_str());

    return 0;
}
