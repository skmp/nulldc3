/*
    Soft floating point for the PowerVR2 setup units.

    The hardware's arithmetic is not IEEE: mantissas are cut toward zero, there are no denormals, adders have a
    limited alignment shift, and overflow saturates. Emulating that with float or double works only as long as
    every such limit is remembered by hand, and each time it was forgotten refsw2 drew something a console does
    not (dreamcast-tests/pvr-tests/covmeas, dreamcast-tests/pvr-tests/hugepos). Here the number is explicit: a sign, an
    exponent and an integer mantissa, and every operation says how many bits it keeps.

        value = (neg ? -1 : 1) * mant * 2^exp        mant == 0: zero

    A zero keeps a sign, with the rules of ordinary floating point: a product's sign is the exclusive or of its
    factors' signs, a sum of two zeros is negative only when both are, an exact cancellation gives +0, and
    cutting a negative number to nothing leaves -0. The ISP units never look at it; the TSP does: a slope that
    cancels exactly, times a negative reciprocal, is a negative zero there, and that floors to one unit below
    zero (dreamcast-tests/pvr-tests/uvmeas).

    Used by refsw2 and by the standalone model / probes of dreamcast-tests/model. Header only, no dependencies.
*/
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace pvrfp {

// 128 bit integers for the exact sums and the fixed-point stepping. GCC and Clang have them (__int128); MSVC does
// not, and gets two structs that do, in two's complement, the few things this code asks of them: shifts, sums,
// negation, products (the low 128 bits), masks, comparisons, and the low 64 bits.
#if defined(__SIZEOF_INT128__) && !defined(PVRFP_NO_INT128)     // PVRFP_NO_INT128: the structs, to test them
typedef __int128 i128;
typedef unsigned __int128 u128;
#else
struct i128;
struct u128 {
    uint64_t lo = 0, hi = 0;
    constexpr u128() {}
    constexpr u128(uint64_t v) : lo(v), hi(0) {}
    constexpr u128(uint64_t h, uint64_t l) : lo(l), hi(h) {}
    explicit u128(const i128& v);
    explicit operator uint64_t() const { return lo; }
    static u128 Mul64(uint64_t a, uint64_t b)
    {
        const uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
        const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
        const uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
        return u128(p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (mid << 32) | (uint32_t)p00);
    }
    friend u128 operator+(u128 a, u128 b) { u128 r; r.lo = a.lo + b.lo; r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0); return r; }
    friend u128 operator-(u128 a, u128 b) { u128 r; r.lo = a.lo - b.lo; r.hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0); return r; }
    friend u128 operator*(u128 a, u128 b) { u128 r = Mul64(a.lo, b.lo); r.hi += a.lo * b.hi + a.hi * b.lo; return r; }
    friend u128 operator&(u128 a, u128 b) { return u128(a.hi & b.hi, a.lo & b.lo); }
    friend u128 operator<<(u128 a, int n)
    {
        if (n <= 0) return a;
        if (n >= 128) return u128();
        if (n >= 64) return u128(a.lo << (n - 64), 0);
        return u128((a.hi << n) | (a.lo >> (64 - n)), a.lo << n);
    }
    friend u128 operator>>(u128 a, int n)
    {
        if (n <= 0) return a;
        if (n >= 128) return u128();
        if (n >= 64) return u128(0, a.hi >> (n - 64));
        return u128(a.hi >> n, (a.lo >> n) | (a.hi << (64 - n)));
    }
    u128& operator>>=(int n) { return *this = *this >> n; }
    friend bool operator==(u128 a, u128 b) { return a.lo == b.lo && a.hi == b.hi; }
    friend bool operator!=(u128 a, u128 b) { return !(a == b); }
};
struct i128 {
    u128 v;
    constexpr i128() {}
    constexpr i128(int x) : v((uint64_t)(x < 0 ? ~0ull : 0ull), (uint64_t)(int64_t)x) {}
    constexpr i128(long x) : v((uint64_t)(x < 0 ? ~0ull : 0ull), (uint64_t)(int64_t)x) {}
    constexpr i128(long long x) : v((uint64_t)(x < 0 ? ~0ull : 0ull), (uint64_t)x) {}
    constexpr i128(unsigned x) : v((uint64_t)x) {}
    constexpr i128(unsigned long x) : v((uint64_t)x) {}
    constexpr i128(unsigned long long x) : v((uint64_t)x) {}
    explicit i128(u128 x) : v(x) {}
    friend i128 operator-(i128 a) { return i128(u128() - a.v); }
    friend i128 operator+(i128 a, i128 b) { return i128(a.v + b.v); }
    friend i128 operator-(i128 a, i128 b) { return i128(a.v - b.v); }
    friend i128 operator*(i128 a, i128 b) { return i128(a.v * b.v); }
    friend i128 operator<<(i128 a, int n) { return i128(a.v << n); }
    friend bool operator<(i128 a, i128 b)
    {
        return a.v.hi != b.v.hi ? (int64_t)a.v.hi < (int64_t)b.v.hi : a.v.lo < b.v.lo;
    }
    friend bool operator>(i128 a, i128 b) { return b < a; }
    friend bool operator<=(i128 a, i128 b) { return !(b < a); }
    friend bool operator>=(i128 a, i128 b) { return !(a < b); }
    friend bool operator==(i128 a, i128 b) { return a.v == b.v; }
    friend bool operator!=(i128 a, i128 b) { return a.v != b.v; }
};
inline u128::u128(const i128& x) : lo(x.v.lo), hi(x.v.hi) {}
#endif

