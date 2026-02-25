#include "producer/session_runner.h"
#include "producer/qrsdp_producer.h"
#include "io/binary_file_sink.h"
#include "io/multiplex_sink.h"
#include "io/event_log_reader.h"
#include "io/event_log_format.h"
#include "book/multi_level_book.h"
#include "model/simple_imbalance_intensity.h"
#include "model/curve_intensity_model.h"
#include "model/hlr_params.h"
#include "rng/mt19937_rng.h"
#include "sampler/competing_intensity_sampler.h"
#include "sampler/unit_size_attribute_sampler.h"

#ifdef QRSDP_KAFKA_ENABLED
#include "io/kafka_sink.h"
#endif

#include <atomic>
#include <csignal>
#include <memory>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace qrsdp {

// ---------------------------------------------------------------------------
// Graceful shutdown on SIGTERM / SIGINT
// ---------------------------------------------------------------------------

static std::atomic<bool> g_shutdown_requested{false};

static void signalHandler(int /*sig*/) {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}

void installShutdownHandler() {
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);
}

// ---------------------------------------------------------------------------
// Date helpers
// ---------------------------------------------------------------------------

Date parseDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-')
        throw std::invalid_argument("date must be YYYY-MM-DD: " + s);
    Date d{};
    d.year  = std::stoi(s.substr(0, 4));
    d.month = std::stoi(s.substr(5, 2));
    d.day   = std::stoi(s.substr(8, 2));
    return d;
}

std::string formatDate(const Date& d) {
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.day);
    return buf;
}

int dayOfWeek(const Date& d) {
    std::tm tm{};
    tm.tm_year = d.year - 1900;
    tm.tm_mon  = d.month - 1;
    tm.tm_mday = d.day;
    tm.tm_isdst = -1;
    std::mktime(&tm);
    return tm.tm_wday;  // 0=Sun, 6=Sat
}

