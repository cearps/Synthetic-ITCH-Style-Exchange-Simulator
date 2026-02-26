#include <gtest/gtest.h>
#include "io/binary_file_sink.h"
#include "io/event_log_reader.h"
#include "io/event_log_format.h"
#include "core/records.h"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
static int truncate(const char* path, long length) {
    int fd;
    if (_sopen_s(&fd, path, _O_RDWR | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0)
        return -1;
    int rc = _chsize_s(fd, length);
    _close(fd);
    return rc;
}
#else
#include <unistd.h>
#endif

namespace qrsdp {
namespace test {

static TradingSession makeTestSession() {
    TradingSession s{};
    s.seed                 = 99;
    s.p0_ticks             = 50000;
    s.session_seconds      = 60;
    s.levels_per_side      = 8;
    s.tick_size            = 100;
    s.initial_spread_ticks = 2;
    s.initial_depth        = 20;
    return s;
}

static EventRecord makeRecord(uint64_t ts, uint8_t type, uint8_t side,
                               int32_t price, uint32_t qty, uint64_t oid) {
    EventRecord r{};
    r.ts_ns       = ts;
    r.type        = type;
    r.side        = side;
    r.price_ticks = price;
    r.qty         = qty;
    r.order_id    = oid;
    r.flags       = 0;
    return r;
}

/// Helper: write N records via BinaryFileSink and return them for comparison.
static std::vector<EventRecord> writeTestFile(const std::string& path, int n,
                                               uint32_t chunk_cap = 8) {
    auto session = makeTestSession();
    std::vector<EventRecord> records;
    BinaryFileSink sink(path, session, chunk_cap);
    for (int i = 0; i < n; ++i) {
        auto rec = makeRecord(
            static_cast<uint64_t>(i) * 1000000,
            static_cast<uint8_t>(i % 6),
            static_cast<uint8_t>(i % 2),
            50000 + (i % 20),
            1,
            static_cast<uint64_t>(i + 1));
        records.push_back(rec);
        sink.append(rec);
    }
    sink.close();
    return records;
}

class EventLogReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = testing::TempDir() + "test_reader_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)) + ".qrsdp";
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::string path_;
};

// --- Constructor / Header ---

TEST_F(EventLogReaderTest, ParsesHeaderCorrectly) {
    writeTestFile(path_, 10);
    EventLogReader reader(path_);

    const auto& hdr = reader.header();
    EXPECT_TRUE(validateMagic(hdr));
    EXPECT_EQ(hdr.version_major, kLogVersionMajor);
    EXPECT_EQ(hdr.record_size, sizeof(DiskEventRecord));
    EXPECT_EQ(hdr.seed, 99u);
    EXPECT_EQ(hdr.p0_ticks, 50000);
    EXPECT_EQ(hdr.tick_size, 100u);
    EXPECT_EQ(hdr.session_seconds, 60u);
}

TEST_F(EventLogReaderTest, ThrowsOnBadFile) {
    EXPECT_THROW(EventLogReader("/nonexistent/path.qrsdp"), std::runtime_error);
}

TEST_F(EventLogReaderTest, ThrowsOnBadMagic) {
    // Write a file with garbage content
    std::FILE* f = std::fopen(path_.c_str(), "wb");
    const char garbage[64] = {0};
    std::fwrite(garbage, 1, 64, f);
    std::fclose(f);

    EXPECT_THROW({ EventLogReader r(path_); }, std::runtime_error);
}

// --- Chunk count and total records ---

TEST_F(EventLogReaderTest, EmptyFileHasNoChunks) {
    writeTestFile(path_, 0);
    EventLogReader reader(path_);

    EXPECT_EQ(reader.chunkCount(), 0u);
    EXPECT_EQ(reader.totalRecords(), 0u);
}

TEST_F(EventLogReaderTest, ChunkCountMatchesWriter) {
    writeTestFile(path_, 25, 8);  // 3 full + 1 partial = 4 chunks
    EventLogReader reader(path_);

    EXPECT_EQ(reader.chunkCount(), 4u);
    EXPECT_EQ(reader.totalRecords(), 25u);
}

// --- readChunk ---

TEST_F(EventLogReaderTest, ReadChunkZero) {
    auto originals = writeTestFile(path_, 20, 8);
    EventLogReader reader(path_);

    auto chunk = reader.readChunk(0);
    ASSERT_EQ(chunk.size(), 8u);
    for (size_t i = 0; i < chunk.size(); ++i) {
        EXPECT_EQ(chunk[i].ts_ns, originals[i].ts_ns)          << "record " << i;
        EXPECT_EQ(chunk[i].type, originals[i].type)             << "record " << i;
        EXPECT_EQ(chunk[i].side, originals[i].side)             << "record " << i;
        EXPECT_EQ(chunk[i].price_ticks, originals[i].price_ticks) << "record " << i;
        EXPECT_EQ(chunk[i].qty, originals[i].qty)               << "record " << i;
        EXPECT_EQ(chunk[i].order_id, originals[i].order_id)     << "record " << i;
    }
}

TEST_F(EventLogReaderTest, ReadLastPartialChunk) {
    auto originals = writeTestFile(path_, 25, 8);  // chunk 3 has 1 record
    EventLogReader reader(path_);

    auto chunk = reader.readChunk(3);
    ASSERT_EQ(chunk.size(), 1u);
    EXPECT_EQ(chunk[0].ts_ns, originals[24].ts_ns);
    EXPECT_EQ(chunk[0].order_id, originals[24].order_id);
}

TEST_F(EventLogReaderTest, ReadChunkOutOfRangeThrows) {
    writeTestFile(path_, 10, 8);
    EventLogReader reader(path_);

    EXPECT_THROW(reader.readChunk(99), std::out_of_range);
}

// --- readAll ---

TEST_F(EventLogReaderTest, ReadAllMatchesWrittenRecords) {
    auto originals = writeTestFile(path_, 50, 8);
    EventLogReader reader(path_);

    auto all = reader.readAll();
    ASSERT_EQ(all.size(), 50u);
    for (size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(all[i].ts_ns, originals[i].ts_ns)          << "record " << i;
        EXPECT_EQ(all[i].type, originals[i].type)             << "record " << i;
        EXPECT_EQ(all[i].order_id, originals[i].order_id)     << "record " << i;
    }
}

TEST_F(EventLogReaderTest, ReadAllTimestampsMonotonicallyIncreasing) {
    writeTestFile(path_, 100, 16);
    EventLogReader reader(path_);

    auto all = reader.readAll();
    for (size_t i = 1; i < all.size(); ++i) {
        EXPECT_GE(all[i].ts_ns, all[i - 1].ts_ns) << "record " << i;
    }
}

// --- readRange ---

TEST_F(EventLogReaderTest, ReadRangeReturnsOverlappingChunks) {
    // 40 records, ts_ns = 0, 1M, 2M, ..., 39M. Chunks of 8.
    // Chunk 0: [0, 7M], Chunk 1: [8M, 15M], Chunk 2: [16M, 23M], ...
    // Query [10M, 20M] should return at least chunks 1 and 2.
    auto originals = writeTestFile(path_, 40, 8);
    EventLogReader reader(path_);

    auto range = reader.readRange(10'000'000, 20'000'000);

    // Must contain records from chunks overlapping the range.
    // Chunk 1 starts at 8M, ends at 15M -> overlaps.
    // Chunk 2 starts at 16M, ends at 23M -> overlaps.
    // That's 16 records minimum (2 full chunks).
    EXPECT_GE(range.size(), 16u);

    // All returned records should come from chunks that overlap the query range.
    // Since filtering is at chunk granularity, some records may be outside [10M, 20M],
    // but none should be from chunks entirely outside the range.
    for (const auto& r : range) {
        // Records are from chunks overlapping [10M, 20M], so they should be
        // in the ts range of those chunks (roughly [8M, 23M] for chunks 1-2).
        EXPECT_GE(r.ts_ns, 8'000'000u);
        EXPECT_LE(r.ts_ns, 23'000'000u);
    }
}

TEST_F(EventLogReaderTest, ReadRangeEmptyWhenNoOverlap) {
    writeTestFile(path_, 20, 8);  // ts_ns up to 19M
    EventLogReader reader(path_);

    auto range = reader.readRange(100'000'000, 200'000'000);
    EXPECT_EQ(range.size(), 0u);
}

// --- Index ---

TEST_F(EventLogReaderTest, IndexEntriesMatchChunks) {
    writeTestFile(path_, 32, 8);  // exactly 4 chunks
    EventLogReader reader(path_);

    const auto& idx = reader.index();
    ASSERT_EQ(idx.size(), 4u);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_EQ(idx[i].record_count, 8u) << "chunk " << i;
        EXPECT_LE(idx[i].first_ts_ns, idx[i].last_ts_ns) << "chunk " << i;
        if (i > 0) {
            EXPECT_GE(idx[i].first_ts_ns, idx[i - 1].last_ts_ns) << "chunk " << i;
        }
    }
}

// --- Scan-based index (no footer) ---
// This tests that the reader can build an index by scanning chunk headers
// when the HAS_INDEX flag is missing (e.g. crash recovery).

TEST_F(EventLogReaderTest, WorksWithoutIndexFooter) {
    // Write a file, then truncate the index footer to simulate a crash.
    auto originals = writeTestFile(path_, 16, 8);

    // Read the file header to find the HAS_INDEX flag, then clear it
    // and truncate the file to remove the footer.
    std::FILE* f = std::fopen(path_.c_str(), "r+b");
    ASSERT_NE(f, nullptr);

    FileHeader hdr{};
    std::fread(&hdr, sizeof(hdr), 1, f);
    ASSERT_NE(hdr.header_flags & kHeaderFlagHasIndex, 0u);

    // Find where the index starts (read tail from end)
    std::fseek(f, -static_cast<long>(sizeof(IndexTail)), SEEK_END);
    IndexTail tail{};
    std::fread(&tail, sizeof(tail), 1, f);
    long data_end = static_cast<long>(tail.index_start_offset);

    // Clear HAS_INDEX and truncate
    hdr.header_flags = 0;
    std::fseek(f, 0, SEEK_SET);
    std::fwrite(&hdr, sizeof(hdr), 1, f);
    std::fclose(f);

    // Truncate the file to remove the index footer
    truncate(path_.c_str(), data_end);

    // Now read — should still work via scanning
    EventLogReader reader(path_);
    EXPECT_EQ(reader.chunkCount(), 2u);
    EXPECT_EQ(reader.totalRecords(), 16u);

    auto all = reader.readAll();
    ASSERT_EQ(all.size(), 16u);
    for (size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(all[i].ts_ns, originals[i].ts_ns) << "record " << i;
    }
}

}  // namespace test
}  // namespace qrsdp
