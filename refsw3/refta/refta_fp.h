#pragma once
/*
    The tile accelerator's numbers (refta.cpp). The TA does no float arithmetic: it takes the floats it is sent
    apart, compares them with a power of two and keeps the integer part of a scaled value. That is all here, on
    the bits, with no float or double. (The ISP and TSP setup units are another matter: pvr_softfp.h.)

    What the console showed (dreamcast-tests/ta-tests/ta-tests, ta-probe): the sign bit counts for zeros, infinities and
    NaNs alike (-0 is "negative" for a colour, but bins as tile 0); an infinity and a NaN of the same sign behave the
    same; denormals are their tiny values, not zero.
*/
#include <cstdint>

namespace refta_fp {

struct F32 {
    bool neg;           // the sign bit
    bool special;       // exponent all ones: an infinity or a NaN
    int32_t exp;        // value = mant * 2^exp
    uint32_t mant;      // with the hidden bit; a denormal as it is; 0 for zeros and specials

    static F32 Of(uint32_t bits)
    {
        F32 f;
        f.neg = (bits >> 31) != 0;
        const uint32_t e = (bits >> 23) & 0xff, m = bits & 0x7fffff;
        f.special = e == 0xff;
        if (f.special) {
            f.mant = 0;
            f.exp = 0;
        } else if (e == 0) {
            f.mant = m;
            f.exp = -149;
        } else {
            f.mant = m | 0x800000;
            f.exp = (int32_t)e - 150;
        }
        return f;
    }

    bool zero() const { return !special && mant == 0; }

    // |value| >= 2^e (not for specials)
    bool atLeastPow2(int e) const
    {
        if (!mant)
            return false;
        int bits = 0;
        for (uint32_t m = mant; m; m >>= 1)
            bits++;
        return exp + bits > e;
    }

    // floor(|value| * 2^k), for |value| * 2^k below 2^32 (not for specials)
    uint32_t floorScaled(int k) const
    {
        const int e = exp + k;
        if (e >= 0)
            return mant << e;
        return -e >= 32 ? 0 : mant >> -e;
    }
};

}