Date nextBusinessDay(const Date& d) {
    std::tm tm{};
    tm.tm_year = d.year - 1900;
    tm.tm_mon  = d.month - 1;
    tm.tm_mday = d.day + 1;
    tm.tm_isdst = -1;
    std::mktime(&tm);  // normalises overflows
    while (tm.tm_wday == 0 || tm.tm_wday == 6) {
        tm.tm_mday += 1;
        std::mktime(&tm);
    }
    return Date{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

// ---------------------------------------------------------------------------
// Manifest writer (hand-rolled JSON)
// ---------------------------------------------------------------------------

void SessionRunner::writeManifest(const RunConfig& config, const RunResult& result) {
    namespace fs = std::filesystem;
    const std::string path = (fs::path(config.output_dir) / "manifest.json").string();
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("cannot open manifest: " + path);

    const bool multi = !config.securities.empty();

    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"format_version\": \"%s\",\n", multi ? "1.1" : "1.0");
    std::fprintf(f, "  \"run_id\": \"%s\",\n", config.run_id.c_str());
    std::fprintf(f, "  \"producer\": \"qrsdp\",\n");
    std::fprintf(f, "  \"base_seed\": %llu,\n", (unsigned long long)config.base_seed);
    std::fprintf(f, "  \"seed_strategy\": \"sequential\",\n");
    std::fprintf(f, "  \"session_seconds\": %u,\n", config.session_seconds);

    if (multi) {
        std::fprintf(f, "  \"securities\": [\n");
        for (size_t si = 0; si < config.securities.size(); ++si) {
            const auto& sec = config.securities[si];
            std::fprintf(f, "    {\n");
            std::fprintf(f, "      \"symbol\": \"%s\",\n", sec.symbol.c_str());
            std::fprintf(f, "      \"p0_ticks\": %d,\n", sec.p0_ticks);
            std::fprintf(f, "      \"tick_size\": %u,\n", sec.tick_size);
            std::fprintf(f, "      \"levels_per_side\": %u,\n", sec.levels_per_side);
            std::fprintf(f, "      \"initial_spread_ticks\": %u,\n", sec.initial_spread_ticks);
            std::fprintf(f, "      \"initial_depth\": %u,\n", sec.initial_depth);
            std::fprintf(f, "      \"sessions\": [\n");

            size_t count = 0;
            for (const auto& d : result.days) {
                if (d.symbol == sec.symbol) ++count;
            }
            size_t idx = 0;
            for (const auto& d : result.days) {
                if (d.symbol != sec.symbol) continue;
                ++idx;
                std::fprintf(f, "        { \"date\": \"%s\", \"seed\": %llu, \"file\": \"%s\" }%s\n",
                             d.date.c_str(),
                             (unsigned long long)d.seed,
                             d.filename.c_str(),
                             (idx < count) ? "," : "");
            }
            std::fprintf(f, "      ]\n");
            std::fprintf(f, "    }%s\n", (si + 1 < config.securities.size()) ? "," : "");
        }
        std::fprintf(f, "  ]\n");
    } else {
        std::fprintf(f, "  \"tick_size\": %u,\n", config.tick_size);
        std::fprintf(f, "  \"p0_ticks\": %d,\n", config.p0_ticks);
        std::fprintf(f, "  \"levels_per_side\": %u,\n", config.levels_per_side);
        std::fprintf(f, "  \"initial_spread_ticks\": %u,\n", config.initial_spread_ticks);
        std::fprintf(f, "  \"initial_depth\": %u,\n", config.initial_depth);
        std::fprintf(f, "  \"sessions\": [\n");
        for (size_t i = 0; i < result.days.size(); ++i) {
            const auto& d = result.days[i];
            std::fprintf(f, "    { \"date\": \"%s\", \"seed\": %llu, \"file\": \"%s\" }%s\n",
                         d.date.c_str(),
                         (unsigned long long)d.seed,
                         d.filename.c_str(),
                         (i + 1 < result.days.size()) ? "," : "");
        }
        std::fprintf(f, "  ]\n");
    }

    std::fprintf(f, "}\n");
    std::fclose(f);
}

// ---------------------------------------------------------------------------
// Performance results writer (markdown)
// ---------------------------------------------------------------------------

void SessionRunner::writePerformanceResults(const RunConfig& config,
                                            const RunResult& result,
                                            const std::string& path) {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path).parent_path());

    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("cannot open perf doc: " + path);

    std::fprintf(f, "# Performance Results\n\n");
    std::fprintf(f, "Auto-generated by `qrsdp_run`.\n\n");

    std::fprintf(f, "## Run Configuration\n\n");
    std::fprintf(f, "| Parameter | Value |\n");
    std::fprintf(f, "|:----------|:------|\n");
    std::fprintf(f, "| run_id | %s |\n", config.run_id.c_str());
    std::fprintf(f, "| base_seed | %llu |\n", (unsigned long long)config.base_seed);
    std::fprintf(f, "| num_days | %u |\n", config.num_days);
    std::fprintf(f, "| session_seconds | %u |\n", config.session_seconds);
    std::fprintf(f, "| p0_ticks | %d |\n", config.p0_ticks);
    std::fprintf(f, "| tick_size | %u |\n", config.tick_size);
    std::fprintf(f, "| levels_per_side | %u |\n", config.levels_per_side);
    std::fprintf(f, "| initial_depth | %u |\n", config.initial_depth);
    std::fprintf(f, "| chunk_capacity | %u |\n",
                 config.chunk_capacity > 0 ? config.chunk_capacity : kDefaultChunkCapacity);
    std::fprintf(f, "| base_L | %.1f |\n", config.intensity_params.base_L);
    std::fprintf(f, "| base_C | %.1f |\n", config.intensity_params.base_C);
    std::fprintf(f, "| base_M | %.1f |\n", config.intensity_params.base_M);
    std::fprintf(f, "\n");

    std::fprintf(f, "## Per-Day Results\n\n");
    std::fprintf(f, "| Date | Events | File Size | Compression | Write ev/s | Read ev/s | Write (s) | Read (s) | Open | Close |\n");
    std::fprintf(f, "|:-----|-------:|----------:|------------:|-----------:|----------:|----------:|---------:|-----:|------:|\n");

    uint64_t total_file_bytes = 0;
    uint64_t total_raw_bytes = 0;
    double total_write_secs = 0.0;
    double total_read_secs = 0.0;
    for (const auto& d : result.days) {
        const uint64_t raw = d.events_written * sizeof(DiskEventRecord);
        const double ratio = d.file_size_bytes > 0
            ? static_cast<double>(raw) / static_cast<double>(d.file_size_bytes)
            : 0.0;
        const double w_eps = d.write_seconds > 0.0
            ? static_cast<double>(d.events_written) / d.write_seconds
            : 0.0;
        const double r_eps = d.read_seconds > 0.0
            ? static_cast<double>(d.events_written) / d.read_seconds
            : 0.0;
        total_file_bytes += d.file_size_bytes;
        total_raw_bytes += raw;
        total_write_secs += d.write_seconds;
        total_read_secs += d.read_seconds;

        std::fprintf(f, "| %s | %llu | %llu B | %.2fx | %.0f | %.0f | %.2f | %.2f | %d | %d |\n",
                     d.date.c_str(),
                     (unsigned long long)d.events_written,
                     (unsigned long long)d.file_size_bytes,
                     ratio, w_eps, r_eps,
                     d.write_seconds, d.read_seconds,
                     d.open_ticks, d.close_ticks);
    }

    std::fprintf(f, "\n## Aggregate\n\n");
    std::fprintf(f, "| Metric | Value |\n");
    std::fprintf(f, "|:-------|:------|\n");
    std::fprintf(f, "| Total events | %llu |\n", (unsigned long long)result.total_events);
    std::fprintf(f, "| Total file size | %llu B (%.2f MB) |\n",
                 (unsigned long long)total_file_bytes,
                 static_cast<double>(total_file_bytes) / (1024.0 * 1024.0));
    std::fprintf(f, "| Total raw size | %llu B (%.2f MB) |\n",
                 (unsigned long long)total_raw_bytes,
                 static_cast<double>(total_raw_bytes) / (1024.0 * 1024.0));
    const double overall_ratio = total_file_bytes > 0
        ? static_cast<double>(total_raw_bytes) / static_cast<double>(total_file_bytes)
        : 0.0;
    const double mean_w_eps = total_write_secs > 0.0
        ? static_cast<double>(result.total_events) / total_write_secs
        : 0.0;
    const double mean_r_eps = total_read_secs > 0.0
        ? static_cast<double>(result.total_events) / total_read_secs
        : 0.0;
    std::fprintf(f, "| Overall compression | %.2fx |\n", overall_ratio);
    std::fprintf(f, "| Mean write throughput | %.0f events/sec |\n", mean_w_eps);
    std::fprintf(f, "| Mean read throughput | %.0f events/sec |\n", mean_r_eps);
    std::fprintf(f, "| Total wall time | %.2f s |\n", result.total_elapsed_seconds);
    std::fprintf(f, "\n");

    std::fclose(f);
}

