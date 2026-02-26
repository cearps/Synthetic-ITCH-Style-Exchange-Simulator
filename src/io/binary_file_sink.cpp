#include "io/binary_file_sink.h"

#include <lz4.h>

#include <cstring>
#include <stdexcept>

namespace qrsdp {

BinaryFileSink::BinaryFileSink(const std::string& path,
                               const TradingSession& session,
                               uint32_t chunk_capacity)
    : chunk_capacity_(chunk_capacity)
{
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_)
        throw std::runtime_error("BinaryFileSink: cannot open " + path);

    buffer_.reserve(chunk_capacity_);

    const int max_compressed = LZ4_compressBound(
        static_cast<int>(chunk_capacity_ * sizeof(DiskEventRecord)));
    compress_buf_.resize(static_cast<size_t>(max_compressed));

    writeFileHeader(session);
}

BinaryFileSink::~BinaryFileSink() {
    if (file_)
        close();
}

void BinaryFileSink::append(const EventRecord& rec) {
    DiskEventRecord disk;
    disk.ts_ns       = rec.ts_ns;
    disk.type        = rec.type;
    disk.side        = rec.side;
    disk.price_ticks = rec.price_ticks;
    disk.qty         = rec.qty;
    disk.order_id    = rec.order_id;
    buffer_.push_back(disk);

    if (buffer_.size() >= chunk_capacity_)
        flushChunk();
}

void BinaryFileSink::flush() {
    if (!buffer_.empty())
        flushChunk();
}

void BinaryFileSink::close() {
    if (!file_)
        return;

    flush();
    writeIndex();
    std::fclose(file_);
    file_ = nullptr;
}

// --- Private ---

void BinaryFileSink::writeFileHeader(const TradingSession& session) {
    FileHeader hdr{};
    std::memcpy(hdr.magic, kLogMagic, 8);
    hdr.version_major        = kLogVersionMajor;
    hdr.version_minor        = kLogVersionMinor;
    hdr.record_size          = static_cast<uint32_t>(sizeof(DiskEventRecord));
    hdr.seed                 = session.seed;
    hdr.p0_ticks             = session.p0_ticks;
    hdr.tick_size            = session.tick_size;
    hdr.session_seconds      = session.session_seconds;
    hdr.levels_per_side      = session.levels_per_side;
    hdr.initial_spread_ticks = session.initial_spread_ticks;
    hdr.initial_depth        = session.initial_depth;
    hdr.chunk_capacity       = chunk_capacity_;
    hdr.header_flags         = 0;
    hdr.market_open_ns       = static_cast<uint64_t>(session.market_open_seconds) * 1'000'000'000ULL;

    std::fwrite(&hdr, sizeof(hdr), 1, file_);
}

void BinaryFileSink::flushChunk() {
    if (buffer_.empty())
        return;

    const uint32_t record_count = static_cast<uint32_t>(buffer_.size());
    const auto raw_bytes = static_cast<int>(record_count * sizeof(DiskEventRecord));

    const int compressed_bytes = LZ4_compress_default(
        reinterpret_cast<const char*>(buffer_.data()),
        compress_buf_.data(),
        raw_bytes,
        static_cast<int>(compress_buf_.size()));

    if (compressed_bytes <= 0)
        throw std::runtime_error("BinaryFileSink: LZ4 compression failed");

    // Track chunk offset before writing
    IndexEntry entry{};
    entry.file_offset  = static_cast<uint64_t>(std::ftell(file_));
    entry.first_ts_ns  = buffer_.front().ts_ns;
    entry.last_ts_ns   = buffer_.back().ts_ns;
    entry.record_count = record_count;
    entry.reserved     = 0;
    index_.push_back(entry);

    ChunkHeader chdr{};
    chdr.uncompressed_size = static_cast<uint32_t>(raw_bytes);
    chdr.compressed_size   = static_cast<uint32_t>(compressed_bytes);
    chdr.record_count      = record_count;
    chdr.chunk_flags       = 0;
    chdr.first_ts_ns       = buffer_.front().ts_ns;
    chdr.last_ts_ns        = buffer_.back().ts_ns;

    std::fwrite(&chdr, sizeof(chdr), 1, file_);
    std::fwrite(compress_buf_.data(), 1, static_cast<size_t>(compressed_bytes), file_);

    total_records_ += record_count;
    buffer_.clear();
}

void BinaryFileSink::writeIndex() {
    if (index_.empty())
        return;

    const uint64_t index_start = static_cast<uint64_t>(std::ftell(file_));

    std::fwrite(index_.data(), sizeof(IndexEntry), index_.size(), file_);

    IndexTail tail{};
    tail.chunk_count        = static_cast<uint32_t>(index_.size());
    std::memcpy(tail.index_magic, kIndexMagic, 4);
    tail.index_start_offset = index_start;

    std::fwrite(&tail, sizeof(tail), 1, file_);

    // Seek back and set HAS_INDEX flag in file header
    std::fseek(file_, static_cast<long>(offsetof(FileHeader, header_flags)), SEEK_SET);
    uint32_t flags = kHeaderFlagHasIndex;
    std::fwrite(&flags, sizeof(flags), 1, file_);

    // Seek back to end
    std::fseek(file_, 0, SEEK_END);
}

}  // namespace qrsdp
