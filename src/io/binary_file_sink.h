#pragma once

#include "io/i_event_sink.h"
#include "io/event_log_format.h"
#include "core/records.h"

#include <cstdio>
#include <string>
#include <vector>

namespace qrsdp {

/// Disk-backed event sink: writes EventRecords to a .qrsdp binary file
/// with chunked LZ4 compression per the event-log-format spec.
class BinaryFileSink : public IEventSink {
public:
    /// Opens the file and writes the file header.
    /// chunk_capacity controls records per LZ4 chunk (default 4096).
    BinaryFileSink(const std::string& path,
                   const TradingSession& session,
                   uint32_t chunk_capacity = kDefaultChunkCapacity);

    ~BinaryFileSink() override;

    BinaryFileSink(const BinaryFileSink&) = delete;
    BinaryFileSink& operator=(const BinaryFileSink&) = delete;

    void append(const EventRecord& rec) override;

    /// Flush any buffered records as a partial chunk.
    void flush() override;

    /// Flush, write chunk index, finalise header flags, close file.
    /// Safe to call multiple times; subsequent calls are no-ops.
    void close() override;

    bool isOpen() const { return file_ != nullptr; }
    uint64_t recordsWritten() const { return total_records_; }
    uint32_t chunksWritten() const { return static_cast<uint32_t>(index_.size()); }

private:
    void writeFileHeader(const TradingSession& session);
    void flushChunk();
    void writeIndex();

    std::FILE* file_ = nullptr;
    uint32_t chunk_capacity_;
    uint64_t total_records_ = 0;

    std::vector<DiskEventRecord> buffer_;
    std::vector<IndexEntry> index_;
    std::vector<char> compress_buf_;
};

}  // namespace qrsdp
