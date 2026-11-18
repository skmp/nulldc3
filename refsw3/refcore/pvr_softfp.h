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
#if defined(__SIZEOF_INT128__)
    __int128 vx = (__int128)x.mant << sx, vy = (__int128)y.mant << sy;
    if (x.neg) vx = -vx;
    if (y.neg) vy = -vy;
    __int128 s = vx + vy;
    r.neg = s < 0;
    unsigned __int128 m = r.neg ? (unsigned __int128)(-s) : (unsigned __int128)s;
    int n = (uint64_t)(m >> 64) ? 64 + Fp::Bits((uint64_t)(m >> 64)) : Fp::Bits((uint64_t)m);
    if (n > 64) {
        m >>= (n - 64);
        e += (n - 64);
    }
    r.mant = (uint64_t)m;
#else
    // without a 128 bit integer: drop the bits of the smaller operand that cannot reach the result's `bits`
    // bits... not exact for subtraction; refsw2 is built with compilers that have __int128
#error "pvr_softfp.h needs a 128 bit integer type"
#endif
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