static const int NoExp = -1000000;      // top() of a zero

struct Fp {
    bool neg = false;
    bool bad = false;       // came from an infinity or a NaN: the caller decides what that means
    int32_t exp = 0;        // of the mantissa's last bit
    uint64_t mant = 0;      // at most 32 bits after any operation here

    // the number of bits of v (0 for 0)
    static int Bits(uint64_t v)
    {
#if defined(__GNUC__) || defined(__clang__)
        return v ? 64 - __builtin_clzll(v) : 0;
#else
        int n = 0;
        while (v) { n++; v >>= 1; }
        return n;
#endif
    }

    bool zero() const { return mant == 0; }

    // The exponent as frexp gives it: the value is in [2^(top - 1), 2^top). NoExp for a zero.
    int top() const { return mant ? exp + Bits(mant) : NoExp; }

    Fp operator-() const { Fp r = *this; r.neg = !r.neg; return r; }

    // Keep the `bits` most significant bits of the mantissa (toward zero).
    Fp cut(int bits) const
    {
        Fp r = *this;
        int n = Bits(r.mant);
        if (n > bits) {
            r.mant >>= (n - bits);
            r.exp += (n - bits);
        }
        return r;
    }

    // Drop everything below 2^e (toward zero).
    Fp cutBelow(int e) const
    {
        Fp r = *this;
        if (r.mant && r.exp < e) {
            int s = e - r.exp;
            r.mant = s >= 64 ? 0 : r.mant >> s;
            r.exp = e;
        }
        return r;
    }

    static Fp FromFloat(float f)
    {
        uint32_t u;
        memcpy(&u, &f, 4);
        Fp r;
        r.neg = (u >> 31) != 0;
        uint32_t e = (u >> 23) & 0xff, m = u & 0x7fffff;
        if (e == 0xff) {
            r.bad = true;
            r.mant = 0;
            r.neg = false;
        } else if (e == 0) {
            r.mant = m;             // denormal (or zero)
            r.exp = -149;
        } else {
            r.mant = m | 0x800000;
            r.exp = (int32_t)e - 150;
        }
        return r;
    }

    // k / 2: tile coordinates and their half pixel offsets
    static Fp FromHalves(int64_t k)
    {
        Fp r;
        r.neg = k < 0;
        r.mant = (uint64_t)(k < 0 ? -k : k);
        r.exp = -1;
        return r;
    }

    // exact: a double has a 53 bit mantissa
    static Fp FromDouble(double v)
    {
        Fp r;
        if (!std::isfinite(v)) {
            r.bad = true;
            return r;
        }
        r.neg = std::signbit(v);
        if (v == 0)
            return r;
        int e;
        double m = frexp(fabs(v), &e);
        r.mant = (uint64_t)ldexp(m, 53);
        r.exp = e - 53;
        while (!(r.mant & 1)) { r.mant >>= 1; r.exp++; }
        return r;
    }