// ---------------------------------------------------------------------------
// Per-security day loop (runs on its own thread for multi-security mode)
// ---------------------------------------------------------------------------

static constexpr uint64_t kSeedStride = 1024;

static std::vector<DayResult> runSecurityDays(
    const RunConfig& config,
    const std::string& symbol,
    int32_t  p0_ticks,
    uint32_t tick_size,
    uint32_t levels_per_side,
    uint32_t initial_spread_ticks,
    uint32_t initial_depth,
    const IntensityParams& intensity_params,
    const QueueReactiveParams& queue_reactive,
    ModelType model_type,
    uint64_t seed_offset)
{
    namespace fs = std::filesystem;

    const std::string sub_dir = symbol.empty()
        ? config.output_dir
        : (fs::path(config.output_dir) / symbol).string();
    fs::create_directories(sub_dir);

    const uint64_t base = config.base_seed + seed_offset;
    Mt19937Rng rng(base);
    MultiLevelBook book;

    std::unique_ptr<IIntensityModel> model_ptr;
    if (model_type == ModelType::HLR) {
        HLRParams hlr = config.hlr_params.hasCurves()
            ? config.hlr_params
            : makeDefaultHLRParams(static_cast<int>(levels_per_side));
        model_ptr = std::make_unique<CurveIntensityModel>(std::move(hlr));
    } else {
        model_ptr = std::make_unique<SimpleImbalanceIntensity>(intensity_params);
    }

    CompetingIntensitySampler sampler(rng);
    UnitSizeAttributeSampler attrs(rng, 0.5, 0.5);
    QrsdpProducer producer(rng, book, *model_ptr, sampler, attrs);

    const uint32_t chunk_cap = config.chunk_capacity > 0
        ? config.chunk_capacity : kDefaultChunkCapacity;

    std::vector<DayResult> days;
    Date current_date = parseDate(config.start_date);
    int32_t next_p0 = p0_ticks;

    const bool infinite = (config.num_days == 0);

    for (uint32_t day_idx = 0;
         infinite || day_idx < config.num_days;
         ++day_idx)
    {
        if (g_shutdown_requested.load(std::memory_order_relaxed)) break;

        const uint64_t day_seed = base + day_idx;
        const std::string date_str = formatDate(current_date);
        const std::string filename = symbol.empty()
            ? (date_str + ".qrsdp")
            : (symbol + "/" + date_str + ".qrsdp");
        const std::string filepath = (fs::path(config.output_dir) / filename).string();

        TradingSession session{};
        session.seed = day_seed;
        session.p0_ticks = next_p0;
        session.session_seconds = config.session_seconds;
        session.levels_per_side = levels_per_side;
        session.tick_size = tick_size;
        session.initial_spread_ticks = initial_spread_ticks;
        session.initial_depth = initial_depth;
        session.intensity_params = intensity_params;
        session.queue_reactive = queue_reactive;

        BinaryFileSink file_sink(filepath, session, chunk_cap);

#ifdef QRSDP_KAFKA_ENABLED
        std::unique_ptr<KafkaSink> kafka_sink;
        MultiplexSink mux_sink;
        mux_sink.addSink(&file_sink);

        if (!config.kafka_brokers.empty()) {
            kafka_sink = std::make_unique<KafkaSink>(
                config.kafka_brokers, config.kafka_topic, symbol);
            mux_sink.addSink(kafka_sink.get());
        }

        IEventSink& sink = config.kafka_brokers.empty()
            ? static_cast<IEventSink&>(file_sink)
            : static_cast<IEventSink&>(mux_sink);
#else
        IEventSink& sink = file_sink;
#endif

        if (config.realtime) {
            std::printf("[%s] %s session starting (speed=%.0fx)\n",
                        symbol.c_str(), date_str.c_str(), config.speed);
        }

        auto t0 = std::chrono::steady_clock::now();

        producer.startSession(session);
        auto wall_start = std::chrono::steady_clock::now();

        while (!g_shutdown_requested.load(std::memory_order_relaxed)
               && producer.stepOneEvent(sink))
        {
            if (config.realtime && config.speed > 0.0) {
                double sim_elapsed = producer.currentTime();
                double wall_target = sim_elapsed / config.speed;
                auto wall_elapsed = std::chrono::steady_clock::now() - wall_start;
                double wall_secs = std::chrono::duration<double>(wall_elapsed).count();
                if (wall_target > wall_secs) {
                    std::this_thread::sleep_for(
                        std::chrono::duration<double>(wall_target - wall_secs));
                }
            }
        }

        const int32_t close_ticks =
            (book.bestBid().price_ticks + book.bestAsk().price_ticks) / 2;
        const uint64_t events_written = producer.eventsWrittenThisSession();

        auto t1 = std::chrono::steady_clock::now();
        sink.close();

        const double write_secs = std::chrono::duration<double>(t1 - t0).count();
        const uint64_t file_size = static_cast<uint64_t>(fs::file_size(filepath));

        double read_secs = 0.0;
        if (!config.realtime) {
            auto r0 = std::chrono::steady_clock::now();
            {
                EventLogReader reader(filepath);
                auto records = reader.readAll();
                if (records.size() != events_written) {
                    throw std::runtime_error("read-back count mismatch");
                }
            }
            auto r1 = std::chrono::steady_clock::now();
            read_secs = std::chrono::duration<double>(r1 - r0).count();
        }

        DayResult dr{};
        dr.symbol = symbol;
        dr.date = date_str;
        dr.filename = filename;
        dr.seed = day_seed;
        dr.open_ticks = next_p0;
        dr.close_ticks = close_ticks;
        dr.events_written = events_written;
        dr.chunks_written = file_sink.chunksWritten();
        dr.file_size_bytes = file_size;
        dr.write_seconds = write_secs;
        dr.read_seconds = read_secs;

        days.push_back(dr);

        if (config.realtime) {
            std::printf("[%s] %s complete: %llu events in %.1fs\n",
                        symbol.c_str(), date_str.c_str(),
                        (unsigned long long)events_written, write_secs);
        }

        next_p0 = close_ticks;
        current_date = nextBusinessDay(current_date);
    }

    return days;
}

