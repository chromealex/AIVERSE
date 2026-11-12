// IEEE float16 <-> float (probe storage). Round to nearest even, no NaN payloads.
#pragma once
#include <cstdint>
#include <cstring>

namespace aiverse::bake {
inline uint16_t floatToHalf(float f) {
    uint32_t x; std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t e = int32_t((x >> 23) & 0xff) - 127 + 15;
    uint32_t m = x & 0x7fffffu;
    if (((x >> 23) & 0xff) == 0xff) return uint16_t(sign | 0x7c00u | (m ? 0x200u : 0));
    if (e >= 31) return uint16_t(sign | 0x7c00u);
    if (e <= 0) {
        if (e < -10) return uint16_t(sign);
        m |= 0x800000u;
        const uint32_t shift = uint32_t(14 - e);
        uint32_t h = m >> shift;
        const uint32_t rem = m & ((1u << shift) - 1), half = 1u << (shift - 1);
        if (rem > half || (rem == half && (h & 1))) ++h;
        return uint16_t(sign | h);
    }
    uint32_t h = sign | (uint32_t(e) << 10) | (m >> 13);
    const uint32_t rem = m & 0x1fffu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1))) ++h;
    return uint16_t(h);
}
inline float halfToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    uint32_t e = (h >> 10) & 0x1f, m = h & 0x3ffu, x;
    if (e == 0) {
        if (m == 0) x = sign;
        else { e = 127 - 15 + 1; while (!(m & 0x400u)) { m <<= 1; --e; } m &= 0x3ffu; x = sign | (e << 23) | (m << 13); }
    } else if (e == 31) x = sign | 0x7f800000u | (m << 13);
    else x = sign | ((e - 15 + 127) << 23) | (m << 13);
    float f; std::memcpy(&f, &x, 4);
    return f;
}
} // namespace aiverse::bake
