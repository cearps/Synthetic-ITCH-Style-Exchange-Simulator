#pragma once

#include "io/event_log_format.h"

#include <cstdio>
#include <string>
#include <vector>

namespace qrsdp {

/// Reads .qrsdp binary event log files produced by BinaryFileSink.
/// Supports sequential iteration, random-access by chunk index,
/// and timestamp-range queries via the chunk index.
class EventLogReader {
public:
    /// Opens the file and parses the file header.
    /// Throws std::runtime_error if the file cannot be opened or the header is invalid.
    explicit EventLogReader(const std::string& path);

    ~EventLogReader();

    EventLogReader(const EventLogReader&) = delete;
    EventLogReader& operator=(const EventLogReader&) = delete;

    /// Returns the parsed file header.
    const FileHeader& header() const { return header_; }

    /// Returns the number of chunks in the file.
    uint32_t chunkCount() const { return static_cast<uint32_t>(index_.size()); }

    /// Returns the total number of records across all chunks.
    uint64_t totalRecords() const;

    /// Read and decompress a single chunk by index (0-based).
    /// Throws std::out_of_range if idx >= chunkCount().
    std::vector<DiskEventRecord> readChunk(uint32_t idx) const;

    /// Read and decompress all chunks whose timestamp ranges overlap [ts_start, ts_end].
    /// Records outside the range may be included (filtering is at chunk granularity).
    std::vector<DiskEventRecord> readRange(uint64_t ts_start, uint64_t ts_end) const;

    /// Read and decompress all records sequentially. Convenience method for small files.
    std::vector<DiskEventRecord> readAll() const;

    /// Returns the chunk index entries (useful for inspection/debugging).
    const std::vector<IndexEntry>& index() const { return index_; }

private:
    /// Build the chunk index. Prefers the footer index if HAS_INDEX is set,
    /// otherwise scans chunk headers sequentially from offset 64.
    void buildIndex();

    /// Build index by reading the footer (fast path).
    void buildIndexFromFooter();

    /// Build index by scanning chunk headers from the start (slow path / crash recovery).
    void buildIndexByScanning();

    /// Read and decompress the chunk at the given file offset.
    std::vector<DiskEventRecord> decompressChunkAt(uint64_t file_offset) const;

    std::FILE* file_ = nullptr;
    FileHeader header_{};
    std::vector<IndexEntry> index_;
};

}  // namespace qrsdp
