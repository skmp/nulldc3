/*
    The ISP's depth of a triangle, in the setup units' own arithmetic (pvr_softfp.h).

    Measured on a console with dreamcast-tests/pvr-tests/zmeas (the stored depth of general triangles, read back as a
    number):
      - 24 bit truncating arithmetic; the adds of the slopes and of the determinant are (v1 + v2) + v3, those of
        the constant (v1 + v3) + v2
      - the depth at the tile's first pixel is the plane BEFORE its division by the determinant, evaluated there,
        times the reciprocal: ((a x0 + b y0) + c) * inv
      - that start is kept as a 20 bit float; the slopes are a * inv and b * inv as 20 bit floats, cut to 21 bits
        under the larger one's exponent and to the tile's grid, 26 bits under the start's exponent
      - the start is cut to the slopes' grid as well (21 bits under the larger slope's exponent), toward zero, of
        either sign: it matters where the start is smaller than the slopes (the plane crosses zero near the tile's
        first pixel). A negative start then comes out up to one grid step higher, and a pixel just under a stored
        step goes over it: the depth buffers of polly2-data's dumps read out (dreamcast-tests dumprender.sh z,
        zdumps.sh), the 36 depths of 64 frames that differed (sa2_ingame 34, shenmue_intro 2), and the 3 brackets
        of zmeas (reach1, reach1r, zero6) the model missed; 0 of 89,801 brackets missed with it, 39 before. A
        positive start cut lower shows only at depths below about a quarter of the slopes: dreamcast-tests
        pvr-tests/zstart, 47,522 of 47,522 pixels with any start cut (121 differ when only a negative one is)
      - a pixel is the start plus whole steps, and is stored with a 19 bit mantissa; the steps are fixed point
        however far the depth reaches above the start (zmeas mix reach: exact to 29 powers of two above it), and a
        start of exactly zero is stepped the same way, on the slopes' own grid
      - a determinant of ZERO (three equal corners, three corners on a line, or a sliver whose constants cancel):
        every pixel gets the mean of the three corners' depths, whatever the constants are
        (dreamcast-tests/pvr-tests/covmeas, mixes degen, tiny and sliver with --prim zprobe --zs: 11,456 + 1,344 + 53
        pixels, brackets 0.02 to 0.1 wide)
*/
#pragma once
#include "pvr_softfp.h"

