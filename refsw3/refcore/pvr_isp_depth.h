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
      - a pixel is the start plus whole steps, and is stored with a 19 bit mantissa (below zero as the block's
        exponent and 16 bits under it, rounded up in size: StoredDepth::Neg); the steps are fixed point
        however far the depth reaches above the start (zmeas mix reach: exact to 29 powers of two above it), and a
        start of exactly zero is stepped the same way, on the slopes' own grid
      - a determinant of ZERO (three equal corners, three corners on a line, or a sliver whose constants cancel):
        every pixel gets the mean of the three corners' depths, whatever the constants are
        (dreamcast-tests/pvr-tests/covmeas, mixes degen, tiny and sliver with --prim zprobe --zs: 11,456 + 1,344 + 53
        pixels, brackets 0.02 to 0.1 wide); the mean is (z1 + z2) + z3 in the setup's adder (z3 the third corner
        sent, in strips and sprites too), divided by 3 toward zero, zero below 2^-126, stored as a flat plane's start
        (pvr-tests/perpmean_word: 16,384 stored words of 16,384; perpmean_strip, perpmean_tiny)
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
    // for sums and products of them. With them dreamcast-tests/pvr-tests/huge is the console's in 250 of 250 regions
    // (228 without), including the quads of which a console draws one triangle only at 2^127, the largest number,
    // infinity and NaN (the two triangles' sums come to infinities of opposite sign). The limit at 2^128 is the same
    // for every product and every sum of the depth setup, each checked where it is made (after the adder's
    // truncation; the reach does not decide it: a reach of 40 changes no zovf test): dreamcast-tests/pvr-tests/zovf_lim (a term of the constant at 2^128 - 2^104, 2^128,
    // 2^128 + 2^105 .. 1.5 2^128, as a product with the others beyond the adder's reach, as a sum of two products
    // below 2^128, a product past 2^128 whose sum comes back below it; 4984 of 4984 regions), zovf_c (the constant
    // alone at +-(1 + 2^-22 .. 1 + 2^-2) 2^128, as one product or as a sum, 352 regions), zovf_ps (the slopes: one
    // product at 2^128 with the sum below, products below 2^128 with the sum at it; 2^128 - 2^104 not: it is
    // zovf_ps that pins the check of a product at exactly 2^128), zovf (the six cells with the constant exactly
    // 2^128; they would be the same with the product kept finite, the sum c reaching 2^128 too), zovf_sum (the
    // start's last sum and last product alone, below; the last sum's order; the constant's order (v1 + v3) + v2
    // rests on zmeas), zovf_far, zovf_ab (13 x 13 slope values from 0 to 2^132 of both signs). range 0: no limits, as
    // before.
    static constexpr pvrfp::C<1> range{};
    // The zero rule of the setup's additions (Setup, add) in the coordinate differences (1) or not (0), and in the
    // edges and the determinant (1) or not (0): 1 and 1 measured (dreamcast-tests/pvr-tests/depth2_zcoord,
    // depth2_zedge); 0 is kept for their generators.
    static constexpr pvrfp::C<1> zeroRuleCoords{};
    static constexpr pvrfp::C<1> zeroRuleEdges{};
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
    int zeroRuleCoords = DepthHW::zeroRuleCoords;
    int zeroRuleEdges = DepthHW::zeroRuleEdges;
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
// below zero drawn like any other (uvmeas wneg). Below 2^-126 a depth above zero is stored as a float's denormal and
// ordered as one, by value: dreamcast-tests/pvr-tests/depth2_denorm (70 pairs of planes over a tile each, slopes of
// 2^-126 .. 2^-125, a diagonal of each tile between 0 and 2^-126, from about 2^-138 up, the second plane 0 .. a few
// hundred 2^-145 off the first, Greater, Equal, Less: 2,137 pixel pairs with a depth there; their depths are on the
// start's grid of 2^-145, the finest a depth there can have, and a store on a grid of 2^-144 would differ at 39
// pairs, 2^-143 at 151, .. 2^-128 at 1,927, rounded to 2^-144 at 68, one word for all at 1,930; and 70 pairs whose
// second plane is the first moved by 2^-141 .. 2^-126, 1,976 pixel pairs in different binades, 2^-128 against
// 2^-127 against 1.5 2^-127 ..: a word of exponent field 0 with the normalized fraction would differ at 463; 420 of
// 420 regions as refsw3); above 0 and below 2^-126 at every pixel: dreamcast-tests/pvr-tests/depthgaps_edges (a ramp from 2^-124 in steps of -1.75 and +1 2^-126:
// 2^-128 and 2^-127 above 0 and below 2^-126 at every pixel, 0 where the plane is 0), depthgaps_tiny (flat quads at
// +-2^-126, +-1.5 2^-126, +-2^-125 ..: above / below 0); slopes and starts are multiples of 2^-145 at least, so a
// depth there has at most 19 bits and the denormal's own grid is never reached. From 2^128 a depth above zero is
// above every float word (StoredDepth::Big).
// A depth below zero is not stored as a float: the depth unit keeps it as its block's exponent E and a fixed point
// value M 2^(E - 16) (StoredDepth::Neg; DepthTileT::NegWord, StoredDepth::Flat). Within the block's top binade that
// is the float's word and order again; outside it the depth lies between the words of two neighbouring fields. k is
// the order with those: the word's order, 32 bits finer.
struct StoredDepth {
    // the word; for a depth below zero outside its block's top binade (between two fields' words) the float word of
    // its value M 2^(E - 16) (24 bits, toward zero; -2^127 from 2^128 on), and for a depth above every float word
    // (Big) 0x7fffffff: for logs and the probes of dreamcast-tests/model only, the order is k
    uint32_t bits = 0;
    int64_t k = 0;                                      // the ISP's order
    static int32_t Key32(uint32_t b)
    {
        if (b == 0x80000000u)
            return 0;
        return (int32_t)(b ^ ((b >> 31) ? 0x007fffffu : 0));
    }
    static StoredDepth Bits(uint32_t b) { StoredDepth d; d.bits = b; d.k = (int64_t)Key32(b) * ((int64_t)1 << 32); return d; }
    // between the word w and the next one in the ISP's order, at off (1 .. 2^32 - 1)
    static StoredDepth Between(uint32_t w, int64_t off, uint32_t standIn)
    {
        StoredDepth d;
        d.bits = standIn;
        d.k = (int64_t)Key32(w) * ((int64_t)1 << 32) + off;
        return d;
    }
    // The stored depth of block exponent E and value M 2^(E - 16), M < 0 (the value rounded down: its size up).
    // Inside the window, the block's top binade (M from -2^16 to -2^15: sizes from 2^(E - 1) to 2^E), it is the float
    // of that value: 16 significant bits, a multiple of 0x100 in the word, and the window's bottom (2^E) the word
    // with the fraction 0x7fffe0 (no carry into the exponent). Outside it M goes on, and the depth lies between the
    // words of two neighbouring fields in the ISP's order: above every word of field E + 126 and below the last one
    // of field E + 127 (above the window), above every word of field E + 125 and below the last one of field E + 126
    // (below it), there above everything from the block below.
    // The window (16 bits, rounded up in size, no carry): dreamcast-tests/pvr-tests/perpmean_bg (the words of 136
    // means below zero and 24 flat 2x2 quads, read with the background as the probe: every one on the 16 bit grid,
    // 0x...00, one step above in size wherever the value had bits below it), perpmean_neg (11,258 means below zero,
    // read with zexfil.h's probes, whose own negative depths are rounded the same way: all consistent with it),
    // perpmean_negstore page 0 (flat quads at -0x42ffffff and a start just over -2^14 tie with the backgrounds
    // 0xc2ffffe0 and 0xc67fffe0; perpmean_neg: the 49 means and probes at the top of a binade; a carry to the next
    // power of two puts the probes out of order), perpmean_negedge pages 0 (16 planes of slopes of 1 .. 16 units of
    // the tile's grid, the pixel in the start's binade) and 2 (64 flat quads), depthgaps_negpp, depthgaps_neggrid
    // (flat polygons at negative depths 1 .. 12 steps of 0x20 apart, zexfil's exact probes: 35 depths of fields 5 ..
    // 227 tie in groups of 0x100, the top group with 0x7fffe0; positive ones do not).
    // Outside it: depthgaps_negladder and depthgaps_search (a ramp from a start of exactly 0, A = -0.71, B = -0.05 a
    // pixel, every pixel of tile (0, 0) read out: E = 5, the 326 pixels in [16, 32) stored rounded up, the 697
    // below 16 above every probe of field 131 and below field 132's last), depthgaps_negconv (12 planes read out:
    // starts of -100, -1000, -96, -64, -127, -3, +-10 with slopes from 2^-12 to 2; E from the start (7, 10) or from
    // the slopes (6 for a start of -9.25 with slopes of 1; 2 for -0.04 with 0.075): the window, above and below it
    // at every pixel), depthgaps_ties and depthgaps_uwmat (ramps against ramps: depths above the window of E 6 and
    // below the window of E 7, both between fields 132 and 133, ordered by M in each, every one of E 6 below every
    // one of E 7; 10 pairs of ramps, every pair of 32 depths: strictly ordered as their values), depthgaps_final G
    // (the grid there: a ramp 2^-10 above another, E 6, above its window, is above it at all 32 x 32 pixels, 2^-11
    // equal; E 7 below its window: 2^-9 above, 2^-10 equal: 2^(E - 16) on both sides), perpmean_negslope,
    // perpmean_negstore pages 1 .. 3, perpmean_negedge page 1 (pixels of sloped planes below zero outside the
    // start's binade: neither the 16 bit rounding up nor the 19 bit cut of their own binades).
    static StoredDepth Neg(int E, int64_t M)
    {
        const uint32_t sign = 0x80000000u, top = (uint32_t)(E + 126) << 23;
        if (M >= -(1 << 16) && M <= -(1 << 15))
            return Bits(sign | top | (M == -(1 << 16) ? 0x7fffe0u : (uint32_t)(-M - (1 << 15)) << 8));
        Fp v;
        v.neg = true;
        v.mant = (uint64_t)-M;
        v.exp = E - 16;
        const uint32_t standIn = FromFp(v.cut(24)).bits;
        if (M > -(1 << 15))                             // above the window: after every word of field E + 126
            return Between(sign | top, M + (1 << 15), standIn);
        // below the window: after every word of field E + 125, after what the block below has there
        const int64_t off = ((int64_t)1 << 31) + (M + ((int64_t)1 << 30) > 0 ? M + ((int64_t)1 << 30) : 0);
        return Between(sign | (top - (1u << 23)), off, standIn);
    }
    // A flat depth v as the depth unit stores one (a plane with no slopes, a determinant-zero mean: its block's
    // exponent is v's own), an infinity or a NaN as Saturated. Above zero: the float, cut to storedBits. Below zero:
    // cut to startBits (a plane's start), then Neg on the grid 2^(E - 16) of E = v's exponent (not below -122, not
    // above 128: DepthTileT::Setup), the size rounded up.
    static StoredDepth Flat(const Fp& v, int startBits, int storedBits)
    {
        if (v.bad)
            return Saturated(v.neg);
        if (v.zero())
            return StoredDepth();
        if (!v.neg)
            return FromFp(v.cut(storedBits));
        const Fp s = v.cut(startBits);
        int E = s.top();
        if (E < -122)
            E = -122;
        if (E > 128)
            E = 128;
        // M = floor(s / 2^(E - 16)) = -ceil(|s| / 2^(E - 16))
        const int sh = E - 16 - s.exp;
        int64_t mag;
        if (sh <= 0)
            mag = (int64_t)(s.mant << -sh);
        else if (sh >= 64)
            mag = 1;
        else
            mag = (int64_t)(s.mant >> sh) + ((s.mant & ((1ull << sh) - 1)) ? 1 : 0);
        return Neg(E, -mag);
    }
    int64_t key() const { return k; }
    friend bool operator<(StoredDepth a, StoredDepth b) { return a.key() < b.key(); }
    friend bool operator>(StoredDepth a, StoredDepth b) { return b < a; }
    friend bool operator<=(StoredDepth a, StoredDepth b) { return a.key() <= b.key(); }
    friend bool operator>=(StoredDepth a, StoredDepth b) { return b <= a; }
    friend bool operator==(StoredDepth a, StoredDepth b) { return a.key() == b.key(); }
    friend bool operator!=(StoredDepth a, StoredDepth b) { return !(a == b); }

