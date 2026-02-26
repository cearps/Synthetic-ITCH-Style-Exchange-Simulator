#include <gtest/gtest.h>
#include "io/binary_file_sink.h"
#include "io/event_log_format.h"
#include "core/records.h"

#include <lz4.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace qrsdp {
namespace test {

static TradingSession makeTestSession() {
    TradingSession s{};
    s.seed                 = 42;
    s.p0_ticks             = 100000;
    s.session_seconds      = 30;
    s.levels_per_side      = 10;
    s.tick_size            = 100;
    s.initial_spread_ticks = 2;
    s.initial_depth        = 50;
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
    r.flags       = kFlagShiftUp; // producer-side flag, should not appear on disk
    return r;
}

class BinaryFileSinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = testing::TempDir() + "test_sink_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)) + ".qrsdp";
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::string path_;
};

// --- Header Tests ---

TEST_F(BinaryFileSinkTest, FileHeaderMagicAndVersion) {
    auto session = makeTestSession();
    {
        BinaryFileSink sink(path_, session);
        sink.close();
    }

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);

    FileHeader hdr{};
    ASSERT_EQ(std::fread(&hdr, sizeof(hdr), 1, f), 1u);
    std::fclose(f);

    EXPECT_TRUE(validateMagic(hdr));
    EXPECT_EQ(hdr.version_major, kLogVersionMajor);
    EXPECT_EQ(hdr.version_minor, kLogVersionMinor);
    EXPECT_EQ(hdr.record_size, sizeof(DiskEventRecord));
}

TEST_F(BinaryFileSinkTest, FileHeaderSessionMetadata) {
    auto session = makeTestSession();
    {
        BinaryFileSink sink(path_, session, 128);
        sink.close();
    }

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);

    FileHeader hdr{};
    ASSERT_EQ(std::fread(&hdr, sizeof(hdr), 1, f), 1u);
    std::fclose(f);

    EXPECT_EQ(hdr.seed, 42u);
    EXPECT_EQ(hdr.p0_ticks, 100000);
    EXPECT_EQ(hdr.tick_size, 100u);
    EXPECT_EQ(hdr.session_seconds, 30u);
    EXPECT_EQ(hdr.levels_per_side, 10u);
    EXPECT_EQ(hdr.initial_spread_ticks, 2u);
    EXPECT_EQ(hdr.initial_depth, 50u);
    EXPECT_EQ(hdr.chunk_capacity, 128u);
    EXPECT_EQ(hdr.market_open_ns, 0u);
}

// --- Empty file ---

TEST_F(BinaryFileSinkTest, EmptyFileNoChunks) {
    auto session = makeTestSession();
    {
        BinaryFileSink sink(path_, session);
        EXPECT_EQ(sink.recordsWritten(), 0u);
        EXPECT_EQ(sink.chunksWritten(), 0u);
        sink.close();
    }

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 0, SEEK_END);
    const long file_size = std::ftell(f);
    std::fclose(f);

    EXPECT_EQ(file_size, static_cast<long>(sizeof(FileHeader)));
}

// --- Round-trip: write, decompress, compare ---

TEST_F(BinaryFileSinkTest, RoundTripSingleChunk) {
    auto session = makeTestSession();
    constexpr uint32_t kChunkCap = 64;
    constexpr int N = 10;

    std::vector<EventRecord> originals;
    {
        BinaryFileSink sink(path_, session, kChunkCap);
        for (int i = 0; i < N; ++i) {
            auto rec = makeRecord(
                static_cast<uint64_t>(i) * 1000000,
                static_cast<uint8_t>(i % 6),
                static_cast<uint8_t>(i % 2),
                100000 + i,
                1,
                static_cast<uint64_t>(i + 1));
            originals.push_back(rec);
            sink.append(rec);
        }
        EXPECT_EQ(sink.recordsWritten(), 0u);  // not flushed yet (10 < 64)
        sink.close();
        // After close, the partial chunk is flushed
    }

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);

    // Skip file header
    std::fseek(f, sizeof(FileHeader), SEEK_SET);

    // Read chunk header
    ChunkHeader chdr{};
    ASSERT_EQ(std::fread(&chdr, sizeof(chdr), 1, f), 1u);
    EXPECT_EQ(chdr.record_count, static_cast<uint32_t>(N));
    EXPECT_EQ(chdr.uncompressed_size, static_cast<uint32_t>(N * sizeof(DiskEventRecord)));
    EXPECT_EQ(chdr.first_ts_ns, 0u);
    EXPECT_EQ(chdr.last_ts_ns, static_cast<uint64_t>(N - 1) * 1000000);

    // Read compressed payload
    std::vector<char> compressed(chdr.compressed_size);
    ASSERT_EQ(std::fread(compressed.data(), 1, chdr.compressed_size, f), chdr.compressed_size);

    // Decompress
    std::vector<char> decompressed(chdr.uncompressed_size);
    int result = LZ4_decompress_safe(compressed.data(), decompressed.data(),
                                     static_cast<int>(chdr.compressed_size),
                                     static_cast<int>(chdr.uncompressed_size));
    ASSERT_EQ(result, static_cast<int>(chdr.uncompressed_size));

    // Compare records (disk records should match originals minus flags)
    auto* disk_records = reinterpret_cast<const DiskEventRecord*>(decompressed.data());
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(disk_records[i].ts_ns,       originals[i].ts_ns)       << "record " << i;
        EXPECT_EQ(disk_records[i].type,        originals[i].type)        << "record " << i;
        EXPECT_EQ(disk_records[i].side,        originals[i].side)        << "record " << i;
        EXPECT_EQ(disk_records[i].price_ticks, originals[i].price_ticks) << "record " << i;
        EXPECT_EQ(disk_records[i].qty,         originals[i].qty)         << "record " << i;
        EXPECT_EQ(disk_records[i].order_id,    originals[i].order_id)    << "record " << i;
    }

    std::fclose(f);
}

