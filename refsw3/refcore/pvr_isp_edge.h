/*
    The ISP's inside test, in the setup units' own arithmetic (pvr_softfp.h).

    Everything here was measured on a console with dreamcast-tests/pvr-tests/covmeas and
    dreamcast-tests/pvr-tests/hugepos; the README there has the evidence for each rule. The parameters of EdgeConfig are
    what the probes of dreamcast-tests/model scan.

    Per edge a -> b, with the tile's first sample point (x0, y0):
        c     = xb ya - xa yb                       24 bit products, 24 bit difference
        first = dx y0 - dy x0                       24 bit products; an operand 25 exponents below the other is
                                                    dropped by the adder; at 2^128 the start saturates
        start = first + c                           24 bits, then kept as a 20 bit float; saturates at 2^128
        steps = dx per row, -dy per column          20 bit floats, cut to 21 bits under the larger's exponent
        E     = top(larger step) + 6, or top(start) + 1 if that is more;  ZERO: |v| <= 2^(E - 21)
    and the decision:
        R = value of the row at the tile's first column. R zero: the whole row of the tile is decided by the sign
            of the column step, or by that of the row step when the column step is zero as well.
        otherwise the sign of the pixel's value; a zero pixel is inside when the next pixel to its right is not
        negative; in the tile's last column, when the column step is positive.
        An edge between two equal corners is zero everywhere and counts as inside.
    The winding is the sign of the setup's determinant, zero counting as positive.
*/
#pragma once
#include "pvr_softfp.h"

namespace pvrisp {

using pvrfp::Fp;

// The edge unit's numbers as the hardware has them, baked in (pvr_softfp.h: C<N>). Every one measured with
// dreamcast-tests/pvr-tests/covmeas (98 archived sets, 0 differences) and hugepos; EdgeConfig below is the same with
// plain ints, for the probes of dreamcast-tests/model to sweep (covprobe --scan); the code is templated on either.
struct EdgeHW {
    static constexpr pvrfp::C<24> productBits{};    // x * y, dx * y0, dy * x0
    static constexpr pvrfp::C<24> sumBits{};        // every add
    static constexpr pvrfp::C<25> reach{};          // exponents an adder can align over; beyond, the smaller is dropped
    static constexpr pvrfp::C<-1> guard{};          // guard bits of the tile-start adds; negative: exact before the cut
    static constexpr pvrfp::C<20> startBits{};      // the start value as kept per tile
    static constexpr pvrfp::C<20> slopeBits{};      // the steps
    static constexpr pvrfp::C<21> pairBits{};       // bits of both steps under the exponent of the larger
    static constexpr pvrfp::C<6> blockAdd{};        // E = top(larger step) + blockAdd ...
    static constexpr pvrfp::C<1> startAdd{};        // ... or top(start) + startAdd
    static constexpr pvrfp::C<21> zeroShift{};      // ZERO: |v| <= 2^(E - zeroShift)
    static constexpr pvrfp::C<21> tieShift{};       // the column step is zero for a row that starts at zero when |sx| <= 2^(E - tieShift)
    static constexpr pvrfp::C<128> overflowExp{};   // the start saturates when the sum, or the start, reaches 2^overflowExp ...
    static constexpr pvrfp::C<127> saturateExp{};   // ... to 2^saturateExp
    static constexpr std::true_type lastColumnByStep{};   // a zero pixel in column 31: inside iff the column step is positive
};

struct EdgeConfig {
    int productBits = EdgeHW::productBits;
    int sumBits = EdgeHW::sumBits;
    int reach = EdgeHW::reach;
    int guard = EdgeHW::guard;
    int startBits = EdgeHW::startBits;
    int slopeBits = EdgeHW::slopeBits;
    int pairBits = EdgeHW::pairBits;
    int blockAdd = EdgeHW::blockAdd;
    int startAdd = EdgeHW::startAdd;
    int zeroShift = EdgeHW::zeroShift;
    int tieShift = EdgeHW::tieShift;
    int overflowExp = EdgeHW::overflowExp;
    int saturateExp = EdgeHW::saturateExp;
    bool lastColumnByStep = EdgeHW::lastColumnByStep;
};

// The winding of a triangle: -1 when the setup's determinant is positive or zero, +1 when it is negative.
// `bad` when a coordinate is an infinity or a NaN.
template<class Cfg>
inline int Winding(const Fp x[3], const Fp y[3], const Cfg& cfg, bool* bad = nullptr, Fp* detOut = nullptr)
{
    auto e = [&](int j, int k) {
        return pvrfp::Sub(pvrfp::Mul(x[j], y[k], cfg.productBits), pvrfp::Mul(x[k], y[j], cfg.productBits), cfg.sumBits, cfg.reach);
    };
    Fp e1 = e(1, 2), e2 = e(2, 0), e3 = e(0, 1);
    Fp det = pvrfp::Add(pvrfp::Add(e1, e2, cfg.sumBits, cfg.reach), e3, cfg.sumBits, cfg.reach);
    if (bad)
        *bad = det.bad;
    if (detOut)
        *detOut = det;
    return (det.neg && !det.zero()) ? 1 : -1;
}

// The rows a polygon is rasterized on (not a modifier volume triangle): floor(ymin) .. floor(ymax + 0.5), whatever
// the sample offset (covmeas, mix sliver: the truncated edge functions of a sliver accept pixels beyond its ends, and
// a console cuts those rows off). The corners finite; floor(y + 1/2) = (floor(2y) + 1) >> 1; beyond 2^40 clamped.
inline void RowRange(const Fp* y, int n, int64_t* first, int64_t* last)
{
    *first = INT64_MAX; *last = INT64_MIN;
    for (int k = 0; k < n; k++) {
        const int64_t f = pvrfp::FloorScaled(y[k], 0), l = (pvrfp::FloorScaled(y[k], 1) + 1) >> 1;
        if (f < *first) *first = f;
        if (l > *last) *last = l;
    }
}

// One edge in one tile, ready to be asked about pixels.
struct EdgeTile {
    // Twice the edge function in units of the steps' last bit, turned so that inside is positive; the lowest
    // bit is set when the start value has bits below that unit (it is then between two units).
    int64_t t0 = 0, tx = 0, ty = 0, zero2 = 0, tie2 = 0;
    bool constant = true, constantInside = false;       // the whole tile has one answer
    bool lastColumnByStep = true;