    // A depth above zero of 2^128 or more from the depth unit's steps (a start of 2^127, the constant's overflow,
    // stepping up) is above every float word and ordered as a float of its 19 bits whose exponent field goes on past
    // 254: dreamcast-tests/pvr-tests/depthgaps_bigpos (triangles whose start is -2^127 stepping by 1.9375 2^123
    // .. 2^126 a pixel, every pixel read with the background as the probe: from 1.07 2^128 to 13 2^127 above every
    // probe, 0x7f7fffe0 included; those below 2^128 the floats, 20 of 20 regions; refsw3 had 2^127 (Saturated)),
    // depthgaps_bigord (two such triangles over each other, Greater, Equal, Less: ordered by value from 1.02 2^128
    // to 12 2^127; slopes 2^112 apart (2^116.7 at the pixel) are, 2^100 apart (2^104.7) equal; above a flat triangle
    // at the largest float or at +infinity; 33 of 33 regions). Its 19 bits are cut, as below 2^128:
    // dreamcast-tests/pvr-tests/depth2_biggrid (10 sets of 26 pairs of such triangles in tile (0, 0), legs 4, the
    // second's slopes 2^104 .. 2^110 off the first's: 1,251 pixel pairs at 2^128 .. 1.7 2^129, Greater, Equal, Less,
    // 30 of 30 regions; 18 bits cut differ at 129 pairs, 20 at 105, 13 / 23 at 679 / 269, 19 rounded at 216, a fixed
    // grid of 2^110 at 22). The same for an overflowing register's values (DepthTileT::RegDepth): depthgaps_mm orders
    // them at whole steps, dreamcast-tests/pvr-tests/depth2_regmix against these (RegDepth).
    // v: above 0, 2^128 or more, at most 19 bits.
    static StoredDepth Big(const Fp& v)
    {
        const int n = Fp::Bits(v.mant);
        const uint64_t frac = (n <= 24 ? v.mant << (24 - n) : v.mant >> (n - 24)) & 0x7fffffu;
        const int64_t field = v.top() - 1 + 127;
        return Between(0x7f800000u, ((field - 255) << 23 | (int64_t)frac) + 1, 0x7fffffffu);
    }

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
    // A plane whose slope overflowed (dreamcast-tests/pvr-tests/zword, zbad, zbadbg, zbadk, zovf; README zspec):
    // ovf 2, every pixel's depth is RegDepth(r) of the register r = r0 + (sa i + sb j) 2^25 + i fa + j fb, i, j from
    // the tile's first pixel, sa, sb the overflowed slopes' signs (0: that slope did not overflow), fa, fb the slopes
    // that did not overflow, r0 the plane's start (below), all in units of 2^102 (2^25: one step, 2^127).
    int ovf = 0, sa = 0, sb = 0;
    int64_t r0 = 0, fa = 0, fb = 0;
    StoredDepth first_z;                                // not valid: the depth every pixel gets
    // block form: twice the depth in units of 2^unit is d0 + sx2 * dx + sy2 * dy, with sx2, sy2 the steps from
    // the tile's first pixel center in half pixels
    pvrfp::i128 d0 = 0, dx = 0, dy = 0;
    int unit = 0;
    // the block's exponent as the store below zero has it: max(the start's, the larger slope's + 5, -122), at most
    // 128 (NegWord)
    int E = 0;
    // float form (a plane of zero)
    Fp A, B, C;
    int left = 0, top = 0;
    bool startCenters = true;                           // the start is the plane at the tile's first pixel centre (else corner)
    Cfg cfg;

