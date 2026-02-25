#include <gtest/gtest.h>
#include "producer/session_runner.h"
#include "io/event_log_format.h"
#include "io/event_log_reader.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace qrsdp {
namespace test {

namespace fs = std::filesystem;

static RunConfig makeTestConfig(const std::string& dir, uint32_t num_days, uint32_t seconds = 2) {
    RunConfig c{};
    c.run_id = "test_run";
    c.output_dir = dir;
    c.base_seed = 100;
    c.p0_ticks = 10000;
    c.session_seconds = seconds;
    c.levels_per_side = 5;
    c.tick_size = 100;
    c.initial_spread_ticks = 2;
    c.initial_depth = 5;
    c.intensity_params = {22.0, 0.2, 30.0, 1.0, 1.0, 0.5, 0.0};
    c.num_days = num_days;
    c.chunk_capacity = 64;
    c.start_date = "2026-01-02";
    return c;
}

class SessionRunnerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = testing::TempDir() + "session_runner_" +
               std::to_string(reinterpret_cast<uintptr_t>(this));
        fs::remove_all(dir_);
    }

    void TearDown() override {
        fs::remove_all(dir_);
    }

    std::string dir_;
};

// ----- Date helper tests -----

TEST(DateHelpers, ParseAndFormat) {
    Date d = parseDate("2026-03-15");
    EXPECT_EQ(d.year, 2026);
    EXPECT_EQ(d.month, 3);
    EXPECT_EQ(d.day, 15);
    EXPECT_EQ(formatDate(d), "2026-03-15");
}

TEST(DateHelpers, ParseInvalid) {
    EXPECT_THROW(parseDate("2026-3-15"), std::invalid_argument);
    EXPECT_THROW(parseDate("not-a-date"), std::invalid_argument);
}

TEST(DateHelpers, DayOfWeek) {
    // 2026-01-02 is a Friday
    EXPECT_EQ(dayOfWeek(parseDate("2026-01-02")), 5);
    // 2026-01-03 is Saturday
    EXPECT_EQ(dayOfWeek(parseDate("2026-01-03")), 6);
    // 2026-01-04 is Sunday
    EXPECT_EQ(dayOfWeek(parseDate("2026-01-04")), 0);
    // 2026-01-05 is Monday
    EXPECT_EQ(dayOfWeek(parseDate("2026-01-05")), 1);
}

TEST(DateHelpers, NextBusinessDayWeekday) {
    // Tuesday -> Wednesday
    Date d = nextBusinessDay(parseDate("2026-01-06"));
    EXPECT_EQ(formatDate(d), "2026-01-07");
}

TEST(DateHelpers, NextBusinessDayFriToMon) {
    // Friday -> Monday (skip Sat/Sun)
    Date d = nextBusinessDay(parseDate("2026-01-02"));
    EXPECT_EQ(formatDate(d), "2026-01-05");
}

TEST(DateHelpers, NextBusinessDaySatToMon) {
    Date d = nextBusinessDay(parseDate("2026-01-03"));
    EXPECT_EQ(formatDate(d), "2026-01-05");
}

TEST(DateHelpers, NextBusinessDayMonthEnd) {
    // 2026-01-30 is Friday -> Monday 2026-02-02
    Date d = nextBusinessDay(parseDate("2026-01-30"));
    EXPECT_EQ(formatDate(d), "2026-02-02");
}

// ----- SessionRunner tests -----

TEST_F(SessionRunnerTest, SingleDay) {
    RunConfig config = makeTestConfig(dir_, 1);
    SessionRunner runner;
    RunResult result = runner.run(config);

    ASSERT_EQ(result.days.size(), 1u);
    EXPECT_EQ(result.days[0].seed, 100u);
    EXPECT_EQ(result.days[0].date, "2026-01-02");
    EXPECT_EQ(result.days[0].open_ticks, 10000);
    EXPECT_GT(result.days[0].events_written, 0u);
    EXPECT_GT(result.days[0].file_size_bytes, 0u);
    EXPECT_GT(result.days[0].write_seconds, 0.0);
    EXPECT_GT(result.days[0].read_seconds, 0.0);
    EXPECT_EQ(result.total_events, result.days[0].events_written);

    EXPECT_TRUE(fs::exists(fs::path(dir_) / "2026-01-02.qrsdp"));
    EXPECT_TRUE(fs::exists(fs::path(dir_) / "manifest.json"));

    EventLogReader reader((fs::path(dir_) / "2026-01-02.qrsdp").string());
    const FileHeader& h = reader.header();
    EXPECT_EQ(h.seed, 100u);
    EXPECT_EQ(h.p0_ticks, 10000);
}

