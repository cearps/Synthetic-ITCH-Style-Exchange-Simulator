#include "io/event_log_reader.h"
#include "io/event_log_format.h"
#include "core/event_types.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static const char* eventTypeName(uint8_t t) {
    switch (t) {
        case 0: return "ADD_BID";
        case 1: return "ADD_ASK";
        case 2: return "CANCEL_BID";
        case 3: return "CANCEL_ASK";
        case 4: return "EXECUTE_BUY";
        case 5: return "EXECUTE_SELL";
        default: return "UNKNOWN";
    }
}

static void printHeader(const qrsdp::FileHeader& h) {
    std::printf("=== File Header ===\n");
    std::printf("  version:             %u.%u\n", h.version_major, h.version_minor);
    std::printf("  record_size:         %u bytes\n", h.record_size);
    std::printf("  seed:                %llu\n", (unsigned long long)h.seed);
    std::printf("  p0_ticks:            %d\n", h.p0_ticks);
    std::printf("  tick_size:           %u\n", h.tick_size);
    std::printf("  session_seconds:     %u\n", h.session_seconds);
    std::printf("  levels_per_side:     %u\n", h.levels_per_side);
    std::printf("  initial_spread:      %u ticks\n", h.initial_spread_ticks);
    std::printf("  initial_depth:       %u\n", h.initial_depth);
    std::printf("  chunk_capacity:      %u\n", h.chunk_capacity);
    std::printf("  has_index:           %s\n",
                (h.header_flags & qrsdp::kHeaderFlagHasIndex) ? "yes" : "no");
}

static void printSummary(const qrsdp::EventLogReader& reader) {
    const auto& idx = reader.index();
    uint64_t total = reader.totalRecords();
    uint64_t first_ts = idx.empty() ? 0 : idx.front().first_ts_ns;
    uint64_t last_ts = idx.empty() ? 0 : idx.back().last_ts_ns;
    double duration_sec = static_cast<double>(last_ts - first_ts) / 1e9;

    std::printf("\n=== Summary ===\n");
    std::printf("  chunks:              %u\n", reader.chunkCount());
    std::printf("  total_records:       %llu\n", (unsigned long long)total);
    std::printf("  time_range:          %llu – %llu ns\n",
                (unsigned long long)first_ts, (unsigned long long)last_ts);
    std::printf("  duration:            %.3f s\n", duration_sec);
    if (duration_sec > 0.0)
        std::printf("  events/sec:          %.1f\n", static_cast<double>(total) / duration_sec);

    uint64_t raw_bytes = total * reader.header().record_size;
    std::printf("  raw_size:            %.2f MB\n", static_cast<double>(raw_bytes) / (1024.0 * 1024.0));
}

static void printEventDistribution(const qrsdp::EventLogReader& reader) {
    uint64_t counts[6] = {};
    uint64_t total = 0;

    for (uint32_t i = 0; i < reader.chunkCount(); ++i) {
        auto chunk = reader.readChunk(i);
        for (const auto& r : chunk) {
            if (r.type < 6) counts[r.type]++;
            total++;
        }
    }

    std::printf("\n=== Event Distribution ===\n");
    for (int t = 0; t < 6; ++t) {
        double pct = total > 0 ? 100.0 * static_cast<double>(counts[t]) / static_cast<double>(total) : 0.0;
        std::printf("  %-14s %10llu  (%5.1f%%)\n",
                    eventTypeName(static_cast<uint8_t>(t)),
                    (unsigned long long)counts[t], pct);
    }
}

static void printFirstN(const qrsdp::EventLogReader& reader, int n) {
    std::printf("\n=== First %d Records ===\n", n);
    std::printf("  %-18s %-14s %-5s %-12s %-6s %-10s\n",
                "ts_ns", "type", "side", "price_ticks", "qty", "order_id");

    int printed = 0;
    for (uint32_t c = 0; c < reader.chunkCount() && printed < n; ++c) {
        auto chunk = reader.readChunk(c);
        for (const auto& r : chunk) {
            if (printed >= n) break;
            std::printf("  %-18llu %-14s %-5u %-12d %-6u %-10llu\n",
                        (unsigned long long)r.ts_ns,
                        eventTypeName(r.type),
                        r.side, r.price_ticks, r.qty,
                        (unsigned long long)r.order_id);
            printed++;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <file.qrsdp> [--events N]\n", argv[0]);
        return 1;
    }

    const char* path = argv[1];
    int show_events = 10;

    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--events" && i + 1 < argc) {
            show_events = std::atoi(argv[++i]);
        }
    }

    try {
        qrsdp::EventLogReader reader(path);

        printHeader(reader.header());
        printSummary(reader);
        printEventDistribution(reader);
        printFirstN(reader, show_events);

    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