// ---------------------------------------------------------------------------
// Main run loop
// ---------------------------------------------------------------------------

RunResult SessionRunner::run(const RunConfig& config) {
    namespace fs = std::filesystem;
    fs::create_directories(config.output_dir);

    RunResult result{};
    result.total_events = 0;

    auto run_start = std::chrono::steady_clock::now();

    if (config.securities.empty()) {
        // Single-security backward-compatible path
        auto days = runSecurityDays(
            config, "",
            config.p0_ticks, config.tick_size, config.levels_per_side,
            config.initial_spread_ticks, config.initial_depth,
            config.intensity_params, config.queue_reactive,
            config.model_type, 0);
        for (auto& d : days) {
            result.total_events += d.events_written;
            result.days.push_back(std::move(d));
        }
    } else {
        // Multi-security: one thread per security
        std::vector<std::thread> threads;
        std::vector<std::vector<DayResult>> per_sec_results(config.securities.size());
        std::vector<std::string> errors(config.securities.size());

        for (size_t si = 0; si < config.securities.size(); ++si) {
            threads.emplace_back([&, si]() {
                try {
                    const auto& sec = config.securities[si];
                    per_sec_results[si] = runSecurityDays(
                        config, sec.symbol,
                        sec.p0_ticks, sec.tick_size, sec.levels_per_side,
                        sec.initial_spread_ticks, sec.initial_depth,
                        sec.intensity_params, sec.queue_reactive,
                        sec.model_type, si * kSeedStride);
                } catch (const std::exception& e) {
                    errors[si] = e.what();
                }
            });
        }

        for (auto& t : threads) t.join();

        for (size_t si = 0; si < config.securities.size(); ++si) {
            if (!errors[si].empty()) {
                throw std::runtime_error("security " + config.securities[si].symbol +
                                         " failed: " + errors[si]);
            }
            for (auto& d : per_sec_results[si]) {
                result.total_events += d.events_written;
                result.days.push_back(std::move(d));
            }
        }
    }

    auto run_end = std::chrono::steady_clock::now();
    result.total_elapsed_seconds = std::chrono::duration<double>(run_end - run_start).count();

    writeManifest(config, result);
    return result;
}

}  // namespace qrsdp