TEST_F(SessionRunnerTest, ContinuousChaining) {
    RunConfig config = makeTestConfig(dir_, 3);
    SessionRunner runner;
    RunResult result = runner.run(config);

    ASSERT_EQ(result.days.size(), 3u);

    // Day 1 opens at p0_ticks
    EXPECT_EQ(result.days[0].open_ticks, 10000);

    // Day 2 opens at day 1's close
    EXPECT_EQ(result.days[1].open_ticks, result.days[0].close_ticks);

    // Day 3 opens at day 2's close
    EXPECT_EQ(result.days[2].open_ticks, result.days[1].close_ticks);

    // Verify via file headers
    for (int i = 0; i < 3; ++i) {
        EventLogReader reader((fs::path(dir_) / result.days[i].filename).string());
        EXPECT_EQ(reader.header().p0_ticks, result.days[i].open_ticks);
    }
}

TEST_F(SessionRunnerTest, SeedSequential) {
    RunConfig config = makeTestConfig(dir_, 4);
    config.base_seed = 200;
    SessionRunner runner;
    RunResult result = runner.run(config);

    ASSERT_EQ(result.days.size(), 4u);
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_EQ(result.days[i].seed, 200u + i);
    }
}

TEST_F(SessionRunnerTest, BusinessDatesSkipWeekend) {
    // Start on Friday 2026-01-02: Fri, Mon, Tue, Wed, Thu
    RunConfig config = makeTestConfig(dir_, 5);
    SessionRunner runner;
    RunResult result = runner.run(config);

    ASSERT_EQ(result.days.size(), 5u);
    EXPECT_EQ(result.days[0].date, "2026-01-02");  // Fri
    EXPECT_EQ(result.days[1].date, "2026-01-05");  // Mon (skipped Sat/Sun)
    EXPECT_EQ(result.days[2].date, "2026-01-06");  // Tue
    EXPECT_EQ(result.days[3].date, "2026-01-07");  // Wed
    EXPECT_EQ(result.days[4].date, "2026-01-08");  // Thu

    for (const auto& d : result.days) {
        EXPECT_TRUE(fs::exists(fs::path(dir_) / d.filename));
    }
}

TEST_F(SessionRunnerTest, ManifestFormat) {
    RunConfig config = makeTestConfig(dir_, 2);
    SessionRunner runner;
    RunResult result = runner.run(config);

    std::ifstream ifs((fs::path(dir_) / "manifest.json").string());
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("\"format_version\": \"1.0\""), std::string::npos);
    EXPECT_NE(content.find("\"run_id\": \"test_run\""), std::string::npos);
    EXPECT_NE(content.find("\"producer\": \"qrsdp\""), std::string::npos);
    EXPECT_NE(content.find("\"base_seed\": 100"), std::string::npos);
    EXPECT_NE(content.find("\"seed_strategy\": \"sequential\""), std::string::npos);
    EXPECT_NE(content.find("\"tick_size\": 100"), std::string::npos);
    EXPECT_NE(content.find("\"p0_ticks\": 10000"), std::string::npos);
    EXPECT_NE(content.find("\"sessions\":"), std::string::npos);
    EXPECT_NE(content.find("\"2026-01-02\""), std::string::npos);
    EXPECT_NE(content.find("\"2026-01-05\""), std::string::npos);
    EXPECT_NE(content.find("\"seed\": 100"), std::string::npos);
    EXPECT_NE(content.find("\"seed\": 101"), std::string::npos);
    EXPECT_NE(content.find("2026-01-02.qrsdp"), std::string::npos);
    EXPECT_NE(content.find("2026-01-05.qrsdp"), std::string::npos);
}

TEST_F(SessionRunnerTest, PerformanceResultsDoc) {
    RunConfig config = makeTestConfig(dir_, 1);
    SessionRunner runner;
    RunResult result = runner.run(config);

    std::string perf_path = (fs::path(dir_) / "perf.md").string();
    SessionRunner::writePerformanceResults(config, result, perf_path);

    std::ifstream ifs(perf_path);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("# Performance Results"), std::string::npos);
    EXPECT_NE(content.find("Run Configuration"), std::string::npos);
    EXPECT_NE(content.find("Per-Day Results"), std::string::npos);
    EXPECT_NE(content.find("Aggregate"), std::string::npos);
    EXPECT_NE(content.find("Total events"), std::string::npos);
    EXPECT_NE(content.find("2026-01-02"), std::string::npos);
}

// ----- Multi-security tests -----