namespace pvrisp {

using pvrfp::Fp;

// The depth unit's numbers as the hardware has them, baked in (pvr_softfp.h: C<N>); the evidence is above (zmeas,
// zstart, covmeas). DepthConfig below is the same with plain ints, for dreamcast-tests/model/depthprobe --scan and the
// generators; DepthTileT is templated on either.
struct DepthHW {
    static constexpr pvrfp::C<24> productBits{};    // z * (difference), z * e, a * x0, b * y0
    static constexpr pvrfp::C<24> sumBits{};
    static constexpr pvrfp::C<25> reach{};          // the adders' alignment reach (measured on the edge unit, pvr_isp_edge.h)
    static constexpr pvrfp::C<24> recipBits{};      // 1 / determinant
    static constexpr pvrfp::C<24> coefBits{};       // a * inv, b * inv, c * inv, start * inv
    static constexpr pvrfp::C<20> startBits{};
    static constexpr pvrfp::C<20> slopeBits{};
    static constexpr pvrfp::C<21> pairBits{};
    static constexpr pvrfp::C<26> tileBits{};
    static constexpr pvrfp::C<19> storedBits{};
    // the start cut to the slopes' grid too: 1 any start (a console: zstart), 2 a negative one only, 0 not (the model
    // before); 0 and 2 are kept for the generators and depthprobe --scan
    static constexpr pvrfp::C<1> startOnSlopes{};
    // The setup's numbers have the limits measured for the TSP's (pvr_tsp_plane.h, Plane::Setup): exponent field 0 is
    // zero and 255 infinity, a result of 2^128 or more is an infinity and one below 2^-126 a zero, with the same rules
    // for sums and products of them. Not measured here number by number: with them dreamcast-tests/pvr-tests/huge is
    // the console's in 250 of 250 regions (228 without), including the quads of which a console draws one triangle
    // only at 2^127, the largest number, infinity and NaN (the two triangles' sums come to infinities of opposite
    // sign). range 0: no limits, as before.
    static constexpr pvrfp::C<1> range{};
};

struct DepthConfig {
    int productBits = DepthHW::productBits;
    int sumBits = DepthHW::sumBits;
    int reach = DepthHW::reach;
    int recipBits = DepthHW::recipBits;
    int coefBits = DepthHW::coefBits;
    int startBits = DepthHW::startBits;
    int slopeBits = DepthHW::slopeBits;
    int pairBits = DepthHW::pairBits;
    int tileBits = DepthHW::tileBits;
    int storedBits = DepthHW::storedBits;
    int startOnSlopes = DepthHW::startOnSlopes;
    int range = DepthHW::range;
};

using pvrfp::Recip;

// A depth as the ISP stores and compares it: the bits of an IEEE single (19 significant bits from the depth unit; a
// corner's z as given when the plane cannot be set up; the background's, refsw_lists.cpp). The compare is NOT a
// float compare: the bits are read as a signed 32 bit integer, the fraction bits of a negative depth inverted. So
// positive depths order as floats, every negative one is below zero, and of two negative depths the one with the
// larger exponent is the larger, within one exponent the float's own order holds (-2 > -1 > -1.5 > -0.5 > -0.75);
// a NaN is an ordinary number (positive ones above +infinity); -0 is +0 (dreamcast-tests/pvr-tests/zsign: every pair
// of 20 depths from -2^100 to 2^100, textured so that the negative ones show, Greater and Less; bgspec, bgneg,
// bgneg2: probes against backgrounds below zero). Also measured: the 19 bits (zmeas, zblock, precision), depths
// below zero drawn like any other (uvmeas wneg). GAP: below 2^-126 the value is rounded to a float's denormal grid,
// and from 2^128 it is an infinity: a float's range, not measured as the depth buffer's own.
struct StoredDepth {
    uint32_t bits = 0;
    static StoredDepth Bits(uint32_t b) { StoredDepth d; d.bits = b; return d; }
    int32_t key() const
    {
        if (bits == 0x80000000u)
            return 0;
        return (int32_t)(bits ^ ((bits >> 31) ? 0x007fffffu : 0));
    }
    friend bool operator<(StoredDepth a, StoredDepth b) { return a.key() < b.key(); }
    friend bool operator>(StoredDepth a, StoredDepth b) { return b < a; }
    friend bool operator<=(StoredDepth a, StoredDepth b) { return a.key() <= b.key(); }
    friend bool operator>=(StoredDepth a, StoredDepth b) { return b <= a; }
    friend bool operator==(StoredDepth a, StoredDepth b) { return a.key() == b.key(); }
    friend bool operator!=(StoredDepth a, StoredDepth b) { return !(a == b); }

    // A depth that overflows is 2^127 with its sign: a polygon whose plane cannot be set up for its size (corners at
    // the largest float, +-infinity, a NaN, or -2^116 and beyond over a cell) ties with a background of +-2^127, is
    // above one of 1.5 2^126 and below one of 1.5 2^127 (dreamcast-tests/pvr-tests/bgbig, bgspec, zsign page 15).
    static StoredDepth Saturated(bool neg) { return Bits((neg ? 0x80000000u : 0) | 0x7f000000u); }

