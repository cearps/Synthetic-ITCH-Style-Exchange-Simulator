#include "io/event_log_reader.h"

#include <lz4.h>

#include <cstring>
#include <stdexcept>

namespace qrsdp {

EventLogReader::EventLogReader(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_)
        throw std::runtime_error("EventLogReader: cannot open " + path);

    if (std::fread(&header_, sizeof(header_), 1, file_) != 1)
        throw std::runtime_error("EventLogReader: cannot read header from " + path);

    if (!validateMagic(header_))
        throw std::runtime_error("EventLogReader: invalid magic in " + path);

    if (header_.version_major != kLogVersionMajor)
        throw std::runtime_error("EventLogReader: unsupported version in " + path);

    if (header_.record_size != sizeof(DiskEventRecord))
        throw std::runtime_error("EventLogReader: record size mismatch in " + path);

    buildIndex();
}

EventLogReader::~EventLogReader() {
    if (file_)
        std::fclose(file_);
}

uint64_t EventLogReader::totalRecords() const {
    uint64_t total = 0;
    for (const auto& entry : index_)
        total += entry.record_count;
    return total;
}

std::vector<DiskEventRecord> EventLogReader::readChunk(uint32_t idx) const {
    if (idx >= chunkCount())
        throw std::out_of_range("EventLogReader: chunk index out of range");
    return decompressChunkAt(index_[idx].file_offset);
}

std::vector<DiskEventRecord> EventLogReader::readRange(uint64_t ts_start, uint64_t ts_end) const {
    std::vector<DiskEventRecord> result;
    for (const auto& entry : index_) {
        if (entry.first_ts_ns <= ts_end && entry.last_ts_ns >= ts_start) {
            auto chunk = decompressChunkAt(entry.file_offset);
            result.insert(result.end(), chunk.begin(), chunk.end());
        }
    }
    return result;
}

std::vector<DiskEventRecord> EventLogReader::readAll() const {
    std::vector<DiskEventRecord> result;
    for (uint32_t i = 0; i < chunkCount(); ++i) {
        auto chunk = readChunk(i);
        result.insert(result.end(), chunk.begin(), chunk.end());
    }
    return result;
}

void EventLogReader::buildIndex() {
    if (header_.header_flags & kHeaderFlagHasIndex)
        buildIndexFromFooter();
    else
        buildIndexByScanning();
}

void EventLogReader::buildIndexFromFooter() {
    std::fseek(file_, -static_cast<long>(sizeof(IndexTail)), SEEK_END);

    IndexTail tail{};
    if (std::fread(&tail, sizeof(tail), 1, file_) != 1)
        throw std::runtime_error("EventLogReader: cannot read index tail");

    if (std::memcmp(tail.index_magic, kIndexMagic, 4) != 0)
        throw std::runtime_error("EventLogReader: invalid index magic");

    std::fseek(file_, static_cast<long>(tail.index_start_offset), SEEK_SET);
    index_.resize(tail.chunk_count);
    if (std::fread(index_.data(), sizeof(IndexEntry), tail.chunk_count, file_) != tail.chunk_count)
        throw std::runtime_error("EventLogReader: cannot read index entries");
}

void EventLogReader::buildIndexByScanning() {
    std::fseek(file_, sizeof(FileHeader), SEEK_SET);

    while (true) {
        long chunk_offset = std::ftell(file_);

        ChunkHeader chdr{};
        if (std::fread(&chdr, sizeof(chdr), 1, file_) != 1)
            break;

        IndexEntry entry{};
        entry.file_offset  = static_cast<uint64_t>(chunk_offset);
        entry.first_ts_ns  = chdr.first_ts_ns;
        entry.last_ts_ns   = chdr.last_ts_ns;
        entry.record_count = chdr.record_count;
        entry.reserved     = 0;
        index_.push_back(entry);

        std::fseek(file_, static_cast<long>(chdr.compressed_size), SEEK_CUR);
    }
}

std::vector<DiskEventRecord> EventLogReader::decompressChunkAt(uint64_t file_offset) const {
    std::fseek(file_, static_cast<long>(file_offset), SEEK_SET);

    ChunkHeader chdr{};
    if (std::fread(&chdr, sizeof(chdr), 1, file_) != 1)
        throw std::runtime_error("EventLogReader: cannot read chunk header");

    std::vector<char> compressed(chdr.compressed_size);
    if (std::fread(compressed.data(), 1, chdr.compressed_size, file_) != chdr.compressed_size)
        throw std::runtime_error("EventLogReader: cannot read compressed payload");

    std::vector<char> decompressed(chdr.uncompressed_size);
    int result = LZ4_decompress_safe(
        compressed.data(),
        decompressed.data(),
        static_cast<int>(chdr.compressed_size),
        static_cast<int>(chdr.uncompressed_size));

    if (result != static_cast<int>(chdr.uncompressed_size))
        throw std::runtime_error("EventLogReader: LZ4 decompression failed");

    std::vector<DiskEventRecord> records(chdr.record_count);
    std::memcpy(records.data(), decompressed.data(), chdr.uncompressed_size);
    return records;
}

}  // namespace qrsdp