static RunConfig makeMultiSecConfig(const std::string& dir, uint32_t num_days) {
    RunConfig c = makeTestConfig(dir, num_days);
    SecurityConfig sec_a{};
    sec_a.symbol = "AAA";
    sec_a.p0_ticks = 10000;
    sec_a.tick_size = c.tick_size;
    sec_a.levels_per_side = c.levels_per_side;
    sec_a.initial_spread_ticks = c.initial_spread_ticks;
    sec_a.initial_depth = c.initial_depth;
    sec_a.intensity_params = c.intensity_params;
    sec_a.queue_reactive = c.queue_reactive;

    SecurityConfig sec_b{};
    sec_b.symbol = "BBB";
    sec_b.p0_ticks = 20000;
    sec_b.tick_size = c.tick_size;
    sec_b.levels_per_side = c.levels_per_side;
    sec_b.initial_spread_ticks = c.initial_spread_ticks;
    sec_b.initial_depth = c.initial_depth;
    sec_b.intensity_params = c.intensity_params;
    sec_b.queue_reactive = c.queue_reactive;

    c.securities = {sec_a, sec_b};
    return c;
}

TEST_F(SessionRunnerTest, MultiSecurityRun) {
    RunConfig config = makeMultiSecConfig(dir_, 2);
    SessionRunner runner;
    RunResult result = runner.run(config);

    // 2 securities * 2 days = 4 DayResult entries
    ASSERT_EQ(result.days.size(), 4u);

    // Separate subdirectories exist
    EXPECT_TRUE(fs::is_directory(fs::path(dir_) / "AAA"));
    EXPECT_TRUE(fs::is_directory(fs::path(dir_) / "BBB"));

    // All files present
    for (const auto& d : result.days) {
        EXPECT_TRUE(fs::exists(fs::path(dir_) / d.filename))
            << "Missing file: " << d.filename;
    }

    // Price chaining within each security
    int aaa_idx = -1;
    int bbb_idx = -1;
    for (size_t i = 0; i < result.days.size(); ++i) {
        if (result.days[i].symbol == "AAA") {
            if (aaa_idx >= 0) {
                EXPECT_EQ(result.days[i].open_ticks, result.days[aaa_idx].close_ticks);
            }
            aaa_idx = static_cast<int>(i);
        }
        if (result.days[i].symbol == "BBB") {
            if (bbb_idx >= 0) {
                EXPECT_EQ(result.days[i].open_ticks, result.days[bbb_idx].close_ticks);
            }
            bbb_idx = static_cast<int>(i);
        }
    }
}

TEST_F(SessionRunnerTest, MultiSecuritySeedIndependence) {
    RunConfig config = makeMultiSecConfig(dir_, 1);
    SessionRunner runner;
    RunResult result = runner.run(config);

    ASSERT_EQ(result.days.size(), 2u);
    // Different seeds for different securities (offset by kSeedStride=1024)
    EXPECT_NE(result.days[0].seed, result.days[1].seed);
}

TEST_F(SessionRunnerTest, MultiSecurityManifest) {
    RunConfig config = makeMultiSecConfig(dir_, 2);
    SessionRunner runner;
    runner.run(config);

    std::ifstream ifs((fs::path(dir_) / "manifest.json").string());
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("\"format_version\": \"1.1\""), std::string::npos);
    EXPECT_NE(content.find("\"securities\":"), std::string::npos);
    EXPECT_NE(content.find("\"symbol\": \"AAA\""), std::string::npos);
    EXPECT_NE(content.find("\"symbol\": \"BBB\""), std::string::npos);
    EXPECT_NE(content.find("\"p0_ticks\": 10000"), std::string::npos);
    EXPECT_NE(content.find("\"p0_ticks\": 20000"), std::string::npos);
    EXPECT_NE(content.find("AAA/2026-01-02.qrsdp"), std::string::npos);
    EXPECT_NE(content.find("BBB/2026-01-02.qrsdp"), std::string::npos);
    // v1.0 has top-level "sessions" without a "securities" wrapper.
    // In v1.1, "securities" must appear before any "sessions".
    auto sec_pos = content.find("\"securities\":");
    auto sess_pos = content.find("\"sessions\":");
    EXPECT_NE(sec_pos, std::string::npos);
    EXPECT_LT(sec_pos, sess_pos) << "securities must precede sessions in v1.1";
}

TEST_F(SessionRunnerTest, SingleSecurityBackwardCompat) {
    // Empty securities vector -> v1.0 format
    RunConfig config = makeTestConfig(dir_, 1);
    ASSERT_TRUE(config.securities.empty());

    SessionRunner runner;
    RunResult result = runner.run(config);

    // Files are in the root output_dir, not in subdirectories
    EXPECT_TRUE(fs::exists(fs::path(dir_) / "2026-01-02.qrsdp"));

    std::ifstream ifs((fs::path(dir_) / "manifest.json").string());
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("\"format_version\": \"1.0\""), std::string::npos);
    EXPECT_NE(content.find("\"sessions\":"), std::string::npos);
    EXPECT_EQ(content.find("\"securities\":"), std::string::npos)
        << "securities array should not exist in v1.0 manifest";
}

}  // namespace test
}  // namespace qrsdp