    // v as a float: to nearest, ties to even, on the denormal grid (2^-149) below 2^-126, 2^127 (Saturated) from
    // 2^128 and for an infinity or a NaN (Fp::bad)
    static StoredDepth FromFp(const Fp& v)
    {
        if (v.bad)
            return Saturated(v.neg);
        const uint32_t sign = v.neg ? 0x80000000u : 0;
        if (v.zero())
            return Bits(sign);
        uint64_t m = v.mant;
        int e = v.exp;                                  // value = m 2^e
        const int top = v.top();                        // in [2^(top - 1), 2^top)
        // the last bit kept: 24 bits from the leading one, or 2^-149 below 2^-126
        int keep = top - 24;
        if (keep < -149)
            keep = -149;
        if (e < keep) {
            const int d = keep - e;
            if (d > 63) {
                m = 0;                                  // far below half a step: zero
            } else {
                const uint64_t rest = m & ((1ull << d) - 1), half = 1ull << (d - 1);
                m >>= d;
                if (rest > half || (rest == half && (m & 1)))
                    m++;
            }
            e = keep;
        }
        if (m == 0)
            return Bits(sign);
        const int t2 = e + Fp::Bits(m);                 // after the rounding
        if (t2 > 128)
            return Saturated(v.neg);
        if (t2 <= -126) {                               // denormal: the mantissa at 2^-149
            return Bits(sign | (uint32_t)(m << (e + 149)));
        }
        const int n = Fp::Bits(m);
        const uint32_t frac = (uint32_t)((n <= 24 ? m << (24 - n) : m >> (n - 24)) & 0x7fffffu);
        return Bits(sign | (uint32_t)(t2 - 1 + 127) << 23 | frac);
    }
    // for logs and the probes of dreamcast-tests/model only
    double toDouble() const { float f; memcpy(&f, &bits, 4); return f; }
};

template<class Cfg>
struct DepthTileT {
    bool valid = false, block = false;
    // A plane that overflows but has slopes (dreamcast-tests/pvr-tests/zword, zbad, zbadbg, zbadk, zovf; README zspec).
    // ovf 2: a slope overflowed: every pixel's depth is the word of the register r = r0 + (sa i + sb j) 2^25 (kWrap at
    // whole steps, WrapWord between them), i, j from the tile's first pixel, sa, sb the overflowed slopes' signs (0: that
    // slope did not overflow), r0 the plane's start, all in units of 2^102 (2^25: one step, 2^127). ovf 1: only the
    // constant did: the tile's first pixel 0, every other one ovfWord.
    int ovf = 0, sa = 0, sb = 0;
    int64_t r0 = 0;
    StoredDepth ovfWord;
    StoredDepth first_z;                                // not valid: the depth every pixel gets
    // block form: twice the depth in units of 2^unit is d0 + sx2 * dx + sy2 * dy, with sx2, sy2 the steps from
    // the tile's first pixel center in half pixels
    pvrfp::i128 d0 = 0, dx = 0, dy = 0;
    int unit = 0;
    // float form (a plane of zero)
    Fp A, B, C;
    int left = 0, top = 0;
    Cfg cfg;