TEST_F(BinaryFileSinkTest, RoundTripMultipleChunks) {
    auto session = makeTestSession();
    constexpr uint32_t kChunkCap = 8;
    constexpr int N = 25;  // 3 full chunks of 8, 1 partial of 1

    {
        BinaryFileSink sink(path_, session, kChunkCap);
        for (int i = 0; i < N; ++i) {
            sink.append(makeRecord(
                static_cast<uint64_t>(i) * 500000,
                static_cast<uint8_t>(i % 6),
                static_cast<uint8_t>(i % 2),
                100000 + i, 1, static_cast<uint64_t>(i + 1)));
        }
        EXPECT_EQ(sink.chunksWritten(), 3u);  // 3 full chunks written during append
        sink.close();
    }

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    std::fseek(f, sizeof(FileHeader), SEEK_SET);

    uint32_t total_records = 0;
    uint64_t prev_last_ts = 0;

    for (int chunk = 0; chunk < 4; ++chunk) {
        ChunkHeader chdr{};
        size_t read = std::fread(&chdr, sizeof(chdr), 1, f);
        ASSERT_EQ(read, 1u) << "failed reading chunk " << chunk;

        if (chunk == 3) {
            EXPECT_EQ(chdr.record_count, 1u);  // partial chunk
        } else {
            EXPECT_EQ(chdr.record_count, kChunkCap);
        }

        EXPECT_GE(chdr.first_ts_ns, prev_last_ts) << "chunk " << chunk << " not in order";
        EXPECT_LE(chdr.first_ts_ns, chdr.last_ts_ns);
        prev_last_ts = chdr.last_ts_ns;

        total_records += chdr.record_count;

        // Skip past compressed payload
        std::fseek(f, static_cast<long>(chdr.compressed_size), SEEK_CUR);
    }

    EXPECT_EQ(total_records, static_cast<uint32_t>(N));
    std::fclose(f);
}

// --- Flags are stripped ---

TEST_F(BinaryFileSinkTest, FlagsNotWrittenToDisk) {
    auto session = makeTestSession();
    {
        BinaryFileSink sink(path_, session, 64);
        auto rec = makeRecord(1000, 4, 1, 100001, 1, 1);
        rec.flags = kFlagShiftUp | kFlagReinit;
        sink.append(rec);
        sink.close();
    }

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    std::fseek(f, sizeof(FileHeader), SEEK_SET);

    ChunkHeader chdr{};
    ASSERT_EQ(std::fread(&chdr, sizeof(chdr), 1, f), 1u);

    std::vector<char> compressed(chdr.compressed_size);
    ASSERT_EQ(std::fread(compressed.data(), 1, chdr.compressed_size, f), chdr.compressed_size);
    std::fclose(f);

    std::vector<char> decompressed(chdr.uncompressed_size);
    LZ4_decompress_safe(compressed.data(), decompressed.data(),
                        static_cast<int>(chdr.compressed_size),
                        static_cast<int>(chdr.uncompressed_size));

    // DiskEventRecord is 26 bytes with no flags field.
    // Verify the raw bytes contain exactly one 26-byte record, not 30 bytes.
    EXPECT_EQ(chdr.uncompressed_size, sizeof(DiskEventRecord));
    EXPECT_EQ(chdr.record_count, 1u);
}

// --- Chunk Index Footer ---