    // x, y, z of the three corners in rasterizer order, the bits of IEEE singles as the TA gives them; (left, top):
    // the tile's first pixel
    void Setup(const uint32_t x[3], const uint32_t y[3], const uint32_t z[3], int tileLeft, int tileTop, const Cfg& config, bool centers = true)
    {
        using namespace pvrfp;
        *this = DepthTileT{};                           // nothing of a previous triangle's (overflow, register, block)
        cfg = config;
        left = tileLeft; top = tileTop;
        startCenters = centers;
        // The plane's start is evaluated at the tile's first sample point: its centre, or with HALF_OFFSET bit 0 clear
        // its top left corner (x0 = left, y0 = top), and the pixels are whole steps from there. Measured where it
        // shows, in the overflow regimes: dreamcast-tests/pvr-tests/depth2_halfc (HALF_OFFSET 7 and 6, background
        // probes 0 Greater / Less, 2^127, -2^127 Equal, 2^120, the largest float, -2^126 Greater): in tile (0, 0),
        // where x0 = y0 = 0 at the corner makes an infinite slope's term 0 (bad x 0 = 0), zword's TL +inf triangle
        // starts at +1 step (c = +inf alone: m = 1 - (i + j)) and zovf_creg's C0 at -1 step (c = -2^128), at every
        // pixel of the 14 regions (7 probes each) that differ from a start at the centre; nantr_fin2's (c / det
        // finite either way), the same triangles in tiles (2, 0) .. (8, 0) (x0 > 0: the same infinities) and two
        // ordinary ramps the same as at the centre (280 regions: no half steps of the register). For an ordinary
        // plane too: dreamcast-tests/pvr-tests/depth2_halfo (8 random ordinary triangles over tile (0, 0), HALF_OFFSET
        // 6, the words of 5 pixels each read by background probes, where the start at the corner and the start at
        // the centre with half steps back, refsw3 before (At's h), store other words, mostly 1 unit of the 19 bits:
        // the corner's at all 40, 16 of 16 regions; in random triangles the two differ at 39 % of the pixels).
        const int64_t sampleHalf = centers ? 1 : 0;
        first_z = StoredDepth::Bits(z[0]);
        const auto PB = cfg.productBits;
        const auto SB = cfg.sumBits;
        const auto RE = cfg.reach;
        Fp X[3], Y[3], Z[3];
        const bool ranged = cfg.range != 0;
        bool forced = false;                            // the start as the overflow has it (below)
        Fp forcedFirst;
        Fp rangedFirst;                                 // the start in the setup's range (below)
        bool haveRangedFirst = false;
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
        // An addition of the setup's. A sum of zero is +0, -0 + -0 too. A zero added to a number x of 2^-126 ..
        // 1.5 2^-126 in size whose sign is not the zero's is +0 (as if the zero were 2^-127 with its sign and the sum
        // then below the range; the TSP's mean adds a zero so, pvr_tsp_plane.h withZero); to any other x it is x, to
        // the bit. A zero is one from an input, a product below 2^-126 with its sign, or a sum. In every addition:
        // dreamcast-tests/pvr-tests/depthgaps_zadd (32 slivers read out to the bit, one triangle per case, nothing
        // else near 2^-126; 12 flat probes against quads at 0, z and z + 2^-126): the slope's last addition, +0 +
        // t3: t3 = -2^-126, -1.25 2^-126, -(1.5 - 2^-22) 2^-126 give a slope of 0, -1.5, -1.75, -(2 - 2^-22) and
        // +1 .. +1.75 (2^-126) themselves exactly (not 2^-127 more or less: the TSP's planes' rule, 76 of 109
        // regions); -0 + -0 is +0 (as -0: 108 of 109); zeros from products below 2^-126 the same; the first
        // addition, -0 + 1.25 2^-126 is +0, +0 + 1.25 and -0 + 1.75 are kept (without the rule there: 108 of 109);
        // the tile's start +0 + c: c = -(1 .. 1.5 - 2^-10) 2^-126 starts at 0, -(1.5 .. 2 - 2^-9) and +1 .. +1.75 at
        // c / det exactly; 109 of 109 regions (a zero as a zero: 84). depthgaps_field1 (1000 of zexfil's flat
        // probes at depths of exponent fields 0 .. 5, most of field 1, against 0 and against a probe of the same
        // shape at another depth): 2000 of 2000 orders with this (1642 with a zero as a zero, 1944 with the TSP's
        // planes' rule in the last additions, 1972 with -0 + -0 = -0, 1996 without the rule in the first additions).
        // The coordinate differences (y1 - y2 .., x2 - x1 ..) go through the same adder, with the rule:
        // dreamcast-tests/pvr-tests/depth2_zcoord (16 triangles near the origin, one coordinate of two corners among
        // +-0 and +-(1 .. 1.5) 2^-126, where such a difference multiplies a depth of 2^126 and more or an infinity:
        // the product a zero, or an infinity times zero 0, with the rule; refsw3 without it there (zeroRuleCoords 0)
        // stores other words at all 2,599 pixels; 5 background probes each: 80 of 80 regions with it, 4,558 probe
        // pixels on 49 pages that tell the two apart). So do the edges x_j y_k - x_k y_j and the determinant's sums:
        // dreamcast-tests/pvr-tests/depth2_zedge (13 triangles near the origin, tiny and signed-zero coordinates,
        // an infinite corner: an edge of +0 against one of +-(1 .. 1.5) 2^-126 makes the infinite depth's term of the
        // constant 0 or an infinity; refsw3 without the rule there (zeroRuleEdges 0) stores other words at all 160
        // pixels; 65 of 65 regions with it, 110 probe pixels on 29 pages that tell the two apart).
        // rule false: the same addition without the zero rule (zeroRuleCoords, zeroRuleEdges 0: not the hardware's)
        auto addRule = [&](const Fp& p, const Fp& q, bool rule) {
            if (ranged && (p.bad || q.bad)) {
                if (p.bad && q.bad && p.neg != q.neg) return zero(false);
                return inf(p.bad ? p.neg : q.neg);
            }
            if (rule && ranged && p.zero() != q.zero()) {
                const Fp& v = p.zero() ? q : p;
                const bool zneg = p.zero() ? p.neg : q.neg;
                const int n = Fp::Bits(v.mant);
                if (v.top() - 1 == -126 && zneg != v.neg && !((v.mant >> (n - 2)) & 1))
                    return zero(false);
            }
            Fp r = limit(Add(p, q, SB, RE), true);
            if (ranged && !r.bad && r.zero())
                r.neg = false;
            return r;
        };
        auto add = [&](const Fp& p, const Fp& q) { return addRule(p, q, true); };
        auto subC = [&](const Fp& p, const Fp& q) { return addRule(p, -q, cfg.zeroRuleCoords != 0); };
        auto addE = [&](const Fp& p, const Fp& q) { return addRule(p, q, cfg.zeroRuleEdges != 0); };
        for (int k = 0; k < 3; k++) {
            X[k] = load(x[k]); Y[k] = load(y[k]); Z[k] = load(z[k]);
        }
        auto edge = [&](int j, int k) { return addE(mul(X[j], Y[k]), -mul(X[k], Y[j])); };
        Fp e1 = edge(1, 2), e2 = edge(2, 0), e3 = edge(0, 1);
        auto sum3 = [&](const Fp& p, const Fp& q, const Fp& r) { return add(add(p, q), r); };
        Fp a = sum3(mul(Z[0], subC(Y[1], Y[2])), mul(Z[1], subC(Y[2], Y[0])), mul(Z[2], subC(Y[0], Y[1])));
        Fp b = sum3(mul(Z[0], subC(X[2], X[1])), mul(Z[1], subC(X[0], X[2])), mul(Z[2], subC(X[1], X[0])));
        Fp c = sum3(mul(Z[0], e1), mul(Z[2], e3), mul(Z[1], e2));       // (v1 + v3) + v2
        Fp det = addE(addE(e1, e2), e3);
        if (pvrfp::Perpendicular(det))
            det = Fp();                                 // FPU_PERP_VAL: as a determinant of zero (pvr_softfp.h)
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
        if (ranged && !det.zero() && !det.bad) {
            // the depth of the tile's first pixel with these numbers; an overflow is stored as 2^127 (Saturated)
            Fp inv0 = Recip(det, cfg.recipBits);
            Fp x00 = Fp::FromHalves(2 * (int64_t)left + sampleHalf), y00 = Fp::FromHalves(2 * (int64_t)top + sampleHalf);
            Fp first0 = mul(add(add(mul(a, x00), mul(b, y00)), c), inv0);
            rangedFirst = first0;
            haveRangedFirst = true;
            // A slope past 2^128 before the division (a or b an infinity) does not step as a number: the plane is
            // evaluated in the wrapping register below (ovf 2). A large slope after the division does not wrap:
            // dreamcast-tests/pvr-tests/depthgaps_edges (a top right corner of (2 - 2^-23) 2^124 with legs 8: a just
            // under 2^128, 2^122 a pixel; legs 12 at 2^118 a pixel: they step from a start of 0, below 2^127).
            const bool capA = a.bad, capB = b.bad;
            if (capA || capB) {
                valid = false; block = false;
                // When a slope overflows its step is +-2^127 a pixel from the plane's start, in a register whose
                // sign is at 2^131, so the steps wrap every 32 pixels from the tile's first pixel; the depth stored
                // is a function of the register alone (RegDepth).
                ovf = 2;
                sa = capA ? ((a.neg != det.neg) ? -1 : 1) : 0;
                sb = capB ? ((b.neg != det.neg) ? -1 : 1) : 0;
                // A slope that did not overflow steps the same register as a number: a * inv (or b * inv) cut to the
                // slopes' 20 bits, then to the register's grid of 2^102 toward zero (not to a pair grid of the infinite
                // slope). dreamcast-tests/pvr-tests/zovf_creg (50 single pixels, words read by background probes:
                // registers at 0, +1 step and -1 step, the other slope from 1.5 2^99 to 1.5 2^122 of either sign, each
                // slope the overflowed one in turn): j 2^116 as itself (j = 1, 7, 30), 2^127 + 20 2^116, 2^127 - 29
                // 2^116, 2^127 + 1.5 2^110 (0x7f000060), 2^128 - 2^116 (0x7f7ff000); -2^127 - 3 2^116 and -2^127 - 3.75
                // 2^112 as a block of E 128 stores them (0xff003000, 0xff000400), -2^127 + 2^116 and -2^116 above every
                // negative probe; a slope of 7 2^101 as 6 2^101 (1 and 3 pixels on), slopes of 1.5 2^101 and 1.5 2^100
                // as 0 (29 pixels on: 0); with a determinant of 32 (the infinite slope 2^124 a pixel) 2^102 and 3 2^102
                // kept (a pair grid of 2^103 would make them 0 and 2^103: 43 of 50); 2^122 + 7 2^102 as 2^122 + 6 2^102
                // at 14 and 15 pixels (the slope's 20 bits; without them 48 of 50). Without these steps 9 of 50, and
                // zovf_ab 518 of 676 regions, zovf_slope 154 of 252, zovf_wrap 104 of 128 (their sign maps: all with
                // them).
                // A slope below zero is taken toward zero onto 2^102: zovf_wrap W2 (b = -16, a slope of -2^-6 a pixel,
                // the register starting at 0: rounded down, the register is -2^102 and its sign against the probe at 0
                // flips; the console keeps 0; 124 of 128 regions rounded down); and an off-grid slope below zero of
                // 2^102 or more: dreamcast-tests/pvr-tests/depth2_cneg (zovf_creg's C0 with F = -(8192 + 1/2),
                // -(8192 + 3/4), -(256 + 1/4), -(2^18 + 1/2) units of 2^102 a row, the words of the pixels (1, j) at
                // 2^127 + j F above zero, read by background probes: toward zero at all 44 targets where down would
                // store another 19 bit word, and the controls +(8192 + 1/2), -(127 + 1/2): 12 of 12 regions).
                {
                    const Fp invS = Recip(det, cfg.recipBits);
                    auto rstep = [&](const Fp& v0, bool cap) -> int64_t {
                        if (cap || v0.zero())
                            return 0;
                        const Fp q = Mul(v0, invS, cfg.coefBits).cut(cfg.slopeBits).cutBelow(102);
                        if (q.zero())
                            return 0;
                        // modulo the register's 2^131 (GAP: a slope of 2^128 or more after the division, from a
                        // determinant below 1, is not measured)
                        const int sh = q.exp - 102;
                        const int64_t m = sh < 30 ? (int64_t)((q.mant << sh) & ((uint64_t(1) << 30) - 1)) : 0;
                        return q.neg ? -m : m;
                    };
                    fa = rstep(a, capA);
                    fb = rstep(b, capB);
                }
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
                // dreamcast-tests/pvr-tests/depthgaps_phase: 105 triangles against three backgrounds, the start
                // fitted per triangle (0 misses each): 33 with c finite start at +-1 step (+inf, NaN, max, 2^127,
                // 2^124, -inf, -max at either corner; others +-0.5, 0, +-100, +-2^60), the one with c an infinity
                // of the slopes' sign (corners at 2^120 beside a +inf top left corner) at -1, the 71 where the
                // infinities cancel at 0. nantr_word: the words, to 19 bits, of 8 such triangles, 31 pixels each,
                // those of 1 step on. nantr_fin (16 starts at the tile's first pixel, the slopes' infinities
                // cancelling): 2^120 .. the largest float as themselves (2^120 as 2^120 - 2^102), 0.5 and 2^100 as
                // 0; nantr_fin2, nantr_wreg: steps added to such starts (RegDepth).
                // A finite start below zero is taken toward zero onto the grid 2^102: dreamcast-tests/pvr-tests/
                // depthgaps_rstart (nantr_fin2's triangle, starts of -2^101, -0.5, -2^90, -(2^119 + 2^100), -(2^119 +
                // 2^101), -(2^119 + 3 2^100), -(2^112 + 2^93) and controls, the pixels 0, 1, 2 steps up and down read
                // by background probes: -2^101 .. -0.5 start at 0 (one step up 2^127, down -2^127 exactly), -(2^119 +
                // 2^100) one step down as -(2^127 + 2^119) (0xff008000), not 0xff008100; 20 pages, 60 depths).
                // It is cut to a start's 20 bits first: dreamcast-tests/pvr-tests/depthgaps_rcut (V0 = (2, 6 + 1/8) at
                // +-inf, V1 = (0, 0), V2 = (2, 6 - 1/8) at Z: start Z, c = Z / 2 finite up to the largest float; the
                // pixel one step toward zero read by background probes: Z = 2^127 + 2^104 and 2^127 + 2^107 give 0,
                // 2^127 + 2^108 + 2^107 gives 2^108, -(2^127 + 2^104) and -(2^127 + 2^107) give 0; 16 pages, 32 of
                // 32 regions; the bits under 2^108 kept would give 2^104, 2^107, 1.5 2^108 and depths below zero).
                if (first0.bad) {
                    r0 = first0.neg ? -(int64_t(1) << 25) : (int64_t(1) << 25);
                } else if (!first0.zero()) {
                    const Fp st = first0.cut(cfg.startBits);
                    const int sh = st.exp - 102;
                    const int64_t m = sh >= 0 ? (sh < 40 ? (int64_t)(st.mant << sh) : 0) : (-sh < 64 ? (int64_t)(st.mant >> -sh) : 0);
                    r0 = st.neg ? -m : m;
                }
                return;
            }
            if (first0.bad || c.bad) {
                valid = false; block = false;
                first_z = first0.bad ? StoredDepth::Saturated(first0.neg) : StoredDepth::FromFp(first0.cut(cfg.storedBits));
                // A plane of zero slopes (all corners bad, flat) is the saturated 2^127 (bgbig).
                if (a.zero() && b.zero())
                    return;
                // The constant overflowed (it holds the corners' x y: from about z = 2^114 .. 2^117 on the screen),
                // the slopes did not: the slopes step as in any other plane (below), from the start these numbers
                // give at the tile's first pixel: 0 where a x0 + b y0 overflows with the other sign and cancels the
                // constant (zword: the tile's first pixel 0, every other one the word UW under the block's window,
                // NegWord; depthgaps_infstart: 8 triangles with a corner from 2^118 to 2^120 of either sign, against
                // backgrounds of +-2^127, 2^126, 0 on 10 pages: 0 at the first pixel, at the others below the window
                // (6, slopes below zero) or above 0 (2)), 2^127 where the start is +infinity (depthgaps_infstart's 2
                // such triangles, 2^118 at (0, 0) and zovf's c = 2^128 at (0, 96); zovf's 6 cells where c is exactly
                // 2^128 at the left of the screen: the first pixel exactly 2^127, the others stepped down from it,
                // between 2^126 and 2^127), -2^127 where it is -infinity (depthgaps_edges N: corners of -2^118 and
                // -2^116 at the left edge: the first pixel equal to a background of -2^127, the others above it and
                // below 0, stored as a block of E 128 above its window, 7 pages; depthgaps_bigpos: stepping up from
                // -2^127 by 2^123 .. 2^126 a pixel past 0 and 2^128, StoredDepth::Big; from +2^127 down past -2^128,
                // a block of E 128 below its window); the same where a x0 + b y0 overflows alone (depthgaps_final X:
                // corners (0, 160) 0.5, (640, 160) +-2^112, (0, 800) 0.5, c finite: from x = 128 on +2^127 at every
                // pixel (steps of 2^102.7 under its last bit), -2^127 at each tile's first pixel and below it at the
                // others; ordinary to the left; 6 pages, 20 tiles each).
                forced = true;
                forcedFirst = first0.bad ? Fp::FromSingle(first0.neg ? 0xff000000u : 0x7f000000u, false) : first0;
            }
        }
        valid = !det.zero() && !det.bad && !a.bad && !b.bad && (forced || !c.bad);
        block = false;
        if (!valid) {
            // A determinant of zero (or at most FPU_PERP_VAL): every pixel the mean of the three depths, (z1 + z2) + z3
            // in rasterizer order in the setup's adder (24 bits, toward zero, the 25 bit reach), times 1/3 rounded up
            // to 24 bits (0xaaaaab 2^-25), the product cut to 24 bits, zero below 2^-126, stored as a flat plane of
            // that depth is (StoredDepth::Flat: above zero cut to 19 bits; below zero cut to the start's 20 bits,
            // then on its block's grid, 16 bits in its own binade, the size rounded up).
            // dreamcast-tests/pvr-tests/perpmean_word: 16,384 triangles at FPU_PERP_VAL 0.75 (flat ones, one binade,
            // within 4 .. 40 binades, one or two depths 16 .. 40 binades below, all six orders, cancelling pairs,
            // zeros), every stored word read back (zexfil.h): 16,384 of 16,384 with this; the other sum orders, a
            // wider or rounding adder, no reach, or a factor 1/3 cut to 24 bits (the reciprocal unit's 0xaaaaaa, as
            // before: flat triangles one step low, 1,618 words wrong) all miss. The factor here is 1/3 rounded up
            // to 24 bits and the product cut to 24 bits: for every 24 bit sum the same 19 bits as an exact division
            // (checked for all 2^23 mantissas), so which of the two the hardware does is not visible. Below zero
            // (pvr-tests/perpmean_neg, the same triangles negated, 11,258 means; perpmean_bg, 136 words): the same
            // sum and quotient of the sizes, toward zero (the adds toward minus infinity instead: 242 wrong), and the
            // 20 bit cut before the 16 bit one rounds up (without it 557 wrong: flat corners with bits only in the
            // last 4 of 24 are not rounded up). A sum of 2^128 or more is +-2^127 (perpmean_bg page 0: 3 x 2^127, the
            // largest float, 2^127 + 2^127 - 2^127 in that order, the same below zero, 36 of 36).
            // The third corner sent is the one added last in every kind of primitive: pvr-tests/perpmean_strip, the
            // last triangle of strips sent with Strip_Len 6 at triangles 1, 2, 3, 5, Strip_Len 1 at triangle 1,
            // Strip_Len 2 at triangle 3 (an odd one has its first two corners swapped: the same sum), and sprites
            // ((A + B) + C, A at each corner of the square, either way round): 8,192 of 8,192 stored words, 6,164 of
            // them triples where another corner added last gives another word (496 .. 542 a tile miss with either).
            // A quotient below 2^-126 is zero, as the tile's start is (the range, above): pvr-tests/perpmean_tiny,
            // means of u / 3 .. u - ulp (u = 2^-126: sums of u .. 3u - ulp, with zeros, cancellations), both
            // signs: equal to a background of 0, below a quad at +u, above one at -2^-120; 2u/3 .. 4u/3 and 2^-120 / 3
            // as their values; 336 of 336 regions (282 with them stored as a float's denormals).
            // dreamcast-tests/pvr-tests/depthgaps_detneg (62 such triangles, three corners at a point that draws a
            // whole row, read out over tile (0, 0)): 100, 1.5, 100 + 2^-12 as themselves (the 1/3 cut down gives 100
            // - 2^-12); -(100 + 2^-12), -64 (1 + 2^-18), the means of -1 -2 -4, -64 -64 -65, -1000 500 100, -0.1 -0.2
            // -0.3, -300 -300 -300.5 rounded up in size onto the 0x100 grid of the word; -100 - d ulp as -100 for d =
            // 1 .. 15, as -100 - 0x100 ulp for d = 16 .. 0x10f (the 20 bit cut first).
            // An infinity or a NaN among the corners' depths makes the mean an infinity, stored as 2^127 with its
            // sign (Saturated). A corner of exponent field 255 adds as an infinity of its sign, and two of opposite
            // signs as their fractions say: the larger fraction's infinity, equal ones +0 (+infinity and -infinity, a
            // NaN against a NaN of the same fraction); a sum is an infinity of fraction 0 (a NaN does not go on).
            // dreamcast-tests/pvr-tests/depthgaps_dzinf (74 triples, each as three corners at one point and 31 as a
            // perpendicular triangle at FPU_PERP_VAL 0.75, against backgrounds at +-2^127, 1.5 2^127, -1.5 2^126 with
            // Equal, Greater, Less: every one with an infinity or a NaN equal to +2^127 or -2^127 or between them as
            // the rule has it, 48 of 48 regions; with the fraction not counting, a NaN against an infinity of the other
            // sign +0: 29 of 48): +inf 1 1, 1 1 +inf, 7f800001 1 1, 2^127 2^127 0, 1e30 1e30 +inf, max x3 equal to
            // 2^127; NaN 7fc00000 -inf 1, -inf NaN 7fffffff 1, NaN 7fc00000 NaN ff800001 1, NaN 7f800002 NaN ff800001
            // 1 at +2^127, NaN 7f800001 NaN ff800001 1, +inf -inf 1, NaN 7fc00000 1 -inf (the NaN, a sum now, against
            // -inf) and NaN 7fc00000 x2 -inf between; depthgaps_detneg (-100.123 +inf +inf, -(1 + 2^-23) NaN 0,
            // 1.2345 +inf 0, 100.123 100 +inf above every probe (2^118), -64 (1 + 2^-23) -inf 0 between -2^118 and 0).
            // refsw3 had the first corner's depth as given there. In the slopes and the constant (products of a corner's
            // depth) a NaN is an infinity of its sign, its fraction does not count: dreamcast-tests/pvr-tests/
            // depthgaps_nanprod (V0 = (0, 0), V1 = (4, 0), V2 = (0, 4) with a NaN and an infinity whose terms of a
            // slope have opposite signs, NaN 7fc00000 +inf 1, NaN 7f800001 +inf 1, NaN 7fc00000 1 +inf, NaN 7fc00000
            // NaN 7f800001 1 ..: every pixel read with the background as the probe, the words of +inf +inf 1 where the
            // terms cancel to +0, 16 of 16 regions).
            // An infinite determinant does not come here (above); a determinant that is not zero always has
            // a plane, so nothing else does either.
            if (det.zero() && !det.bad) {
                const Fp third = pvrfp::OneThird();
                auto frac = [&](int k) { return ((z[k] >> 23) & 0xff) == 255 ? z[k] & 0x7fffffu : 0u; };
                auto addBad = [&](const Fp& p, uint32_t pf, const Fp& q, uint32_t qf) {
                    if (ranged && p.bad && q.bad && p.neg != q.neg && pf != qf)
                        return inf(pf > qf ? p.neg : q.neg);
                    return add(p, q);
                };
                const Fp mean = mul(addBad(addBad(Z[0], frac(0), Z[1], frac(1)), 0, Z[2], frac(2)), third);
                first_z = StoredDepth::Flat(mean, cfg.startBits, cfg.storedBits);
            }
            return;
        }
        Fp inv = Recip(det, cfg.recipBits);
        A = Mul(a, inv, cfg.coefBits);
        B = Mul(b, inv, cfg.coefBits);
        C = Mul(c, inv, cfg.coefBits);
        Fp x0 = Fp::FromHalves(2 * (int64_t)left + sampleHalf), y0 = Fp::FromHalves(2 * (int64_t)top + sampleHalf);
        // The start in the setup's range at every step (rangedFirst: a product a x0 below 2^-126 is zero there):
        // dreamcast-tests/pvr-tests/depthgaps_tinyramp (a ramp with a corner at the tile's start a denormal, read
        // as zero, slopes of about 2^-126: 16 pixels along the zero crossing are below zero on the console, above it
        // with the start taken in plain arithmetic and only then cut to the range; 12 other such ramps, 14 regions of
        // 14 pixel for pixel either way).
        Fp first = forced ? forcedFirst : haveRangedFirst ? rangedFirst : Mul(Add(Add(Mul(a, x0, PB), Mul(b, y0, PB), SB, RE), c, SB, RE), inv, cfg.coefBits);
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
        // at each pixel in floating point, as before, gave 22). So is a start that the cut to the slopes' grid below
        // makes zero: dreamcast-tests/pvr-tests/depthgaps_flt (planes z = C + A (x - y) whose start at tile (0, 0) is
        // C exactly, C from 2^-100 to 2^-60, slopes 2^-10 .. 2, every pixel of the tile read out: the depths below
        // zero stored as a block from the slopes, between field 131's top and field 132's bottom for slopes of 0.5,
        // the diagonal 0; 9 planes, 1024 pixels each). refsw3 evaluated those with slopes more than about 2^71 times
        // the start in floating point at every pixel (the float form below, now only for startOnSlopes 0 and 2), a
        // depth below zero as a float word.
        Fp start = first.cut(cfg.startBits);
        if (pair != pvrfp::NoExp && (cfg.startOnSlopes == 1 || (cfg.startOnSlopes == 2 && start.neg)))
            start = start.cutBelow(pair - cfg.pairBits);
        int e = first.zero() || (cfg.startOnSlopes == 1 && start.zero()) ? pair + (cfg.tileBits - cfg.pairBits) : first.top();
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
        E = (pair != pvrfp::NoExp && pair + (cfg.tileBits - cfg.pairBits) > e) ? pair + (cfg.tileBits - cfg.pairBits) : e;
        // and not below -122 (the block's grid not below 2^-148): dreamcast-tests/pvr-tests/depthgaps_neggrid, flat
        // polygons at negative depths of field 2 (E -124) tie in groups of 0x400 of the word (the grid 2^-138 of a
        // block of E -122, above its window: 0x12340 with every one from 0x121c0 to 0x12400, not 0x12420), field 5
        // (E -121) in groups of 0x100 as everywhere above; depthgaps_tinymean (perpmean_tiny's first version: the
        // determinant-zero mean -2^-125 below a flat quad at -2^-126, as -2^13 and -2^12 of the grid 2^-138, the
        // other means below zero, 0, above it: 336 of 336 regions; 335 with their float words)
        if (E < -122)
            E = -122;
        // and not above 128 (the exponent field's top): slopes of 2^123 a pixel and more give a larger slope's + 5 of
        // 129 .. 131, and the depths of such a block below zero are those of a block of 128 (on its grid 2^112):
        // dreamcast-tests/pvr-tests/depthgaps_bigslope (14 small triangles at the origin, L = 2 .. 5, corners 0 and
        // +-2^123 .. 1.9375 2^126, every pixel inside read by a binary search with the background: 67 depths, those
        // below zero between -2^127 and 0 as a block of 128 above its window has them, and a start of -2^127 (the
        // constant overflowing, regime B) stepping by -2^123 a pixel exactly -2^127, -(2^127 + 2^123), -(2^127 +
        // 2^124): the window of 128, where a block of 129 would have had them above -2^127; the positive ones exact),
        // depthgaps_bigpair (such triangles over each other at 3 .. 10 pixels, Greater, Equal, Less: slopes giving 129,
        // 130, 131 and a block of 128 ordered as their values, every one; as blocks of 129 .. 131 above a block of
        // 128 they would be the other way round)
        if (E > 128)
            E = 128;
        block = true;
    }

