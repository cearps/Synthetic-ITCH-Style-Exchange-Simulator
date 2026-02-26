#pragma once

#include <cstdint>
#include <cstring>

namespace qrsdp {

// --- Magic bytes and version ---
constexpr char     kLogMagic[8] = {'Q','R','S','D','P','L','O','G'};
constexpr uint16_t kLogVersionMajor = 1;
constexpr uint16_t kLogVersionMinor = 0;
constexpr uint32_t kDefaultChunkCapacity = 4096;

// --- Header flags ---
constexpr uint32_t kHeaderFlagHasIndex = 0x1;

// --- File Header (64 bytes) ---
#pragma pack(push, 1)
struct FileHeader {
    char     magic[8];
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t record_size;
    uint64_t seed;
    int32_t  p0_ticks;
    uint32_t tick_size;
    uint32_t session_seconds;
    uint32_t levels_per_side;
    uint32_t initial_spread_ticks;
    uint32_t initial_depth;
    uint32_t chunk_capacity;
    uint32_t header_flags;
    uint64_t market_open_ns;
};
#pragma pack(pop)
static_assert(sizeof(FileHeader) == 64, "FileHeader must be 64 bytes");

// --- On-disk EventRecord (26 bytes, no flags) ---
#pragma pack(push, 1)
struct DiskEventRecord {
    uint64_t ts_ns;
    uint8_t  type;
    uint8_t  side;
    int32_t  price_ticks;
    uint32_t qty;
    uint64_t order_id;
};
#pragma pack(pop)
static_assert(sizeof(DiskEventRecord) == 26, "DiskEventRecord must be 26 bytes");

// --- Chunk Header (32 bytes) ---
#pragma pack(push, 1)
struct ChunkHeader {
    uint32_t uncompressed_size;
    uint32_t compressed_size;
    uint32_t record_count;
    uint32_t chunk_flags;
    uint64_t first_ts_ns;
    uint64_t last_ts_ns;
};
#pragma pack(pop)
static_assert(sizeof(ChunkHeader) == 32, "ChunkHeader must be 32 bytes");

// --- Chunk Index Entry (32 bytes) ---
#pragma pack(push, 1)
struct IndexEntry {
    uint64_t file_offset;
    uint64_t first_ts_ns;
    uint64_t last_ts_ns;
    uint32_t record_count;
    uint32_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(IndexEntry) == 32, "IndexEntry must be 32 bytes");

// --- Index Tail (16 bytes) ---
constexpr char kIndexMagic[4] = {'Q','I','D','X'};

#pragma pack(push, 1)
struct IndexTail {
    uint32_t chunk_count;
    char     index_magic[4];
    uint64_t index_start_offset;
};
#pragma pack(pop)
static_assert(sizeof(IndexTail) == 16, "IndexTail must be 16 bytes");

inline bool validateMagic(const FileHeader& h) {
    return std::memcmp(h.magic, kLogMagic, 8) == 0;
}

}  // namespace qrsdp