    static Fp Pow2(int e, bool negative)
    {
        Fp r;
        r.mant = 1;
        r.exp = e;
        r.neg = negative;
        return r;
    }

    // For debugging and for code that still works in double: exact while the mantissa has at most 53 bits and
    // the exponent is in a double's range.
    double toDouble() const
    {
        if (bad)
            return NAN;
        double v = ldexp((double)mant, exp);
        return neg ? -v : v;
    }

    // |value| >= 2^e
    bool atLeastPow2(int e) const { return mant != 0 && top() > e; }
};

// a * b, the product's mantissa cut to `bits` bits.
inline Fp Mul(const Fp& a, const Fp& b, int bits)
{
    Fp r;
    r.bad = a.bad || b.bad;
    Fp x = a.cut(32), y = b.cut(32);
    r.mant = x.mant * y.mant;
    r.exp = x.exp + y.exp;
    r.neg = a.neg != b.neg;
    return r.cut(bits);
}

// a + b in an adder that aligns the smaller operand to the larger: an operand whose exponent (top) is `reach`
// or more below the other's is beyond the alignment shift and is dropped; otherwise the sum is exact (as with
// a sticky bit) and its mantissa is then cut to `bits` bits.
// guard: how many bits below the larger operand's last bit the smaller operand keeps when it is aligned (the rest
// is cut off before the add); negative: all of them (exact, as with a sticky bit).
inline Fp Add(const Fp& a, const Fp& b, int bits, int reach, int guard = -1)
{
    Fp r;
    r.bad = a.bad || b.bad;
    if (a.zero() && b.zero()) { r.neg = a.neg && b.neg; return r; }
    if (b.zero()) { r = a.cut(bits); r.bad = a.bad || b.bad; return r; }
    if (a.zero()) { r = b.cut(bits); r.bad = a.bad || b.bad; return r; }
    int ta = a.top(), tb = b.top();
    if (ta - tb >= reach) { r = a.cut(bits); r.bad = a.bad || b.bad; return r; }
    if (tb - ta >= reach) { r = b.cut(bits); r.bad = a.bad || b.bad; return r; }
    Fp x = a.cut(32), y = b.cut(32);
    if (guard >= 0) {
        if (ta >= tb) y = y.cutBelow(ta - bits - guard);
        else x = x.cutBelow(tb - bits - guard);
        if (y.zero()) { r = a.cut(bits); r.bad = a.bad || b.bad; return r; }
        if (x.zero()) { r = b.cut(bits); r.bad = a.bad || b.bad; return r; }
    }
    // both to the smaller exponent: the shifts stay below 32 + reach bits; keep within 126 bits
    int e = x.exp < y.exp ? x.exp : y.exp;
    int sx = x.exp - e, sy = y.exp - e;
    if (sx > 90 || sy > 90) {       // cannot happen with reach <= 58; be safe
        return ta > tb ? a.cut(bits) : b.cut(bits);
    }
    i128 vx = (i128)x.mant << sx, vy = (i128)y.mant << sy;
    if (x.neg) vx = -vx;
    if (y.neg) vy = -vy;
    i128 s = vx + vy;
    r.neg = s < 0;
    u128 m = r.neg ? (u128)(-s) : (u128)s;
    int n = (uint64_t)(m >> 64) ? 64 + Fp::Bits((uint64_t)(m >> 64)) : Fp::Bits((uint64_t)m);
    if (n > 64) {
        m >>= (n - 64);
        e += (n - 64);
    }
    r.mant = (uint64_t)m;
    r.exp = e;
    if (r.mant == 0)
        r.neg = false;
    r = r.cut(bits);
    r.bad = a.bad || b.bad;
    return r;
}

inline Fp Sub(const Fp& a, const Fp& b, int bits, int reach) { return Add(a, -b, bits, reach); }

// 1 / v with the mantissa cut to `bits` bits (v not zero)
inline Fp Recip(const Fp& v, int bits)
{
    Fp d = v.cut(32);
    int nb = Fp::Bits(d.mant);
    int sh = nb + bits + 2;                             // at most 60
    Fp r;
    r.bad = v.bad;
    r.neg = v.neg;
    r.mant = ((uint64_t)1 << sh) / d.mant;
    r.exp = -sh - d.exp;
    return r.cut(bits);
}

} // namespace pvrfp
