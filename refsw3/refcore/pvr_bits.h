// Bit scans for refsw3 (refcore's soft float, refta's numbers), as the compiler's intrinsics: clz / ctz on GCC and
// Clang, BSR / BSF on MSVC.
#pragma once

#include <cstdint>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace pvrfp {

// the number of bits of v: the index of its leading one plus one (0 for 0)
inline int BitLength(uint64_t v)
{
    if (!v)
        return 0;
#if defined(__GNUC__) || defined(__clang__)
    return 64 - __builtin_clzll(v);
#elif defined(_MSC_VER)
    unsigned long i;
    _BitScanReverse64(&i, v);
    return (int)i + 1;
#else
    int n = 0;
    while (v) { n++; v >>= 1; }
    return n;
#endif
}

// the zero bits below v's lowest one (v not 0)
inline int TrailingZeros(uint64_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(v);
#elif defined(_MSC_VER)
    unsigned long i;
    _BitScanForward64(&i, v);
    return (int)i;
#else
    int n = 0;
    while (!(v & 1)) { n++; v >>= 1; }
    return n;
#endif
}

} // namespace pvrfp
