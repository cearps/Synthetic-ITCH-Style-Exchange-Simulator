#pragma once

#include "core/records.h"
#include "model/hlr_params.h"
#include <cstdint>
#include <string>
#include <vector>

namespace qrsdp {

enum class ModelType { SIMPLE, HLR };

struct SecurityConfig {
    std::string symbol;
    int32_t  p0_ticks;
    uint32_t tick_size;
    uint32_t levels_per_side;
    uint32_t initial_spread_ticks;
    uint32_t initial_depth;
    IntensityParams intensity_params;
    QueueReactiveParams queue_reactive;
    ModelType model_type = ModelType::SIMPLE;
};

struct RunConfig {
    std::string run_id;
    std::string output_dir;
    uint64_t base_seed;
    int32_t  p0_ticks;
    uint32_t session_seconds;
    uint32_t levels_per_side;
    uint32_t tick_size;
    uint32_t initial_spread_ticks;
    uint32_t initial_depth;
    IntensityParams intensity_params;
    QueueReactiveParams queue_reactive;
    ModelType model_type = ModelType::SIMPLE;
    HLRParams hlr_params;          // used when model_type == HLR; if !hasCurves(), use defaults
    uint32_t num_days;
    uint32_t chunk_capacity;    // 0 = use default (4096)
    std::string start_date;     // "YYYY-MM-DD"
    std::vector<SecurityConfig> securities;  // empty = single-security mode
    std::string kafka_brokers;  // empty = no Kafka (file-only)
    std::string kafka_topic = "exchange.events";
    uint32_t market_open_seconds = kDefaultMarketOpenSeconds;
    bool realtime = false;      // pace events to simulated inter-arrival times
    double speed = 1.0;         // wall-clock multiplier (100 = 100x faster than real time)
};

struct DayResult {
    std::string symbol;         // empty for single-security runs
    std::string date;
    std::string filename;       // relative to output_dir
    uint64_t seed;
    int32_t  open_ticks;
    int32_t  close_ticks;
    uint64_t events_written;
    uint32_t chunks_written;
    uint64_t file_size_bytes;
    double   write_seconds;
    double   read_seconds;      // sequential read-back benchmark
};

struct RunResult {
    std::vector<DayResult> days;
    double total_elapsed_seconds;
    uint64_t total_events;
};

/// Drives multiple consecutive trading sessions (days) with continuous chaining:
/// each day's opening price = previous day's closing price.
/// Writes one .qrsdp file per day plus a manifest.json.
class SessionRunner {
public:
    RunResult run(const RunConfig& config);

    static void writeManifest(const RunConfig& config, const RunResult& result);
    static void writePerformanceResults(const RunConfig& config,
                                        const RunResult& result,
                                        const std::string& path);
};

/// Install SIGTERM/SIGINT handler for graceful shutdown in continuous mode.
void installShutdownHandler();

// Date helpers (exposed for testing)
struct Date {
    int year;
    int month;  // 1-12
    int day;    // 1-31
};

Date parseDate(const std::string& s);
std::string formatDate(const Date& d);
Date nextBusinessDay(const Date& d);
int dayOfWeek(const Date& d);  // 0=Sun, 6=Sat

}  // namespace qrsdp
