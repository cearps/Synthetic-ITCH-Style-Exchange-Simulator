#pragma once

#include <cstdint>

#ifdef _MSC_VER
#include <cstdlib>
#endif

namespace qrsdp {
namespace itch {

inline uint16_t htobe16(uint16_t x) {
#ifdef _MSC_VER
    return _byteswap_ushort(x);
#else
    return __builtin_bswap16(x);
#endif
}

inline uint32_t htobe32(uint32_t x) {
#ifdef _MSC_VER
    return _byteswap_ulong(x);
#else
    return __builtin_bswap32(x);
#endif
}

inline uint64_t htobe64(uint64_t x) {
#ifdef _MSC_VER
    return _byteswap_uint64(x);
#else
    return __builtin_bswap64(x);
#endif
}

inline uint16_t betoh16(uint16_t x) { return htobe16(x); }
inline uint32_t betoh32(uint32_t x) { return htobe32(x); }
inline uint64_t betoh64(uint64_t x) { return htobe64(x); }

/// Store the lower 6 bytes of a 64-bit value in big-endian order.
/// ITCH timestamps are 6-byte big-endian nanosecond offsets from midnight.
inline void store48be(uint8_t* dst, uint64_t val) {
    uint64_t be = htobe64(val);
    auto* src = reinterpret_cast<const uint8_t*>(&be);
    dst[0] = src[2];
    dst[1] = src[3];
    dst[2] = src[4];
    dst[3] = src[5];
    dst[4] = src[6];
    dst[5] = src[7];
}

/// Read a 6-byte big-endian value into a uint64_t.
inline uint64_t load48be(const uint8_t* src) {
    uint64_t val = 0;
    auto* dst = reinterpret_cast<uint8_t*>(&val);
    dst[2] = src[0];
    dst[3] = src[1];
    dst[4] = src[2];
    dst[5] = src[3];
    dst[6] = src[4];
    dst[7] = src[5];
    return betoh64(val);
}

}  // namespace itch
}  // namespace qrsdp