    // a -> b, sgn from Winding(); (x0Halves, y0Halves): the tile's first sample point in half pixels
    template<class Cfg>
    void Setup(const Fp& xa, const Fp& ya, const Fp& xb, const Fp& yb, int64_t x0Halves, int64_t y0Halves, int sgn, const Cfg& cfg)
    {
        using namespace pvrfp;
        constant = true;
        constantInside = false;
        lastColumnByStep = cfg.lastColumnByStep;
        if (xa.bad || ya.bad || xb.bad || yb.bad)
            return;                                     // an infinity or a NaN: nothing is inside
        const auto PB = cfg.productBits;
        const auto SB = cfg.sumBits;
        const auto RE = cfg.reach;
        Fp c = Sub(Mul(xb, ya, PB), Mul(xa, yb, PB), SB, RE);
        Fp dx = Sub(xa, xb, SB, RE), dy = Sub(ya, yb, SB, RE);
        Fp x0 = Fp::FromHalves(x0Halves), y0 = Fp::FromHalves(y0Halves);
        const auto GD = cfg.guard;
        Fp first = Add(Mul(dx, y0, PB), -Mul(dy, x0, PB), SB, RE, GD);
        Fp start;
        if (first.atLeastPow2(cfg.overflowExp))
            start = Fp::Pow2(cfg.saturateExp, first.neg);
        else {
            start = Add(first, c, SB, RE, GD);
            if (start.atLeastPow2(cfg.overflowExp)) {
                // Through the constant only. With a column step of 2^127 or more (an edge far down or up, the next
                // pixel's value past 2^128 away) the start is not 2^127 but +-2^132 less the first sum
                // (dreamcast-tests/pvr-tests/faredge pages 12-35: the K 127 vertical triangles' crossings, 19 to 24
                // pixels into the tile; modvol: its garbage polygon's edge, 192 of 192 cells). GAP: faredge page 4's
                // first-column strip (far corner up, the first product the one past 2^128) keeps 2^127 on the
                // console, and page 1's control strip; which of the two the hardware takes when is not known.
                const bool neg = start.neg;
                if (dy.atLeastPow2(cfg.saturateExp))
                    start = Add(Fp::Pow2(cfg.saturateExp + 5, neg), -first, 62, 400);
                else
                    start = Fp::Pow2(cfg.saturateExp, neg);
            }
        }
        Fp sy = dx.cut(cfg.slopeBits), sx = (-dy).cut(cfg.slopeBits);
        int ep = dx.top() > dy.top() ? dx.top() : dy.top();
        int es = start.top();
        if (ep == NoExp && es == NoExp) {
            // a -> b is a point: the edge function is zero everywhere, and that counts as INSIDE. A triangle of
            // three equal corners, drawn without culling, so fills the whole width of its tile on the rows of
            // its row range (dreamcast-tests/pvr-tests/covmeas, mix degen: 234 of 234).
            constantInside = true;
            return;
        }
        Fp s20 = start.cut(cfg.startBits);
        if (sgn < 0) {
            s20 = -s20; sx = -sx; sy = -sy;
        }
        if (ep == NoExp) {                              // no steps: the start's sign
            constantInside = !s20.neg;
            return;
        }
        int E = ep + cfg.blockAdd;
        if (es != NoExp && es + cfg.startAdd > E)
            E = es + cfg.startAdd;
        const int unit = ep - cfg.pairBits;             // the exponent of the steps' last bit
        if (E - unit > 56) {
            // the start is 2^30 times a step or more: no pixel of the tile is near zero or changes sign
            constantInside = !s20.neg;
            return;
        }
        sx = sx.cutBelow(unit);
        sy = sy.cutBelow(unit);
        auto units = [&](const Fp& v, bool* frac) -> int64_t {      // floor(v / 2^unit); *frac: not a whole number
            if (frac) *frac = false;
            if (v.zero())
                return 0;
            int s = v.exp - unit;
            int64_t m;
            if (s >= 0)
                m = (int64_t)(v.mant << s);
            else {
                uint64_t lost = (-s >= 64) ? v.mant : (v.mant & ((1ull << -s) - 1));
                m = (-s >= 64) ? 0 : (int64_t)(v.mant >> -s);
                if (lost) {
                    if (frac) *frac = true;
                    if (v.neg)
                        m += 1;                         // floor of a negative: one further from zero
                }
            }
            return v.neg ? -m : m;
        };
        bool frac;
        int64_t s0 = units(s20, &frac);
        t0 = 2 * s0 + (frac ? 1 : 0);
        tx = 2 * units(sx, nullptr);
        ty = 2 * units(sy, nullptr);
        zero2 = (int64_t)2 << (E - cfg.zeroShift - unit);
        tie2 = (int64_t)2 << (E - cfg.tieShift - unit);
        constant = false;
    }

    bool Inside(int i, int j) const
    {
        if (constant)
            return constantInside;
        int64_t r = t0 + ty * j;
        if (r >= -zero2 && r <= zero2)
            return (tx >= -tie2 && tx <= tie2) ? ty > 0 : tx > 0;
        int64_t v = r + tx * i;
        if (v > zero2)
            return true;
        if (v < -zero2)
            return false;
        if (i == 31 && lastColumnByStep)
            return tx > 0;
        return v + tx >= -zero2;
    }

    // no pixel of row j of the tile is inside (cheap; false when in doubt)
    bool RowOutside(int j) const
    {
        if (constant)
            return !constantInside;
        int64_t r = t0 + ty * j;
        if (r >= -zero2 && r <= zero2)
            return !((tx >= -tie2 && tx <= tie2) ? ty > 0 : tx > 0);
        return r < -zero2 && r + tx * 31 < -zero2;
    }
};

} // namespace pvrisp