    // x, y, z of the three corners in rasterizer order, the bits of IEEE singles as the TA gives them; (left, top):
    // the tile's first pixel
    void Setup(const uint32_t x[3], const uint32_t y[3], const uint32_t z[3], int tileLeft, int tileTop, const Cfg& config)
    {
        using namespace pvrfp;
        cfg = config;
        left = tileLeft; top = tileTop;
        first_z = StoredDepth::Bits(z[0]);
        const auto PB = cfg.productBits;
        const auto SB = cfg.sumBits;
        const auto RE = cfg.reach;
        Fp X[3], Y[3], Z[3];
        const bool ranged = cfg.range != 0;
        auto inf = [](bool neg) { Fp r; r.bad = true; r.neg = neg; return r; };
        auto zero = [](bool neg) { Fp r; r.neg = neg; return r; };
        auto load = [&](uint32_t u) { return Fp::FromSingle(u, ranged); };
        auto limit = [&](const Fp& r, bool sumSign) {
            if (!ranged || r.zero()) return r;
            if (r.top() - 1 > 127) return inf(r.neg);
            if (r.top() - 1 < -126) return zero(sumSign ? false : r.neg);
            return r;
        };
        auto mul = [&](const Fp& p, const Fp& q) {
            if (ranged && (p.bad || q.bad)) {
                const Fp& other = p.bad ? q : p;
                return (!other.bad && other.zero()) ? zero(p.neg != q.neg) : inf(p.neg != q.neg);
            }
            return limit(Mul(p, q, PB), false);
        };
        auto add = [&](const Fp& p, const Fp& q) {
            if (ranged && (p.bad || q.bad)) {
                if (p.bad && q.bad && p.neg != q.neg) return zero(false);
                return inf(p.bad ? p.neg : q.neg);
            }
            return limit(Add(p, q, SB, RE), true);
        };
        auto sub = [&](const Fp& p, const Fp& q) { return add(p, -q); };
        for (int k = 0; k < 3; k++) {
            X[k] = load(x[k]); Y[k] = load(y[k]); Z[k] = load(z[k]);
        }
        auto edge = [&](int j, int k) { return sub(mul(X[j], Y[k]), mul(X[k], Y[j])); };
        Fp e1 = edge(1, 2), e2 = edge(2, 0), e3 = edge(0, 1);
        auto sum3 = [&](const Fp& p, const Fp& q, const Fp& r) { return add(add(p, q), r); };
        Fp a = sum3(mul(Z[0], sub(Y[1], Y[2])), mul(Z[1], sub(Y[2], Y[0])), mul(Z[2], sub(Y[0], Y[1])));
        Fp b = sum3(mul(Z[0], sub(X[2], X[1])), mul(Z[1], sub(X[0], X[2])), mul(Z[2], sub(X[1], X[0])));
        Fp c = sum3(mul(Z[0], e1), mul(Z[2], e3), mul(Z[1], e2));       // (v1 + v3) + v2
        Fp det = sum3(e1, e2, e3);
        if (ranged && det.bad) {
            // An infinite determinant (corners at 2^64 and beyond): its reciprocal is taken to be zero, and zero
            // times anything is zero, so the depth is 0. Not measured; dreamcast-tests/pvr-tests/huge does not tell
            // (its triangles of that size are drawn whatever their depth), and one pixel of
            // dreamcast-tests/pvr-tests/modvol, where a polygon of garbage with such corners decides whether it is in a
            // volume, has it behind 0.75.
            valid = false; block = false;
            first_z = StoredDepth();
            return;
        }
        if (!det.bad && !det.zero() && (pvrfp::perpVal.bad || !pvrfp::perpVal.zero()) && pvrfp::AbsAtMost(det, pvrfp::perpVal))
            det = Fp();                                 // FPU_PERP_VAL: as a determinant of zero
        if (ranged && !det.zero() && !det.bad) {
            // the depth of the tile's first pixel with these numbers; an overflow is stored as 2^127 (Saturated)
            Fp inv0 = Recip(det, cfg.recipBits);
            Fp x00 = Fp::FromHalves(2 * (int64_t)left + 1), y00 = Fp::FromHalves(2 * (int64_t)top + 1);
            Fp first0 = mul(add(add(mul(a, x00), mul(b, y00)), c), inv0);
            if (first0.bad || a.bad || b.bad || c.bad) {
                valid = false; block = false;
                first_z = first0.bad ? StoredDepth::Saturated(first0.neg) : StoredDepth::FromFp(first0.cut(cfg.storedBits));
                // The depth unit evaluates such a plane all the same. When a slope overflows (from about 32 z = 2^128
                // for a cell-sized triangle) its step is +-2^127 a pixel from the plane's start, in a register whose
                // sign is at 2^131, so the steps wrap every 32 pixels from the tile's first pixel; the word stored is
                // a function of the register alone (WrapWord). When only the constant overflows (it holds the corners'
                // x y: from about z = 2^114 .. 2^117 on the screen), the tile's first pixel is 0 and every other one
                // the negative full scale of the block, -(2^(e + 1) - ulp), e the block's exponent from the slopes
                // (pair + 5). A plane of zero slopes (all corners bad, flat) is the saturated 2^127 above (bgbig).
                if (a.bad || b.bad) {
                    ovf = 2;
                    sa = a.bad ? ((a.neg != det.neg) ? -1 : 1) : 0;
                    sb = b.bad ? ((b.neg != det.neg) ? -1 : 1) : 0;
                    // The register starts at the plane's start, first0 above, with the infinities' rules: an infinity
                    // is one step with its sign (2^127, the slopes' step; its NaN payload does not count), a finite
                    // start is kept as a number, its bits from 2^102 up. dreamcast-tests/pvr-tests/nantr_pos (300 cells
                    // of one bad corner, every corner bad in turn, both orientations, NaNs 7fc00000 7fa00000 7f800001
                    // 7fffffff ffc00000 ffffffff, +-inf, +-max, 2^127, 2^123; 9 pages of backgrounds): 2700 of 2700
                    // regions, 70 cells start at +-1 step (refsw3 before: 2140 regions). That was zbad / zbadbg's one
                    // cell (TR at a NaN at x = 0: TR's term of c is NaN * 0 there, so c is finite and the start a x0 is
                    // +inf); TR or BL bad with the edge facing it on a line through the origin (x = 0, y = 0, the
                    // diagonal) is the same everywhere. The start is (a x0 + b y0) + c: TL bad, (-inf - inf) + inf, is 0
                    // (zword), and a TR / BL corner facing the diagonal starts at the sign of c, flipping across it.
                    // nantr_word: the words, to 19 bits, of 8 such triangles, 31 pixels each, those of 1 step on.
                    // nantr_fin (16 starts at the tile's first pixel, the slopes' infinities cancelling): 2^120 .. the
                    // largest float as themselves (2^120 as 2^120 - 2^102), 0.5 and 2^100 as 0; nantr_fin2,
                    // nantr_wreg: steps added to such starts (WrapWord). GAP: a finite start below zero is taken
                    // toward zero to 2^102 (no test tells), and is cut to the start's 20 bits first, as an ordinary one.
                    if (first0.bad) {
                        r0 = first0.neg ? -(int64_t(1) << 25) : (int64_t(1) << 25);
                    } else if (!first0.zero()) {
                        const Fp st = first0.cut(cfg.startBits);
                        const int sh = st.exp - 102;
                        const int64_t m = sh >= 0 ? (sh < 40 ? (int64_t)(st.mant << sh) : 0) : (-sh < 64 ? (int64_t)(st.mant >> -sh) : 0);
                        r0 = st.neg ? -m : m;
                    }
                } else if (!a.zero() || !b.zero()) {
                    const Fp invS = Recip(det, cfg.recipBits);
                    const Fp As = Mul(a, invS, cfg.coefBits), Bs = Mul(b, invS, cfg.coefBits);
                    const int pairS = As.top() > Bs.top() ? As.top() : Bs.top();
                    const int eS = pairS + (cfg.tileBits - cfg.pairBits);
                    ovf = 1;
                    // GAP: the sign (measured below zero with slopes below zero only), and the low 5 bits
                    ovfWord = StoredDepth::Bits((As.neg || (As.zero() && Bs.neg) ? 0x80000000u : 0) |
                                                (uint32_t)(eS + 127) << 23 | 0x7fffffu);
                }
                return;
            }
        }
        valid = !det.zero() && !det.bad && !a.bad && !b.bad && !c.bad;
        block = false;
        if (!valid) {
            // an infinity or a NaN among the inputs: the first vertex's depth as given. A determinant of zero:
            // the mean of the three depths (as a console stores it; the arithmetic of the mean is a guess)
            if (det.zero() && !det.bad && !Z[0].bad && !Z[1].bad && !Z[2].bad) {
                Fp third = Recip(Fp::FromHalves(6), cfg.recipBits);
                Fp mean = Mul(sum3(Z[0], Z[1], Z[2]), third, cfg.coefBits);
                first_z = StoredDepth::FromFp(mean.cut(cfg.storedBits));
            }
            return;
        }
        Fp inv = Recip(det, cfg.recipBits);
        A = Mul(a, inv, cfg.coefBits);
        B = Mul(b, inv, cfg.coefBits);
        C = Mul(c, inv, cfg.coefBits);
        Fp x0 = Fp::FromHalves(2 * (int64_t)left + 1), y0 = Fp::FromHalves(2 * (int64_t)top + 1);
        Fp first = Mul(Add(Add(Mul(a, x0, PB), Mul(b, y0, PB), SB, RE), c, SB, RE), inv, cfg.coefBits);
        if (ranged) {
            // The range holds for the coefficients and the tile's start too: below 2^-126 they are zero
            // (dreamcast-tests/pvr-tests/zspec pages 3-6: a plane from 2^-124 to -2^-124 over a cell, slopes of
            // 2^-128 a pixel, is stored as its start everywhere; one from 2^-126 to -2^-126, start below 2^-126, as 0)
            auto under = [](Fp& v) { if (!v.zero() && v.top() - 1 < -126) v.mant = 0; };
            under(A); under(B); under(C); under(first);
        }
        int pair = A.top() > B.top() ? A.top() : B.top();
        if (first.zero() && pair == pvrfp::NoExp) {
            // no start and no slopes (a plane of zero, or one whose start and slopes are below 2^-126 and so zero:
            // zspec page 3-6, the plane from 2^-126 to -2^-126): 0 at every pixel
            valid = false;
            first_z = StoredDepth();
            return;
        }
        // A start of exactly zero is stepped from zero all the same, on the slopes' own grid (21 bits under the
        // larger one's exponent): dreamcast-tests/pvr-tests/zmeas, mix reach, 30 of 30 readings (evaluating the plane
        // at each pixel in floating point, as before, gave 22)
        int e = first.zero() ? pair + (cfg.tileBits - cfg.pairBits) : first.top();
        Fp start = first.cut(cfg.startBits);
        if (pair != pvrfp::NoExp && (cfg.startOnSlopes == 1 || (cfg.startOnSlopes == 2 && start.neg)))
            start = start.cutBelow(pair - cfg.pairBits);
        unit = e - cfg.tileBits;
        auto step = [&](const Fp& v) -> pvrfp::i128 {
            if (v.zero())
                return 0;
            Fp s = v.cut(cfg.slopeBits).cutBelow(pair - cfg.pairBits).cutBelow(unit);
            if (s.zero())
                return 0;
            pvrfp::i128 m = (pvrfp::i128)s.mant << (s.exp - unit);
            return s.neg ? -m : m;
        };
        if (pair != pvrfp::NoExp && pair - cfg.pairBits + 24 - unit > 100)
            return;                                     // a slope 2^50 times the start: leave it to the float form
        pvrfp::i128 s0 = start.zero() ? 0 : (pvrfp::i128)start.mant << (start.exp - unit);
        d0 = 2 * (start.neg ? -s0 : s0);
        dx = step(A);
        dy = step(B);
        block = true;
    }