    // A depth below zero from the block's fixed point steps, t in units of 2^(unit - 1) (t < 0): StoredDepth::Neg of
    // the block's exponent E (the larger of the start's exponent and the larger slope's + 5: the exponent of the
    // block's grid; Setup) and M = t / 2^(E - 16) rounded down (toward minus infinity). The evidence is at Neg.
    StoredDepth NegWord(pvrfp::i128 t) const
    {
        const int sh = E - 16 - unit + 1;               // 2^(E - 16) in units of 2^(unit - 1)
        if (sh < 0 || sh > 120)
            return StoredDepth::Bits(0x80000000u | (uint32_t)(E + 126) << 23);     // not reached (startOnSlopes 1)
        return StoredDepth::Neg(E, (int64_t)(t >> sh));
    }
    // The depth stored for the pixel i columns right and j rows down of the tile's first pixel; centers: the
    // ISP samples at pixel centers (HALF_OFFSET bit 0), as given to Setup (the start is evaluated at that sample
    // point and the pixels are whole steps from it; a centers other than Setup's moves by half steps: the probes of
    // dreamcast-tests/model, which set up at the centre). An overflowing plane's register steps whole pixels from
    // its start either way.
    // The depth stored for an overflowing plane's register (ovf 2): r in units of 2^102, two's complement with its
    // sign at 2^131 (it wraps every 32 steps of 2^127). Zero is 0; above zero, below 2^128, it is r itself, cut to 19
    // bits; from 2^128 on above every float word (StoredDepth::Big); at -2^131 the lowest word there is; otherwise
    // below zero it is a negative depth's store (StoredDepth::Neg) of the register shifted left past the zeros under its
    // sign bit: with u = r + 2^131 (the bits under the sign) and 2^p its leading one (2^(p + 102)), E = p + 100 and M
    // = floor((u 2^(28 - p) - 2^29) / 2^10), the bits that are left as a block of E 128 has them.
    // At whole steps, m = r / 2^127 mod 32 (a 5 bit two's complement number: 16 .. 31 are -16 .. -1), this gives:
    // dreamcast-tests/pvr-tests/zword (background probes; every word to 19 bits, the same for +inf, NaN and 2^127
    // corners at every place tried, and the mirror, steps of +2^127, word for word): m = 0 is 0, 1 is 2^127 (equal), 2
    // .. 15 above every probe (the largest positive is 0x7f7fffe0), 16 below every probe (the lowest is 0xc2ffffe0),
    // 23 and 30 equal the bottoms of fields 253, 254 (0xfeffffe0, 0xff7fffe0), 31 is -2^127, and 17; 18, 19; 20 ..
    // 22; 24 .. 29 lie between field 250's top and 251's bottom; 251's and 252's; 252's and 253's; 253's and 254's
    // (17: E 125, M -8 2^15; 18, 19: E 126, M -8, -4 2^15; 20 .. 23: E 127, M -8, -6, -4, -2 2^15; 24 .. 31: E 128, M
    // (m - 32) 2^15). And the order of the 32 among themselves: depthgaps_mm (zword's triangle with legs 64 over its
    // mirror, every pair of the 32 values at one pixel, Equal, Greater, Less: -16 < -15 < .. < -1 < 0 < 1 < .. < 15,
    // no two equal, at three places). 16 is below -2^-100 and -1.5 2^-126 as well (depthgaps_edges M).
    // Between whole steps (a finite start): dreamcast-tests/pvr-tests/nantr_fin (16 starts at the tile's first
    // pixel), nantr_fin2 (8 starts with 0 .. +-15 steps, 31 pixels each) and nantr_wreg (8 starts of up to 1/8 step
    // with 0 .. -12 steps, 14 pixels each), every word where the probes pin one: 2^127 + 2^110 as itself, -(2^127 +
    // 2^110) as 0xff000100 (E 128, M -(2^15 + 1)), 2^119 - 9 steps as 0xfeff0000, 1/8 - 9 steps as 0xfee00100, 1/8 - 2
    // steps as 0xff700100; -8.125, -8.0625, -8.09375, -12.125, -12.09375 steps not above -8 and -12 (above the
    // window of E 127 and 126: below every depth of the next E); -2^119 and 2^119 - 1 step between -2^127 and 0.25
    // (above the window of E 128).
    // Among OTHER planes' depths between the same two float words the register's words and the values between them
    // lie as this representation (RegDepth = Neg / Big) has them: dreamcast-tests/pvr-tests/depth2_regmix (1,188
    // pairs in one tile each, a register triangle (zword's TL +-inf and C1, zovf_creg's C0 with a finite slope of
    // +-2^100 .. 2^122 a row: between whole steps) against a block from a start of 0 (depthgaps_ties' B(L, Z), E 122
    // .. 128 from its slopes: depths above its window) or another C0, and in tile (0, 0) zword's TL +inf with legs 64
    // over 17 small triangles a set whose start is -2^127 (depthgaps_bigpos: depths of 2^128 and more, and a block of
    // E 128 below its window); Greater, Equal, Less: 3,600 of 3,600 regions; 40,884 pixel pairs lie between the same
    // two float words: a block's depths above the window of E below the register's below the window of E + 1,
    // between fields 250 / 251 .. 253 / 254 (m = 17 .. 29 and values between steps: 36,314 pairs), above-window
    // depths of one E ordered by M with the register's (C0, both orders, 132), Big against m = 2 .. 15 (103, both
    // orders), E 128 below its window against m = 24 .. 29 (45, both orders)). And against an ordinary block's depths
    // below its window, E 125 .. 127: dreamcast-tests/pvr-tests/depth2_regbelow (tiny triangles 2^-10 .. 2^-4 pixel
    // around one pixel's centre near the origin, the only place the setup keeps such corners' products in range, their
    // start in the window of E, slopes below 2^(E - 5); against zovf_creg's C0 with a finite F of -(2^108 .. 2^118) a
    // row, off the block's grid, and in tile (0, 0) zword's TL +inf with legs 64; Greater, Equal, Less: 2,442 pixel
    // pairs, 666 of 666 regions as refsw3): the register below the window of the same E ordered by M with the block,
    // the register's value rounded down onto 2^(E - 16) as the block's (1,308 pairs: E 127 just below m = 23, between
    // m = 22 and 23, M from -66,382 to -65,536.2, 1,225, 352 of them equal, the register drawn first or second; the
    // words m = 19 and 22 against blocks of E 126 and 127 at i + j = 45 and 42, 72 (23 equal) and 11; the register
    // keeping its fraction would differ at 351, both exact at 374, the block's M rounded up at 613, the register's at
    // 581, the register above / below every block depth there at 754 / 929), and the register above the window of
    // E - 1 (just below m = 20, 18, 17) below the block below the window of E (1,134 pairs, E 127, 126, 125: 289,
    // 384, 461; by value it would be above: all 1,134 differ). The words m = 17, 18, 20, 21 lie beyond an ordinary
    // block's reach (its M is -2^16 - (i + j) 2^11 or more, i + j <= 62 in a tile): their order with one never shows.
    static StoredDepth RegDepth(int64_t r)
    {
        r = ((r + (int64_t(1) << 29)) & ((int64_t(1) << 30) - 1)) - (int64_t(1) << 29);
        if (r == 0)
            return StoredDepth();
        if (r > 0) {
            if (r >= (int64_t(1) << 26)) {
                Fp v;
                v.mant = (uint64_t)r;
                v.exp = 102;
                return StoredDepth::Big(v.cut(19));
            }
            const int p = Fp::Bits((uint64_t)r) - 1;
            const uint64_t m = p <= 23 ? (uint64_t)r << (23 - p) : (uint64_t)r >> (p - 23);
            return StoredDepth::Bits((uint32_t)(p + 102 + 127) << 23 | ((uint32_t)m & 0x7fffe0u));
        }
        const int64_t u = r + (int64_t(1) << 29);
        if (u == 0)
            return StoredDepth::Bits(0x807fffffu);
        const int p = Fp::Bits((uint64_t)u) - 1;
        return StoredDepth::Neg(p + 100, ((u << (28 - p)) - (int64_t(1) << 29)) >> 10);
    }

    StoredDepth At(int i, int j, bool centers) const
    {
        using namespace pvrfp;
        if (ovf == 2)
            return RegDepth(r0 + (int64_t)(sa * i + sb * j) * (int64_t(1) << 25) + i * fa + j * fb);
        if (!valid)
            return first_z;
        // half steps from the sample point the start was evaluated at (Setup) to the one asked for: 0 when they agree
        const int h = (centers ? 1 : 0) - (startCenters ? 1 : 0);
        Fp v;
        if (block) {
            pvrfp::i128 t = d0 + (2 * i + h) * dx + (2 * j + h) * dy;
            if (t < 0)
                return NegWord(t);
            pvrfp::u128 m = (pvrfp::u128)t;
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
        const Fp s = v.cut(cfg.storedBits);
        if (!s.zero() && s.top() - 1 >= 128)
            return StoredDepth::Big(s);
        return StoredDepth::FromFp(s);
    }
};

using DepthTile = DepthTileT<DepthConfig>;    // the run-time form, for the probes and generators

} // namespace pvrisp
