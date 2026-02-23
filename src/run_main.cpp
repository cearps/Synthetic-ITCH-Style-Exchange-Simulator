#include "producer/session_runner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
        "  --securities <spec> Comma-separated symbol:p0 pairs (e.g. AAPL:10000,MSFT:15000)\n"
        "  --help              Show this help\n",
        prog);
}

static std::vector<qrsdp::SecurityConfig> parseSecurities(
    const char* spec,
    uint32_t tick_size, uint32_t levels_per_side,
    uint32_t initial_spread_ticks, uint32_t initial_depth,
    const qrsdp::IntensityParams& ip, const qrsdp::QueueReactiveParams& qr)
{
    std::vector<qrsdp::SecurityConfig> result;
    std::string s(spec);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t comma = s.find(',', pos);
        if (comma == std::string::npos) comma = s.size();
        std::string token = s.substr(pos, comma - pos);
        pos = comma + 1;

        size_t colon = token.find(':');
        if (colon == std::string::npos || colon == 0) {
            std::fprintf(stderr, "bad securities spec token: %s (expected SYMBOL:P0)\n", token.c_str());
            std::exit(1);
        }
        qrsdp::SecurityConfig sec{};
        sec.symbol = token.substr(0, colon);
        sec.p0_ticks = std::atoi(token.substr(colon + 1).c_str());
        sec.tick_size = tick_size;
        sec.levels_per_side = levels_per_side;
        sec.initial_spread_ticks = initial_spread_ticks;
        sec.initial_depth = initial_depth;
        sec.intensity_params = ip;
        sec.queue_reactive = qr;
        result.push_back(sec);
    }
    return result;
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
    std::string securities_spec;

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
        else if (std::strcmp(arg, "--securities") == 0) securities_spec = next();
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

    if (!securities_spec.empty()) {
        config.securities = parseSecurities(
            securities_spec.c_str(),
            config.tick_size, config.levels_per_side,
            config.initial_spread_ticks, config.initial_depth,
            config.intensity_params, config.queue_reactive);
    }

    std::printf("=== qrsdp_run ===\n");
    std::printf("seed=%llu  days=%u  seconds=%u  p0=%d  output=%s\n",
                (unsigned long long)seed, days, seconds, p0, output_dir.c_str());
    if (!config.securities.empty()) {
        std::printf("securities:");
        for (const auto& s : config.securities)
            std::printf(" %s:%d", s.symbol.c_str(), s.p0_ticks);
        std::printf("\n");
    }

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
        std::printf("  %s%s%s  seed=%llu  events=%llu  chunks=%u  file=%llu B  "
                    "ratio=%.2fx  W:%.0f ev/s (%.2fs)  R:%.0f ev/s (%.2fs)  "
                    "open=%d close=%d\n",
                    d.symbol.empty() ? "" : d.symbol.c_str(),
                    d.symbol.empty() ? "" : " ",
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