    // The depth stored for the pixel i columns right and j rows down of the tile's first pixel; centers: the
    // ISP samples at pixel centers (HALF_OFFSET bit 0).
    // The words of an overflowing plane's register (ovf 2) at whole steps, by m = (r / 2^127) mod 32 (r0 = 0: m = (sa i +
    // sb j) mod 32): dreamcast-tests/pvr-tests/zword (the words, to the stored depth's 19 bits), the same for +inf, NaN
    // and 2^127 corners at every place tried, and the mirror (steps of +2^127) word for word. GAP: where zword says a
    // word lies between two probes 32 apart the low 5 bits are taken as ones (m 17 .. 22, 24 .. 29), m 2 .. 15 are
    // beyond the largest float (any word above 0x7f7fffe0: taken as 0x7fffffff) and m 16 below every probe (taken as
    // the lowest word).
    static constexpr uint32_t kWrap[32] = {
        0x00000000u, 0x7f000000u, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu,
        0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu,
        0x807fffffu, 0xfdffffffu, 0xfe7fffffu, 0xfe7fffffu, 0xfeffffffu, 0xfeffffffu, 0xfeffffffu, 0xfeffffe0u,
        0xff7fffffu, 0xff7fffffu, 0xff7fffffu, 0xff7fffffu, 0xff7fffffu, 0xff7fffffu, 0xff7fffe0u, 0xff000000u,
    };
    // The word stored for the overflowing plane's register (ovf 2) between whole steps: r in units of 2^102, two's
    // complement with its sign at 2^131 (it wraps every 32 steps of 2^127). Zero is 0; above zero it is r itself, cut to
    // 19 bits, from 2^128 on beyond the largest float (0x7fffffff); below zero, with u = r + 2^131 (the bits under the
    // sign) cut to 19 bits and 2^p its leading one, the exponent field is p + 124 and the ISP's compare key's fraction
    // (the word's fraction inverted) is v = (u / 2^p - 1.75) 2^26 - 1: below 0 the binade's lowest word, from 2^23 on the
    // next binade's lowest word. So from -2 steps to -1 step the word is r itself, from -9 to -8.5 steps r + 8 steps,
    // from -12.5 to -12.25 r + 12 steps, ..., and in between the binades' lowest words (monotone in r). At whole steps
    // the formula gives each of kWrap's 32 words (zword). Between them: dreamcast-tests/pvr-tests/nantr_fin (16 starts at
    // the tile's first pixel), nantr_fin2 (8 starts with 0 .. +-15 steps, 31 pixels each) and nantr_wreg (8 starts of
    // up to 1/8 step with 0 .. -12 steps, 14 pixels each), every word as the formula where the probes pin one: 2^127 +
    // 2^110 as itself, -(2^127 + 2^110) as 0xff000100 (u's 19 bits), 2^119 - 9 steps as 0xfeff0000, 1/8 - 9 steps as
    // 0xfee00100, 1/8 - 2 steps as 0xff700100; -8.125, -8.0625, -8.09375, -12.125, -12.09375 steps with the lowest words
    // of -8 and -12 (not above them: v from 2^23 on); -2^119 and 2^119 - 1 step between -2^127 and 0.25.
    // GAP: the low 5 bits where zword's words lie between two probes (v below -1 and from 2^23 on: taken as ones in the
    // word, as kWrap; v = -1: 0x7fffe0 as zword's -9 and -2); the words beyond the largest float (taken as 0x7fffffff);
    // between -1 step and 0 (exponent field 255: above -2^127 and below 0.25 in the ISP's order, nothing finer).
    static uint32_t WrapWord(int64_t r)
    {
        r = ((r + (int64_t(1) << 29)) & ((int64_t(1) << 30) - 1)) - (int64_t(1) << 29);
        if (r == 0)
            return 0;
        if (r > 0) {
            if (r >= (int64_t(1) << 26))
                return 0x7fffffffu;
            const int p = Fp::Bits((uint64_t)r) - 1;
            const uint64_t m = p <= 23 ? (uint64_t)r << (23 - p) : (uint64_t)r >> (p - 23);
            return (uint32_t)(p + 102 + 127) << 23 | ((uint32_t)m & 0x7fffe0u);
        }
        int64_t u = r + (int64_t(1) << 29);
        if (u == 0)
            return 0x807fffffu;                         // -16 steps: exponent field 0, the lowest word (zword)
        const int p = Fp::Bits((uint64_t)u) - 1;
        if (p > 18)
            u &= ~((int64_t(1) << (p - 18)) - 1);
        const int64_t v = ((u << 26) >> p) - 7 * (int64_t(1) << 24) - 1;
        uint32_t e = (uint32_t)(p + 226);
        if (v < -1)
            return 0x80000000u | e << 23 | 0x7fffffu;
        if (v == -1)
            return 0x80000000u | e << 23 | 0x7fffe0u;
        if (v >= (int64_t(1) << 23))
            return 0x80000000u | (e + 1) << 23 | 0x7fffffu;
        return 0x80000000u | e << 23 | ((uint32_t)~v & 0x7fffffu);
    }