TEST_F(BinaryFileSinkTest, IndexFooterPresent) {
    auto session = makeTestSession();
    constexpr uint32_t kChunkCap = 4;
    constexpr int N = 10;  // 2 full + 1 partial = 3 chunks

    {
        BinaryFileSink sink(path_, session, kChunkCap);
        for (int i = 0; i < N; ++i) {
            sink.append(makeRecord(
                static_cast<uint64_t>(i) * 100000, 0, 0,
                100000, 1, static_cast<uint64_t>(i + 1)));
        }
        sink.close();
    }

    // Read file header — HAS_INDEX should be set
    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);

    FileHeader hdr{};
    ASSERT_EQ(std::fread(&hdr, sizeof(hdr), 1, f), 1u);
    EXPECT_NE(hdr.header_flags & kHeaderFlagHasIndex, 0u);

    // Read index tail from end of file
    std::fseek(f, -static_cast<long>(sizeof(IndexTail)), SEEK_END);
    IndexTail tail{};
    ASSERT_EQ(std::fread(&tail, sizeof(tail), 1, f), 1u);

    EXPECT_EQ(std::memcmp(tail.index_magic, kIndexMagic, 4), 0);
    EXPECT_EQ(tail.chunk_count, 3u);

    // Read all index entries
    std::fseek(f, static_cast<long>(tail.index_start_offset), SEEK_SET);
    std::vector<IndexEntry> entries(tail.chunk_count);
    ASSERT_EQ(std::fread(entries.data(), sizeof(IndexEntry), tail.chunk_count, f),
              tail.chunk_count);

    // Verify index entries are in order and point to valid offsets
    for (uint32_t i = 0; i < tail.chunk_count; ++i) {
        EXPECT_GE(entries[i].file_offset, sizeof(FileHeader)) << "entry " << i;
        EXPECT_LE(entries[i].first_ts_ns, entries[i].last_ts_ns) << "entry " << i;
        if (i > 0) {
            EXPECT_GT(entries[i].file_offset, entries[i - 1].file_offset) << "entry " << i;
            EXPECT_GE(entries[i].first_ts_ns, entries[i - 1].last_ts_ns) << "entry " << i;
        }
    }

    // Verify record counts sum to N
    uint32_t total = 0;
    for (auto& e : entries) total += e.record_count;
    EXPECT_EQ(total, static_cast<uint32_t>(N));

    std::fclose(f);
}

// --- Index random access: seek to chunk via index, decompress, verify first record ---

TEST_F(BinaryFileSinkTest, IndexRandomAccess) {
    auto session = makeTestSession();
    constexpr uint32_t kChunkCap = 4;
    constexpr int N = 12;  // 3 full chunks

    {
        BinaryFileSink sink(path_, session, kChunkCap);
        for (int i = 0; i < N; ++i) {
            sink.append(makeRecord(
                static_cast<uint64_t>(i) * 1000000,
                static_cast<uint8_t>(i % 6),
                static_cast<uint8_t>(i % 2),
                100000 + i, 1, static_cast<uint64_t>(i + 1)));
        }
        sink.close();
    }

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);

    // Read index tail
    std::fseek(f, -static_cast<long>(sizeof(IndexTail)), SEEK_END);
    IndexTail tail{};
    ASSERT_EQ(std::fread(&tail, sizeof(tail), 1, f), 1u);
    ASSERT_EQ(tail.chunk_count, 3u);

    // Read index entries
    std::fseek(f, static_cast<long>(tail.index_start_offset), SEEK_SET);
    std::vector<IndexEntry> entries(tail.chunk_count);
    ASSERT_EQ(std::fread(entries.data(), sizeof(IndexEntry), 3, f), 3u);

    // Random-access chunk 2 (last chunk, records 8-11)
    std::fseek(f, static_cast<long>(entries[2].file_offset), SEEK_SET);
    ChunkHeader chdr{};
    ASSERT_EQ(std::fread(&chdr, sizeof(chdr), 1, f), 1u);
    EXPECT_EQ(chdr.record_count, kChunkCap);

    std::vector<char> compressed(chdr.compressed_size);
    ASSERT_EQ(std::fread(compressed.data(), 1, chdr.compressed_size, f), chdr.compressed_size);

    std::vector<char> decompressed(chdr.uncompressed_size);
    int dec = LZ4_decompress_safe(compressed.data(), decompressed.data(),
                                  static_cast<int>(chdr.compressed_size),
                                  static_cast<int>(chdr.uncompressed_size));
    ASSERT_EQ(dec, static_cast<int>(chdr.uncompressed_size));

    auto* records = reinterpret_cast<const DiskEventRecord*>(decompressed.data());
    EXPECT_EQ(records[0].ts_ns, 8u * 1000000);
    EXPECT_EQ(records[0].order_id, 9u);

    std::fclose(f);
}

// --- Destructor auto-closes ---

TEST_F(BinaryFileSinkTest, DestructorFlushesAndCloses) {
    auto session = makeTestSession();
    {
        BinaryFileSink sink(path_, session, 64);
        for (int i = 0; i < 5; ++i) {
            sink.append(makeRecord(
                static_cast<uint64_t>(i) * 100, 0, 0,
                100000, 1, static_cast<uint64_t>(i + 1)));
        }
        // No explicit close — destructor should handle it
    }

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    ASSERT_NE(f, nullptr);

    FileHeader hdr{};
    ASSERT_EQ(std::fread(&hdr, sizeof(hdr), 1, f), 1u);
    EXPECT_TRUE(validateMagic(hdr));
    EXPECT_NE(hdr.header_flags & kHeaderFlagHasIndex, 0u);

    std::fseek(f, sizeof(FileHeader), SEEK_SET);
    ChunkHeader chdr{};
    ASSERT_EQ(std::fread(&chdr, sizeof(chdr), 1, f), 1u);
    EXPECT_EQ(chdr.record_count, 5u);

    std::fclose(f);
}

}  // namespace test
}  // namespace qrsdp