    StoredDepth At(int i, int j, bool centers) const
    {
        using namespace pvrfp;
        if (ovf == 2) {
            const int64_t r = r0 + (int64_t)(sa * i + sb * j) * (int64_t(1) << 25);
            return StoredDepth::Bits((r & ((int64_t(1) << 25) - 1)) ? WrapWord(r) : kWrap[(r >> 25) & 31]);
        }
        if (ovf == 1)
            return (i == 0 && j == 0) ? StoredDepth() : ovfWord;
        if (!valid)
            return first_z;
        const int h = centers ? 0 : -1;
        Fp v;
        if (block) {
            pvrfp::i128 t = d0 + (2 * i + h) * dx + (2 * j + h) * dy;
            v.neg = t < 0;
            pvrfp::u128 m = v.neg ? (pvrfp::u128)(-t) : (pvrfp::u128)t;
            int n = (uint64_t)(m >> 64) ? 64 + Fp::Bits((uint64_t)(m >> 64)) : Fp::Bits((uint64_t)m);
            int drop = n > 64 ? n - 64 : 0;
            v.mant = (uint64_t)(m >> drop);
            v.exp = unit - 1 + drop;
        } else {
            const auto PB = cfg.productBits;
            const auto SB = cfg.sumBits;
            const auto RE = cfg.reach;
            Fp X = Fp::FromHalves(2 * (int64_t)(left + i) + (centers ? 1 : 0)), Y = Fp::FromHalves(2 * (int64_t)(top + j) + (centers ? 1 : 0));
            v = Add(Add(Mul(A, X, PB), Mul(B, Y, PB), SB, RE), C, SB, RE);
        }
        return StoredDepth::FromFp(v.cut(cfg.storedBits));
    }
};

using DepthTile = DepthTileT<DepthConfig>;    // the run-time form, for the probes and generators

} // namespace pvrisp
