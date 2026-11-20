/*
    The TSP's planes (1/w, and attribute / w for texture coordinates and colors), in the setup units' own
    arithmetic (pvr_softfp.h).

    Measured on a console with dreamcast-tests/pvr-tests/wmeas, uvmeas, cmeas and uvspan (11 million readings, all
    reproduced):
      - the coefficients A, B, C are the setup's (24 bit truncating arithmetic; an attribute plane multiplies each
        product of the 1/w plane by the corner's attribute and cuts again), cut to 16 bit floats. A zero keeps
        its sign.
      - they are evaluated once per RUN of pixels of a triangle on a line of a tile, in fixed point on one grid
        per tile: 22 bits under E, the largest of the exponent of C and of the exponents the two products can
        have in the tile, exp(A) + exp(X) - 1 and exp(B) + exp(Y) - 1 with X, Y the tile's first coordinate
        plus 16. The value at the run's first pixel is floor(C) + floor(A x0) + floor(B y), every further pixel
        adds floor(A); a coefficient that is zero counts as +-2^-127 (so a negative zero floors to one unit
        below zero). The result is cut down to 16 bits.
      - attribute = (attribute / w) / (1 / w) with a table reciprocal of 16 bits (512 measured digits) and the
        product of the two mantissas cut down at 17 fractional bits.
      - a determinant of zero (or at most FPU_PERP_VAL): the plane is flat: 1/w the mean of the depths, an attribute
        the third corner's (dreamcast-tests/pvr-tests/slivers_detzero, slivers_dzzero, perpmean_fogw, perpmean_uv).
*/
#pragma once
#include "pvr_softfp.h"
#include <cstdlib>

namespace pvrtsp {

using pvrfp::Fp;

// The TSP is its own block: nothing here is taken over from the ISP's units. Its setup adders drop an operand
// whose leading bit is 25 or more exponents below the other's, as the ISP's do: dreamcast-tests/pvr-tests/uvmeas
// set reach1 (model/reachgen: 40 triangles for each boundary R | R + 1 from 10 to 54, 95,453 readings) is the
// console's with reach 25 only (24: 554 wrong, 26: 785). Found on polly2-data gundam_ingame's ground, a slope that
// is what is left of two large terms cancelling and a third far smaller (set dump_gundam1: 747,976 readings,
// 596,862 right with the sums exact); with it gundam_ingame, daytona_sanic, daytona_intro, sa2_ingame2 and three
// more are the console's in all of video memory.
// The TSP setup's numbers as the hardware has them, baked in (pvr_softfp.h: C<N>); the evidence for each is above
// and at its use. PlaneConfig below is the same with plain ints, for dreamcast-tests/model/tspprobe --scan (its
// fields[] table points at them); PlaneT is templated on either.
struct PlaneHW {
    static constexpr pvrfp::C<24> productBits{};
    static constexpr pvrfp::C<24> sumBits{};
    static constexpr pvrfp::C<25> reach{};          // set reach1 (above)
    static constexpr pvrfp::C<24> recipBits{};
    static constexpr pvrfp::C<16> coefBits{};       // A, B, C
    static constexpr pvrfp::C<22> gridBits{};       // under the tile's exponent
    static constexpr pvrfp::C<16> valueBits{};      // the plane's value at the pixel
    // EXPERIMENT (dreamcast-tests/model/tspprobe valueFixed= / wfixed=; default 0): drop at least this many low bits of
    // the grid's integer, as a fixed field would, instead of only those below 16 bits from the leading one
    static constexpr pvrfp::C<0> valueFixed{};
    // The numbers of the setup have an exponent field of 8 bits: a result is at least 2^expLow and below
    // 2^(expHigh + 1), or it is a zero or an infinity (see Plane::Setup). range 0: no limits, the arithmetic as
    // it was before dreamcast-tests/pvr-tests/uvmeas measured them (mixes wexp, whuge, wtiny, wspec, wneg).
    static constexpr pvrfp::C<1> range{};
    static constexpr pvrfp::C<-126> expLow{};
    static constexpr pvrfp::C<127> expHigh{};
    // Adding a zero (see Plane::Setup): the zero counts as 2^-127 with its sign, for whether the sum is below the
    // range, when the other operand is below 2^(-127 + zeroReach), that is, has the smallest exponent there is.
    static constexpr pvrfp::C<2> zeroReach{};
    // EXPERIMENT (model/tspprobe tileLocal=): 0 a run's coordinates are the screen's (measured: every uvmeas reading);
    // 1 they are the tile's own: the tile's constant C + A Xt + B Yt (exact, rounded down once) plus A x and B y with
    // x, y counted from the tile's corner, on the same grid (15,801 readings wrong); 2 the same with the grid chosen
    // from the tile's constant and the tile's own coordinates (1,277,606); 3 every piece rounded down by itself (111,580)
    static constexpr pvrfp::C<0> tileLocal{};
    // EXPERIMENT (model/tspprobe macOrder=): 0 C, A x and B y each rounded down by itself (measured); 1 C + B y rounded
    // together, then A x added rounded (a MAC for the line, then one for the run); 2 C + A x together, B y alone;
    // 3 all three together, rounded once
    static constexpr pvrfp::C<0> macOrder{};
    // EXPERIMENT (model/tspprobe sumA=, sumW=): the order of the three terms of an attribute plane's slope sums (sumA)
    // and of the 1/w plane's (sumW): 0 (t0 + t1) + t2 (measured), 1 (t0 + t2) + t1, 2 (t1 + t2) + t0. sumA 2 gives set
    // dump_gundam1 too, but 22,582 readings of ugen5 and 1,621 of wtiny1 wrong: the reach does it (above).
    static constexpr pvrfp::C<0> sumA{};
    static constexpr pvrfp::C<0> sumW{};
};

struct PlaneConfig {
    int productBits = PlaneHW::productBits, sumBits = PlaneHW::sumBits, reach = PlaneHW::reach, recipBits = PlaneHW::recipBits;
    int coefBits = PlaneHW::coefBits;
    int gridBits = PlaneHW::gridBits;
    int valueBits = PlaneHW::valueBits;
    int valueFixed = PlaneHW::valueFixed;
    int range = PlaneHW::range, expLow = PlaneHW::expLow, expHigh = PlaneHW::expHigh;
    int zeroReach = PlaneHW::zeroReach;
    int tileLocal = PlaneHW::tileLocal;
    int macOrder = PlaneHW::macOrder;
    int sumA = PlaneHW::sumA, sumW = PlaneHW::sumW;
};

// floor(v / 2^unit) as an integer; a negative zero gives -1
inline int64_t FloorUnits(const Fp& v, int unit)
{
    if (v.zero())
        return v.neg ? -1 : 0;
    int s = v.exp - unit;
    int64_t m;
    if (s >= 0)
        m = s >= 62 ? INT64_MAX / 4 : (int64_t)(v.mant << s);
    else {
        uint64_t lost = (-s >= 64) ? v.mant : (v.mant & ((1ull << -s) - 1));
        m = (-s >= 64) ? 0 : (int64_t)(v.mant >> -s);
        if (lost && v.neg)
            m += 1;
    }
    return v.neg ? -m : m;
}

// EXPERIMENT (environment TSP_ZERO, or set by a generator; 0: the model): how the setup's adders add a zero (Setup).
// 1 the rule before dreamcast-tests/pvr-tests/tsp2_zadd: in a sum's last addition a zero added to 2^-126 .. 2^-125
// counts as 2^-127 with its sign (so 1.25 2^-126 + 0 is 1.75 2^-126), in the first a zero is a zero, -0 + -0 is -0
inline int tspZeroVariant = -1;
inline int TspZero()
{
    if (tspZeroVariant < 0) {
        const char* e = getenv("TSP_ZERO");
        tspZeroVariant = e ? atoi(e) : 0;
    }
    return tspZeroVariant;
}

template<class Cfg>
struct PlaneT {
    Fp A, B, C;
    Fp A0, B0, C0;              // the same before the cut to coefBits: what the mipmap level is made of
    Fp Sa, Sb, Sc;              // the three sums before they are divided by the determinant (MipSelect)
    Fp Sdet;                    // and the determinant
    bool bad = false;           // an infinity or a NaN among the inputs: not handled here
    Cfg cfg;

    // x, y, z (1/w) of the corners; attr: the attribute at each corner for an attribute / w plane, or null. The bits
    // of IEEE singles, as the TA gives them (a colour: refsw_tile.h ColorExp)
    void Setup(const uint32_t x[3], const uint32_t y[3], const uint32_t z[3], const uint32_t* attr, const Cfg& config)
    {
        using namespace pvrfp;
        cfg = config;
        const auto PB = cfg.productBits;
        const auto SB = cfg.sumBits;
        const auto RE = cfg.reach;
        Fp X[3], Y[3], Z[3], AT[3];
        bad = false;
        // The range of the setup's numbers (cfg.range; dreamcast-tests/pvr-tests/uvmeas: 3.9 million readings at very
        // large, very small and special depths, all reproduced). A number is a sign, an exponent field of 8 bits and a
        // mantissa; field 0 is a zero and field 255 an infinity, whatever the mantissa says, so a denormal depth
        // is zero and a NaN is an infinity. Here an infinity is an Fp with `bad` set and a sign.
        //   product   zero times anything is zero, also times an infinity; infinity times anything else is
        //             infinity; a result of 2^128 or more is an infinity, one below 2^-126 a zero. The sign is the
        //             product's in every case.
        //   sum       infinity plus a number is that infinity; two infinities of the same sign too, of opposite
        //             signs they are +0. A result of 2^128 or more is an infinity, one below 2^-126 is +0, and every
        //             sum that is zero is +0 (an exact cancellation, -0 + -0 too).
        //   a zero    (an input, a product below 2^-126 with its sign, or a sum) added to a number x of 2^-126 ..
        //             1.5 2^-126 whose sign is not the zero's gives +0, as if the zero were 2^-127 with its sign
        //             and the sum then below the range; added to any other x it gives x, to the bit. In every
        //             addition, the first of a sum of three terms as well as the last: the ISP's depth setup's rule
        //             (pvr_isp_depth.h add, dreamcast-tests/pvr-tests/depthgaps_zadd), one adder. Measured with
        //             dreamcast-tests/pvr-tests/tsp2_zadd: 9,600 slivers of determinant about 1 whose U / w x slope
        //             is a sum with a zero term and another of 1 .. 2 2^-126 (so that the slope's magnitude shows,
        //             times the screen's x), U read through uvmeas's number texture: 9,600 of 9,600 with this rule.
        //             The rule before (a zero counted as 2^-127 in a sum's last addition, 1.25 2^-126 + 0 =
        //             1.75 2^-126; a zero a zero in the first; -0 + -0 = -0; fitted on the sign of 26 slopes that
        //             underflow, uvmeas wtiny1 and cmeas cwext1, which this rule gives as well): 1,463. Only -0 + -0 =
        //             -0 changed: 7,203 (3 of the 2,400 cases with two zeros); 2^-127 kept in the last addition:
        //             6,207 (207 of 3,600 with the zero there); no rule in the first addition: 7,253 (53 of 2,400
        //             with the zero there); 2^-127 in both: 3,810. uvmeas (36 sets) and cmeas (c1, cwext1, cwsum1)
        //             stay exact. -0 + -0 = +0 in the determinant-zero mean too: tsp2_mipdz2 row 14, column 0 (three
        //             corners of -2^-127 (1 + j/16), denormals: the mean (-0 + -0) + -0, the level's w that of its
        //             +0, trilinear pass B): 2 of 2 regions, as -0 neither. GAP: whether a zero from a denormal
        //             input brings its mantissa in here as it does in the mean's first addition (below); the
        //             coordinate differences and the edge products take the rule too (one adder), not measured there.
        const bool ranged = cfg.range != 0;
        auto inf = [](bool neg) { Fp r; r.bad = true; r.neg = neg; return r; };
        auto zero = [](bool neg) { Fp r; r.neg = neg; return r; };
        auto above = [&](const Fp& v) { return !v.zero() && v.top() - 1 > cfg.expHigh; };
        auto below = [&](const Fp& v) { return !v.zero() && v.top() - 1 < cfg.expLow; };
        auto load = [&](uint32_t u) { return Fp::FromSingle(u, ranged); };
        auto mul = [&](const Fp& p, const Fp& q) {
            if (!ranged)
                return Mul(p, q, PB);
            const bool neg = p.neg != q.neg;
            if (p.bad || q.bad) {
                const Fp& other = p.bad ? q : p;
                return (!other.bad && other.zero()) ? zero(neg) : inf(neg);
            }
            Fp r = Mul(p, q, PB);
            if (above(r)) return inf(neg);
            if (below(r)) return zero(neg);
            return r;
        };
        const bool oldZero = TspZero() == 1;            // EXPERIMENT: the rule before tsp2_zadd
        auto addTerm = [&](const Fp& p, const Fp& q, bool third) {
            if (!ranged)
                return Add(p, q, SB, RE);
            if (p.bad || q.bad) {
                if (p.bad && q.bad && p.neg != q.neg)
                    return zero(false);
                return inf(p.bad ? p.neg : q.neg);
            }
            auto smallest = [&](const Fp& v) { return !v.zero() && v.top() - 1 < -127 + cfg.zeroReach; };
            if (oldZero) {
                Fp a = p, b = q;
                if (third) {
                    if (q.zero() && smallest(p)) { b.mant = 1; b.exp = -127; }
                    if (p.zero() && smallest(q)) { a.mant = 1; a.exp = -127; }
                }
                Fp r = Add(a, b, SB, RE);
                if (above(r)) return inf(r.neg);
                if (below(r)) return zero(false);
                return r;
            }
            if (p.zero() != q.zero()) {
                const Fp& v = p.zero() ? q : p;
                const bool zneg = p.zero() ? p.neg : q.neg;
                const int n = Fp::Bits(v.mant);
                const bool half = n >= 2 && ((v.mant >> (n - 2)) & 1);        // |v| >= 1.5 2^(top - 1)
                if (smallest(v) && zneg != v.neg && !half)
                    return zero(false);
            }
            Fp r = Add(p, q, SB, RE);
            if (above(r)) return inf(r.neg);
            if (below(r) || r.zero()) return zero(false);
            return r;
        };
        auto add = [&](const Fp& p, const Fp& q) { return addTerm(p, q, false); };
        auto sum3 = [&](const Fp& t1, const Fp& t2, const Fp& t3) { return addTerm(addTerm(t1, t2, false), t3, true); };
        auto sub = [&](const Fp& p, const Fp& q) { return add(p, -q); };
        for (int k = 0; k < 3; k++) {
            X[k] = load(x[k]); Y[k] = load(y[k]); Z[k] = load(z[k]);
            if (attr)
                AT[k] = load(attr[k]);
            bad = bad || (!ranged && (X[k].bad || Y[k].bad || Z[k].bad || (attr && AT[k].bad)));
        }
        if (bad)
            return;
        auto edge = [&](int j, int k) { return sub(mul(X[j], Y[k]), mul(X[k], Y[j])); };
        Fp e[3] = { edge(1, 2), edge(2, 0), edge(0, 1) };
        Fp ta[3], tb[3], tc[3];
        for (int k = 0; k < 3; k++) {
            int j = (k + 1) % 3, l = (k + 2) % 3;
            ta[k] = mul(Z[k], sub(Y[j], Y[l]));
            tb[k] = mul(Z[k], sub(X[l], X[j]));
            tc[k] = mul(Z[k], e[k]);
            if (attr) {
                ta[k] = mul(AT[k], ta[k]);
                tb[k] = mul(AT[k], tb[k]);
                tc[k] = mul(AT[k], tc[k]);
            }
        }
        static const int ord[3][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 } };
        const int* o = ord[attr ? (int)cfg.sumA : (int)cfg.sumW];
        Fp a = sum3(ta[o[0]], ta[o[1]], ta[o[2]]);
        Fp b = sum3(tb[o[0]], tb[o[1]], tb[o[2]]);
        Fp c = attr ? sum3(tc[0], tc[1], tc[2]) : sum3(tc[0], tc[2], tc[1]);
        Fp det = sum3(e[0], e[1], e[2]);
        Sa = a; Sb = b; Sc = c; Sdet = det;
        if (pvrfp::Perpendicular(det))
            det = Fp();                                 // FPU_PERP_VAL: as a determinant of zero (pvr_softfp.h)
        if (det.zero() && !det.bad) {
            // A determinant of zero: every plane is flat. The 1/w plane is the mean of the three depths, ((z1 + z2) +
            // z3) times the single nearest 1/3 (0x3eaaaaab, a hair above it), the sums and the product in the setup's
            // 24 bit arithmetic; an attribute plane is the third corner's attribute times that, so a colour or U is
            // the third corner's (dreamcast-tests/pvr-tests/covmeas, mix degen --vcol: the colour). Measured with
            // dreamcast-tests/pvr-tests/slivers_tsp (covmeas's degenerate and almost-point triangles with a depth, a U
            // and a colour per corner: with the third corner's depth, refsw3's guess until then, 42 of its 3600
            // cells differ, every degenerate one that is drawn, in 1/w and U; with the mean none) and slivers_detzero
            // (9,600 triples of depths picked where ways of taking the mean differ, each read as 1/w through table
            // fog, as a U of 64 over it and as a random U over it: 9,600 of 9,600 triples; with the 1/3 cut down,
            // 0xaaaaaa, 8,143; with 1/3 rounded up at 26 bits or more 9,594; the sums 25 bits wide 9,474; (z1 + z3) + z2
            // 9,413, (z2 + z3) + z1 9,379; the exact mean 9,373; the colour of every one, Gouraud, the third
            // corner's). FPU_PERP_VAL's triangles take the same path (slivers_perp: 2,400 of 2,400 cells).
            // The same mean on FPU_PERP_VAL's triangles: pvr-tests/perpmean_fogw, perpendicular triangles with table fog of density 1 (the fog's w is
            // this 1/w; a page of the table's blend bits, one of its index): 16,384 of 16,384 pixels, 12,288 of them
            // triangles of three different 1/w (both windings); with the third corner's 1/w (the guess before) 4 of 16
            // regions, those of three equal 1/w. To the last bit: pvr-tests/perpmean_uv (U of 64 .. 128 read through
            // uvmeas's number texture, so the quotient (u3 X) / X to half a unit of its 16th bit; 8,192 triangles, 1/w
            // of 2^-20 .. 2^20 or one 12 .. 30 binades below the others, both windings): 8,192 of 8,192 readings; X the
            // third corner's 1/w 2,220, the sum (w1 + w3) + w2 8,176, the factor 0xaaaaaa 8,132, an exact division by 3
            // cut to 24 bits 8,182 (2 of 8 regions), the mean cut to 20 bits 7,934, the attribute times the 16 bit mean
            // 4,102.
            // With an overflowing sum (corners at 2^127 or the largest float) the plane is an infinity, which the
            // evaluator takes as 2^-128, and a colour of 0 (a zero, 2^-127) comes out 2 / 1: white, as a console draws
            // detinf's perpendicular black probe quads at 2^127 and the largest float (FPU_PERP_VAL 2^20 and +infinity:
            // 16 cells each, black before); any other colour infinity / infinity, 2^-128 / 2^-128: white as well, and a
            // texel one unit under a whole U. pvr-tests/perpmean_ovf (FPU_PERP_VAL +infinity, 1/w sums at 2^128 and
            // just under it, ten colour sets, Gouraud, untextured, textured modulate and decal, both windings): 240 of
            // 240 regions (126 before).
            A = Fp(); B = Fp();
            const Fp third = pvrfp::OneThird();
            // A depth of exponent field 0 is a zero, but not quite in the first addition: there, against a depth of
            // 2^-126 .. 2^-125 of the other sign, it is taken as (1 + m 2^-23) 2^-127 (m its mantissa, the hidden
            // bit not cleared, as every addition takes a zero for 2^-127 there: Setup above), and if the difference
            // would be below 2^-126 the sum is +0; otherwise the sum is the other depth as it is, the zero taking
            // nothing off (dreamcast-tests/pvr-tests/slivers_dzspec: 4 rows of tiny depths; slivers_dzzero: 9,600 of
            // 9,600 triples, 831 of them exactly on the boundary, which keeps the depth. Taking the zero off the kept
            // depth: 6,711; the zero as its denormal value: 7,562; as 2^-127: 7,564; a zero as zero: 6,834). Signs
            // equal, or a larger depth: the zero is nothing. With m = 0 that is the rule of every addition (Setup
            // above). In the last addition that rule; GAP: a denormal's m there is not told apart, the mean of a sum
            // below 2^-125 is below the range anyway.
            auto fieldZero = [&](int k) { return ((z[k] >> 23) & 0xff) == 0; };
            auto withZero = [&](const Fp& p, uint32_t zeroBits) {
                const bool zneg = (zeroBits >> 31) != 0;
                if (p.zero() || p.bad || p.top() - 1 != cfg.expLow || p.neg == zneg)
                    return p;
                Fp zv;
                zv.mant = (1u << 23) | (zeroBits & 0x7fffffu);
                zv.exp = -150;
                zv.neg = zneg;
                const Fp r = Add(p, zv, 62, 400);
                return (r.zero() || r.top() - 1 < cfg.expLow) ? Fp() : p;
            };
            Fp first;
            if (ranged && fieldZero(0) && !fieldZero(1))
                first = withZero(Z[1], z[0]);
            else if (ranged && fieldZero(1) && !fieldZero(0))
                first = withZero(Z[0], z[1]);
            else
                first = addTerm(Z[0], Z[1], false);
            const Fp mean = mul(addTerm(first, Z[2], true), third);
            C = attr ? mul(AT[2], mean).cut(cfg.coefBits) : mean.cut(cfg.coefBits);
            A0 = A; B0 = B; C0 = C;
            return;
        }
        // The reciprocal has the range too: of a determinant of 2^126 or more it is zero (below), and of an
        // infinite one it is zero. Every coefficient is then a zero with the sign of its sum times the
        // determinant's (dreamcast-tests/pvr-tests/hugepos: triangle c, determinant -2^127.4, is black, the quotient of
        // two planes below zero; b, +2^127.7, is white exactly where x <= y, where -2^-127 x + 2^-127 y + 2^-127 is
        // above zero; a, whose determinant is an infinity from corners of 2^62 on, is white all over).
        // The limit is the determinant's exponent: from 2^126 on, 2^126 itself included (its reciprocal's exponent
        // taken as -(e + 1) before a mantissa of exactly 1 moves it up, as the ISP's depth unit, pvr_isp_depth.h):
        // dreamcast-tests/pvr-tests/trsort_recip pages 3, 11, 13 (one blue triangle over the screen, determinants of
        // exactly +2^126, -2^126 and +2^126 with sloped depths; its colour where the probes over it fail, 61,440
        // pixels a page): white, black and 0x81..0x84 greys, the colours of a zero reciprocal, as at 2^126 (1 +
        // 2^-23) (pages 4, 12, 14); with 2^-126 kept, blue as at 2^126 (1 - 2^-24) (page 2): 6 of 160 regions wrong.
        Fp inv = det.bad ? zero(det.neg) : Recip(det, cfg.recipBits);
        if (ranged && (below(inv) || (!det.bad && !det.zero() && det.top() - 1 >= 126)))
            inv = zero(inv.neg);
        A0 = mul(a, inv); B0 = mul(b, inv); C0 = mul(c, inv);
        A = A0.cut(cfg.coefBits);
        B = B0.cut(cfg.coefBits);
        C = C0.cut(cfg.coefBits);
    }

    // The plane at a pixel. x0Halves, yHalves: the sample point of the run's first pixel and of the line, in half
    // pixels (absolute); n: whole pixels from the run's first pixel; tileLeft, tileTop: the tile's first pixel.
    // super: a SupSample point (TSP bit 12, dreamcast-tests/pvr-tests/supsample). The evaluator then steps along the
    // run in half pixels, (2 n + hx) steps of A / 2 instead of n of A, the centre's point too, each half coefficient
    // cut to the grid like a whole one. A point half a line down (hy) has B / 2 added to C before C is cut to the
    // grid: floor(C + B / 2), not floor(C) + floor(B / 2), one unit more where their fractions below the grid carry
    // (C has any only where another term sets the grid; dreamcast-tests/pvr-tests/ssrare: of 140,545 pixels where
    // that unit shows, the console has it on exactly those 24 that carry).
    Fp Value(int64_t x0Halves, int64_t yHalves, int n, int tileLeft, int tileTop, bool super = false, int hx = 0, int hy = 0) const
    {
        using namespace pvrfp;
        // The evaluator has no zero and no infinity: it takes a coefficient's exponent field as a signed 8 bit
        // number. A zero (field 0) is 2^-127 with its sign, and an infinity (field 255, +128) wraps to 2^-128
        // (dreamcast-tests/pvr-tests/uvmeas, mixes wexp and whuge: with every product of the setup overflowing, both
        // constants are -2^-128 and both slopes -2^-127 on 4875 of 4875 readings). On an ordinary grid a zero's 2^-127
        // is the "negative zero floors to one unit below zero" above; with 1/w below 2^-106 the grid is so fine
        // that a flat plane visibly runs away, by 2^-127 a pixel.
        auto seen = [](const Fp& v) {
            if (v.bad) return Fp::Pow2(-128, v.neg);
            if (v.zero()) return Fp::Pow2(-127, v.neg);
            return v;
        };
        const Fp a_ = seen(A), b_ = seen(B), c_ = seen(C);
        // range 0: as before the limits were measured, a zero coefficient does not count for the grid
        auto top = [&](const Fp& v, const Fp& s) { return (!cfg.range && v.zero() && !v.bad) ? NoExp : s.top(); };
        int ex = Fp::FromHalves(2 * (int64_t)(tileLeft + 16)).top(), ey = Fp::FromHalves(2 * (int64_t)(tileTop + 16)).top();
        int e = top(C, c_);
        if (top(A, a_) != NoExp && top(A, a_) + ex - 1 > e) e = top(A, a_) + ex - 1;
        if (top(B, b_) != NoExp && top(B, b_) + ey - 1 > e) e = top(B, b_) + ey - 1;
        if (e == NoExp)
            e = 0;                                      // a plane that is zero everywhere: any grid
        int unit = e - cfg.gridBits;
        Fp ax = Mul(a_, Fp::FromHalves(x0Halves), 62), by = Mul(b_, Fp::FromHalves(yHalves), 62);
        if (ax.zero()) ax.neg = a_.neg;                 // x = 0: still on the coefficient's side of zero
        if (by.zero()) by.neg = b_.neg;
        int64_t k = FloorUnits(c_, unit) + FloorUnits(ax, unit) + FloorUnits(by, unit) + (int64_t)n * FloorUnits(a_, unit);
        if (cfg.macOrder) {
            auto exact = [](const Fp& p, const Fp& q) { return Add(p, q, 62, 400); };
            if (cfg.macOrder == 1)
                k = FloorUnits(exact(c_, by), unit) + FloorUnits(ax, unit) + (int64_t)n * FloorUnits(a_, unit);
            else if (cfg.macOrder == 2)
                k = FloorUnits(exact(c_, ax), unit) + FloorUnits(by, unit) + (int64_t)n * FloorUnits(a_, unit);
            else
                k = FloorUnits(exact(exact(c_, by), ax), unit) + (int64_t)n * FloorUnits(a_, unit);
        }
        if (cfg.tileLocal) {
            const Fp xt = Fp::FromHalves(2 * (int64_t)tileLeft), yt = Fp::FromHalves(2 * (int64_t)tileTop);
            const Fp ct = Add(Add(c_, Mul(a_, xt, 62), 62, 400), Mul(b_, yt, 62), 62, 400);
            Fp axl = Mul(a_, Fp::FromHalves(x0Halves - 2 * (int64_t)tileLeft), 62), byl = Mul(b_, Fp::FromHalves(yHalves - 2 * (int64_t)tileTop), 62);
            if (axl.zero()) axl.neg = a_.neg;
            if (byl.zero()) byl.neg = b_.neg;
            if (cfg.tileLocal == 2) {
                const int el = Fp::FromHalves(2 * 16).top();
                int e2 = ct.zero() ? NoExp : ct.top();
                if (a_.top() + el - 1 > e2) e2 = a_.top() + el - 1;
                if (b_.top() + el - 1 > e2) e2 = b_.top() + el - 1;
                unit = e2 - cfg.gridBits;
            }
            k = FloorUnits(ct, unit) + FloorUnits(axl, unit) + FloorUnits(byl, unit) + (int64_t)n * FloorUnits(a_, unit);
            if (cfg.tileLocal == 3) {       // every piece rounded down by itself: C, A Xt, B Yt, A x, B y
                Fp axt = Mul(a_, xt, 62), byt = Mul(b_, yt, 62);
                if (axt.zero()) axt.neg = a_.neg;
                if (byt.zero()) byt.neg = b_.neg;
                k = FloorUnits(c_, unit) + FloorUnits(axt, unit) + FloorUnits(byt, unit) + FloorUnits(axl, unit) + FloorUnits(byl, unit)
                    + (int64_t)n * FloorUnits(a_, unit);
            }
        }
        if (super) {
            const Fp half = Fp::FromHalves(1);
            Fp ah = Mul(a_, half, 62), bh = Mul(b_, half, 62);
            if (ah.zero()) ah.neg = a_.neg;
            if (bh.zero()) bh.neg = b_.neg;
            k += -(int64_t)n * FloorUnits(a_, unit) + (int64_t)(2 * n + hx) * FloorUnits(ah, unit);
            if (hy)
                k += FloorUnits(Add(c_, bh, 62, 400), unit) - FloorUnits(c_, unit);
        }
        // The value becomes a float again: 16 bits from its leading bit, cut toward minus infinity, with a signed
        // 8 bit exponent. Below 2^-128 the exponent stays -128 and the mantissa is kept
        // (dreamcast-tests/pvr-tests/uvmeas, mix wtiny: 37 readings where U/w passes through zero). -1 is every bit
        // set, with no leading bit to find: it gets the smallest exponent and a mantissa of -1 in the last place,
        // whatever the grid is, which is
        // -2^-143 (5 readings: a U/w of -1 unit of 2^-8 over a 1/w of 2^-17 reads texel 1023, like one of -1 unit
        // of 2^-138 over 2^-120; all that is measured is that it is below 2^-130). 0 stays 0: see Divide.
        Fp v;
        if (k == 0)
            return v;
        if (k == -1 && cfg.range)
            return Fp::Pow2(-143, true);
        v.neg = k < 0;
        uint64_t m = (uint64_t)(k < 0 ? -k : k);
        int bits = Fp::Bits(m), drop = bits > cfg.valueBits ? bits - cfg.valueBits : 0;
        if (cfg.valueFixed > drop)
            drop = cfg.valueFixed < 63 ? cfg.valueFixed : 63;
        if (drop) {
            uint64_t lost = m & ((1ull << drop) - 1);
            m >>= drop;
            if (lost && v.neg)
                m += 1;
        }
        v.mant = m;
        v.exp = unit + drop;
        if (cfg.range && v.top() - 1 < -128)
            v.exp += -128 - (v.top() - 1);
        return v;
    }
};

using Plane = PlaneT<PlaneConfig>;    // the run-time form, for the probes

// The TSP's reciprocal, read off a console for all 32768 16 bit mantissas (dreamcast-tests/pvr-tests/uvmeas, mix
// recipscan): a table over the 9 bits below the leading one, t[i] = ceil(2^32 / (64 i)) in half units, and a linear
// step over the low 6 bits with slope (t[i] - t[i+1] + c[i]) / 128; c[i] are the measured digits below.
struct RecipTable {
    int64_t t[513];
    int64_t n[512];
    RecipTable()
    {
        static const char c[] =
            "0111111111111111111111111011011111111111111110111101100100111111"
            "1110011111110011101011111111101111111110110111111111121010111111"
            "1011111111001111111111111101111101110101111111001111111111001101"
            "0111111111111111111210111110100101111111111001110001111111001111"
            "1111111111111011011110111001111210001111001111111122100011111111"
            "0110010110100011110100011111011110111111110100011111111121110001"
            "0111111111011111111111011111111111111211001110100110100111111111"
            "1011101101111111111110111010111110111111001100111111111111111110";
        for (int i = 0; i <= 512; i++) {
            uint64_t d = 64ull * (512 + i);
            t[i] = (int64_t)(((1ull << 32) + d - 1) / d);
        }
        for (int i = 0; i < 512; i++)
            n[i] = t[i] - t[i + 1] + (c[i] - '0');
    }
    // D: a 16 bit mantissa 32768..65535; the result likewise, a fraction of 65536 in [0.5, 1)
    int64_t Mantissa(int64_t D) const
    {
        int64_t i = (D >> 6) - 512;
        return (64 * t[i] - 1 - n[i] * (D & 63)) >> 7;
    }
};

// What the divider makes of a denominator below zero (dreamcast-tests/pvr-tests/uvmeas, mix wneg: 780,000 of 780,000
// readings with 1/w of -2^-6 .. -2^8 and of both signs in one triangle; the other-winding pages of wexp, whuge, wtiny).
// The leading bit stays where it is and the 15 bits under it are negated: a mantissa m of 16 bits (32768 .. 65535) is
// read as 32768 + (-m mod 32768), so -v between two powers of two reads as 3 * 2^p - v, and a power of two as
// itself. The sign is dropped: the quotient has the numerator's.
// A 1/w of -1 grid unit (every bit set, no leading bit: -2^-143, Plane::Value) is not normalized first: its mantissa
// is all ones at the smallest exponent, and with the leading bit forced it is read as 0xFFFF 2^-143, just under
// 2^-127 (dreamcast-tests/pvr-tests/texfar_m1: the first pixel of a line where 1/w is exactly -1 unit, under
// attribute / w from 2^-138 to 2^40, 1/w 2^-20 .. 2^100, both signs: 166 of 166 point-sampled readings with this
// and no other mantissa from 2^-160 to 2^-110; 2^-143 itself got 98 of 300 regions; texfar_m1q: the flat
// attribute's quotient there, -1 unit over -1 unit, a hair below 0, not -1: 0 of 300 regions with 2^-143, 300 so).
// The same as the rule above if the 15 bits are those of the two's complement mantissa, -1 being 0xFFFF. So no
// denominator is below 2^-128 and no quotient above about 2^243 (texfar_quot, texfar_m1q). The mipmap level's w
// there is this divider's too (MipSelect; dreamcast-tests/pvr-tests/tsp2_m1), and so are the colours' quotients:
// tsp2_m1 rows 10-14, Gouraud base and offset colours (a colour per corner, or one at all three) with 1/w of 2^-100
// .. 2^100, and the flat-shaded control: 300 of 300 regions, the colour/w there 0 (below zero, -1 unit too) or 255
// by its sign, every channel. GAP: a colour's quotient below 1 there (8 bit colours with in-range slopes do not get
// the colour/w below 2^-127 at that pixel).
inline Fp DenominatorSeen(const Fp& den)
{
    if (!den.neg || den.zero())
        return den;
    if (den.top() < -127) {                             // only -1 unit is below 2^-128 (Plane::Value)
        Fp d;
        d.mant = 0xffff;
        d.exp = -143;
        return d;
    }
    Fp d = den;
    d.neg = false;
    int bits = Fp::Bits(d.mant);
    // the 16 bits from the leading one (a value with more was cut by Plane::Value already: only zeros go)
    if (bits > 16) { d.mant >>= (bits - 16); d.exp += bits - 16; bits = 16; }
    if (bits < 16) { d.mant <<= 16 - bits; d.exp -= 16 - bits; }    // 32768 .. 65535
    d.mant = 32768 + ((65536 - d.mant) & 32767);
    return d;
}

// num / den as the TSP does it: num times the table reciprocal of den, the product of the two 16 bit mantissas
// cut down (toward minus infinity) at 17 fractional bits before it is normalized. A negative den: see
// DenominatorSeen. A num of 0 gives 0, whatever den is (dreamcast-tests/pvr-tests/hugepos, triangle c: the one pixel
// where both planes are exactly 0 is black). A den of 0 is read as 2^-128, like every number without a leading bit
// (dreamcast-tests/pvr-tests/uvmeas, mix wzero2: 32,784 readings on the line where 1/w is exactly 0; 2^-127 and 2^-129
// each get 23,000 fewer).
inline Fp Divide(const Fp& num, const Fp& den0)
{
    static const RecipTable tab;
    if (num.zero())
        return Fp();
    const Fp den = den0.zero() ? Fp::Pow2(-128, false) : DenominatorSeen(den0);
    // mantissas as fractions of 65536 in [0.5, 1): the top 16 bits
    auto norm16 = [](const Fp& v, int64_t* m) {
        int bits = Fp::Bits(v.mant);
        *m = bits >= 16 ? (int64_t)(v.mant >> (bits - 16)) : (int64_t)(v.mant << (16 - bits));
        return v.exp + bits;                            // the exponent as frexp gives it
    };
    int64_t mn, md;
    // num keeps all its bits (it has at most 17): scale to 16.x bits exactly
    int bn = Fp::Bits(num.mant);
    int en = num.exp + bn;
    int ed = norm16(den, &md);
    if (md < 32768) md = 32768;
    if (md > 65535) md = 65535;
    int64_t r = tab.Mantissa(md);
    // floor(mn * r * 131072) with mn = mant / 2^bn and r / 65536: mant * r * 2 / 2^bn, toward minus infinity
    (void)mn;
    pvrfp::u128 p = (pvrfp::u128)num.mant * (uint64_t)r * 2;
    uint64_t q = (uint64_t)(p >> bn);
    bool lost = (p & (((pvrfp::u128)1 << bn) - 1)) != 0;
    if (num.neg && lost)
        q += 1;
    Fp out;
    out.neg = num.neg;
    out.mant = q;
    out.exp = en + 1 - ed - 17;
    if (out.mant == 0)
        out.neg = false;
    return out;
}

// An interpolated colour as the TSP rounds it: q = attribute / w at the pixel (Divide), the attribute entered times
// 2^scaleExp (refsw_tile.h PlaneStepper3: a colour's 8 bit value times 2^-8); v = q in the attribute's own scale,
// below zero 0, above `max` max (255; 65535 for the cheap shadow's intensity, which comes before the clamp), and then
// floor(v + 1/2): to nearest, half way up (dreamcast-tests/pvr-tests/shade, 80,256 of 80,256 samples). A quotient that
// is an infinity or a NaN (a depth of 0, infinity or NaN at the pixel): `fallback`, the attribute itself
// (dreamcast-tests/pvr-tests/huge: a console still draws such a polygon in its colour). Shared with the probes of
// dreamcast-tests/model (covprobe shades the pages it reads as a console's: a sliver's colour, from planes that are
// garbage, is what decides whether covmeas counts its pixel).
inline int ColorRounded(const Fp& q, int scaleExp, int max, int fallback)
{
    if (q.bad)
        return fallback;
    if (q.neg || q.zero())
        return 0;
    const int e = q.exp - scaleExp;                     // v = mant * 2^e
    if (q.top() - scaleExp > 30)
        return max;
    // floor(v + 1/2) = floor((2 mant 2^e + 1) / 2)
    int64_t twice;
    if (e >= -1)
        twice = (int64_t)(q.mant << (e + 1));
    else {
        const int s = -(e + 1);
        twice = s >= 64 ? 0 : (int64_t)(q.mant >> s);
    }
    int64_t v = (twice + 1) >> 1;
    if (twice > 2 * (int64_t)max)                       // v above max (before the rounding): max
        return max;
    return (int)(v > max ? max : v);
}

// The mipmap level of a pixel and the coefficient trilinear filtering blends the next level in with
// (dreamcast-tests/pvr-tests/mipmeas and mip).
//
//   This is the D of the hardware manual: D = max(dx, dy), dx = sqrt(dudx^2 + dvdx^2), dudx = (a r - p c) /
//   (p X + q Y + r)^2 and so on, with u / w = a X + b Y + c, v / w = d X + e Y + f, 1 / w = p X + q Y + r.
//   d = K * w^2, in arithmetic of 8 bits:
//     K belongs to the polygon: the numerators |A_a C_w - C_a A_w| (along x) and |B_a C_w - C_a B_w| (along y)
//       for a = U and V, with the planes' coefficients before they are cut to 16 bits; the two of a direction
//       make one length, the larger plus 27146/65536 of the smaller (sqrt(2) - 1: the manual's square root of
//       the sum of squares; any other 16 bit value is worse); K is the longer of the two lengths, times the
//       texture's size and the bias, cut to 8 bits;
//     w is the TSP's reciprocal of the pixel's 1/w (the 16 bit table reciprocal, which is a hair low: 1 / 1.0
//       comes out as 0.FFFF), cut to 8 bits; w * w is cut to 8 bits, and so is K times it.
//   d = m * 2^L with m = 128 .. 255 of 128: level L, and m - 128 is the fraction, of 128. The coefficient is twice
//   the fraction; 127 counts as 128. Below level 0 there is no blending.
//   With 1/w = 1.0 exactly, w = 0.FF and w * w = 0.FE, which takes two units off m: a quad with a power of two
//   determinant has its level change at 2 + 1/32 texels per pixel, any other at 2.
// Measured: 12,000 cells (quads and triangles of many sizes, flat and sloped 1/w, turned textures, both DCalcCtrl
// settings, every bias): all the console's.
struct MipChoice {
    int level;      // 0: the largest
    int k;          // of 256: the share of level + 1 (trilinear pass B); pass A has 256 - k of this level
    Fp kFull;       // K before its cut to 8 bits (0: none); for the generators (pvr-tests/mipk)
};

// MIP_K (0: the model): 1 K as it was before (numerators of the divided coefficients, exactly, and the longer plus
// 27146 / 65536 of the shorter); 98, 99:
// K one step of 8 bits more, less (pvr-tests/mipk places its cases with them). A generator sets mipKVariant,
// refsw3-offline takes the environment's MIP_K
inline int mipKVariant = -1;
inline int MipK()
{
    if (mipKVariant < 0) {
        const char* e = getenv("MIP_K");
        mipKVariant = e ? atoi(e) : 0;
    }
    return mipKVariant;
}

// EXPERIMENT (MipSelect, environment MIP_DZ; a generator may set it; 0: the model): a determinant of zero read as
// 1 1/det = +0 (K a zero's: 2^(-127 + size); polly2-rtl's D4), 2 1/det = an infinity (K an infinity's, 2^(-128 +
// size), where a numerator is not zero, else a zero's)
inline int mipDzVariant = -1;

// first: null (DCalcCtrl 0: the numerators are taken at the screen's origin), or the first vertex's u, v, 1/w, x
// and y (DCalcCtrl 1: they are taken at that vertex).
template<class P>
inline MipChoice MipSelect(const P& u, const P& v, const P& w, const Fp& q, int sizeLog2, int biasQuarters, const uint32_t* first = nullptr)
{
    using namespace pvrfp;
    MipChoice r = { 0, 0, Fp() };
    const bool ranged = w.cfg.range != 0;
    if (biasQuarters <= 0 || u.bad || v.bad || w.bad)
        return r;
    if (!ranged && (q.zero() || q.neg))
        return r;
    // At the limits of the setup's numbers (dreamcast-tests/pvr-tests/mipmeas, mix wext: 2400 cells with 1/w of 2^-126
    // .. 2^127, below zero, zero, infinite and NaN). A numerator is made of the planes' sums BEFORE their division by
    // the determinant, a_u c_w - c_u a_w, and divided twice afterwards: it is an infinity as soon as one of those two
    // products reaches 2^128 (flat 1/w = z on a 32x32 quad: z^2 * 2^15.6 for 3 texels a pixel, so from z = 2^57),
    // and zero when the quotient is below 2^-126 (z below 2^-60). A sum of two terms keeps the rules of the
    // setup. K is the length times the bias and then gets the texture's size added to its exponent field, with
    // no look at what the field means: a zero (field 0) becomes 2^(-127 + size), an infinity (field 255) wraps
    // to 2^(-128 + size). So below 2^-60 the level climbs by two for each halving of z, up to the smallest
    // map, and above 2^56 it is the largest map with no blending. The sign of 1/w does not matter.
    auto above = [](const Fp& x) { return !x.zero() && x.top() - 1 > 127; };
    auto below = [](const Fp& x) { return !x.zero() && x.top() - 1 < -126; };
    auto inf = []() { Fp x; x.bad = true; return x; };
    // one product of two sums: 0 a number, 1 an infinity, -1 a zero (the value itself: minor, below)
    auto kind = [&](const Fp& x, const Fp& y) {
        if (!ranged) return 0;
        if (x.bad || y.bad) return ((!x.bad && x.zero()) || (!y.bad && y.zero())) ? -1 : 1;
        if (x.zero() || y.zero()) return -1;
        Fp m = Mul(x, y, 24);
        return above(m) ? 1 : below(m) ? -1 : 0;
    };
    // The console's numerators (dreamcast-tests/pvr-tests/mipk5: 1991 of 1991 cells; mipk): of the UNDIVIDED sums,
    // each product cut to 24 bits and their difference cut to 24 bits, all toward zero; the two lengths are made of
    // those, and the longer is then multiplied once by 1/det^2, the determinant's 24 bit reciprocal squared and cut to
    // 24 bits (where every coefficient divided first loses its own cut; the 1/w plane's C of a flat triangle whose
    // determinant is not a power of two is just under 1, and the console's numerators are as if it were 1)
    const Fp& det = w.Sdet;
    if (mipDzVariant < 0) {
        const char* e = getenv("MIP_DZ");
        mipDzVariant = e ? atoi(e) : 0;
    }
    const int mipDz = mipDzVariant;
    const bool infInv = det.zero() && !det.bad && mipDz == 2;
    Fp inv = (det.bad || det.zero()) ? Fp() : Recip(det, 24);
    if (ranged && below(inv))
        inv = Fp();
    const bool undivided = MipK() != 1 && ((!det.zero() && !inv.zero()) || infInv);
    Fp inv2 = Mul(inv, inv, 24);
    // 1/det^2 is a number of the setup's range like the others: below 2^-126 (a determinant above about 2^63) it is
    // zero, so the length is zero and the level the largest map without blending, whatever the numerators are
    // (polly2-data sydney_race, sydney_stadium: the kerb's triangles with two corners at about 2^36 and 1/w of 10^6,
    // determinants of 2^63.0 .. 2^66.8 on the largest map on every pixel, those of 2^62.9 and below as before;
    // dreamcast-tests pvr-tests/mipdump)
    if (ranged && below(inv2))
        inv2 = Fp();
    // |a1 b2 - b1 a2|: of the undivided sums sa1 .. sb2 (MIP_K 1: of the divided coefficients a1 .. b2)
    auto minor = [&](const Fp& a1, const Fp& b1, const Fp& a2, const Fp& b2, const Fp& sa1, const Fp& sb1, const Fp& sa2, const Fp& sb2, bool neg1, bool neg2) {
        const int k1 = kind(sa1, sb2), k2 = kind(sb1, sa2);
        if (k1 == 1 || k2 == 1) {
            if (k1 == 1 && k2 == 1 && neg1 == neg2)
                return Fp();                                // infinity minus infinity
            return inf();
        }
        // a product that is a zero by the sums (zero times anything, also times an infinity) is one here too
        if (undivided) {
            const Fp q1 = (k1 < 0 || sa1.bad || sb2.bad) ? Fp() : Mul(sa1, sb2, 24), q2 = (k2 < 0 || sb1.bad || sa2.bad) ? Fp() : Mul(sb1, sa2, 24);
            Fp n = Sub(q1, q2, 62, 200).cut(24);
            n.neg = false;
            n.bad = false;
            if (infInv)
                return n.zero() ? Fp() : inf();
            if (ranged) {                               // the limits as before: by the quotient
                const Fp nq = Mul(n, inv2, 62);
                if (below(nq)) return Fp();
                if (above(nq)) return inf();
            }
            return n;
        }
        const Fp p1 = (k1 < 0 || a1.bad || b2.bad) ? Fp() : Mul(a1, b2, 62), p2 = (k2 < 0 || b1.bad || a2.bad) ? Fp() : Mul(b1, a2, 62);
        Fp n = Sub(p1, p2, 62, 200);
        n.neg = false;
        n.bad = false;
        if (ranged && below(n))
            return Fp();
        if (ranged && above(n))
            return inf();
        return n;
    };
    // along x: |C_a A_w - A_a C_w| for U and for V; along y: |B_a C_w - C_a B_w|. Each pair is one length (about
    // the larger plus (sqrt(2) - 1) of the smaller); K is the longer of the two.
    auto length = [](const Fp& p, const Fp& q) {
        if (p.bad || q.bad) {
            Fp x; x.bad = true; return x;
        }
        const bool pLonger = AtMost(q, p);
        const Fp& hi = pLonger ? p : q;
        const Fp& lo = pLonger ? q : p;
        if (lo.zero())
            return hi;
        if (MipK() == 1) {                              // as it was: the longer plus 27146 / 65536 of the shorter
            Fp part = Mul(lo, Fp::FromHalves(2 * 27146), 62);
            part.exp -= 16;
            return Add(hi, part, 62, 200);
        }
        // the console's (dreamcast-tests/pvr-tests/mipk4, mipk3, mipk2: every cell): the two added, less 0x3F15F612
        // (0.585785985, a little under 2 - sqrt(2)) times the shorter, each of the three cut to 24 bits toward zero,
        // in the setup's adder (an operand 25 exponents below the other is dropped: a shorter that is a rounding's
        // residue leaves the longer as it is). (longer + shorter) - (2 - sqrt(2)) shorter is longer + (sqrt(2) - 1)
        // shorter, but with the sum's own cut: the part of the shorter falls as the shorter rises, until the sum steps up
        Fp k2;
        k2.mant = 0x95F612;
        k2.exp = -24;
        const Fp sum = Add(hi, lo, 62, 25).cut(24), less = Mul(lo, k2, 62).cut(24);
        return Sub(sum, less, 62, 25).cut(24);
    };
    // the planes' values where the numerators are taken: their constants, or the first vertex's u / w, v / w, 1 / w
    Fp pu = u.C0, pv = v.C0, pw = w.C0;
    Fp su = u.Sc, sv = v.Sc, sw = w.Sc;                 // the same as undivided sums
    if (first) {
        // DCalcCtrl 1: the planes evaluated at the first vertex, a x1 + b y1 + c, instead of their constants c
        // (the value at the screen's origin). For the limits, the undivided planes there, in the setup's
        // arithmetic: a flat 1/w = z overflows from 2^57 as with DCalcCtrl 0, and an infinite or NaN z1 gives the
        // levels it gives there (dreamcast-tests/pvr-tests/mipmeas, mix wextd: 2400 of 2400 cells). The vertex's own u1
        // z1, v1 z1, z1 give the same levels in every set measured; the plane is one path for both.
        auto load = [&](uint32_t u) { return Fp::FromSingle(u, ranged); };
        auto rmul = [&](const Fp& x, const Fp& y) {
            if (x.bad || y.bad) {
                const Fp& o = x.bad ? y : x;
                Fp z; z.neg = x.neg != y.neg;
                if (!o.bad && o.zero()) return z;
                z.bad = true; return z;
            }
            Fp m = Mul(x, y, 24);
            if (ranged && above(m)) { Fp z; z.bad = true; z.neg = m.neg; return z; }
            if (ranged && below(m)) { Fp z; z.neg = m.neg; return z; }
            return m;
        };
        const Fp z1 = load(first[2]);
        pu = rmul(load(first[0]), z1);
        pv = rmul(load(first[1]), z1);
        pw = z1;
        if (!ranged && (pu.bad || pv.bad || pw.bad))
            return r;
        // for the limits: the undivided planes evaluated at the first vertex, a x1 + b y1 + c, in the setup's
        // arithmetic (a triangle whose 1/w sums are all zero has a zero there, whatever z1 is)
        const Fp x1 = load(first[3]), y1 = load(first[4]);
        auto radd = [&](const Fp& x, const Fp& y) {
            if (x.bad || y.bad) {
                Fp z;
                if (x.bad && y.bad && x.neg != y.neg) return z;
                z.bad = true; z.neg = x.bad ? x.neg : y.neg; return z;
            }
            Fp m = Add(x, y, 24, 53);
            if (ranged && above(m)) { Fp z; z.bad = true; z.neg = m.neg; return z; }
            if (ranged && below(m)) return Fp();
            return m;
        };
        auto at1 = [&](const P& pl) { return radd(radd(rmul(pl.Sa, x1), rmul(pl.Sb, y1)), pl.Sc); };
        su = at1(u); sv = at1(v); sw = at1(w);
        if (!x1.bad && !y1.bad) {
            auto val1 = [&](const P& pl) { return Add(Add(Mul(pl.A0, x1, 62), Mul(pl.B0, y1, 62), 62, 200), pl.C0, 62, 200); };
            pu = val1(u); pv = val1(v); pw = val1(w);
        }
    }
    auto sgn = [](const Fp& x, const Fp& y) { return x.neg != y.neg; };
    Fp lx = length(minor(pu, u.A0, pw, w.A0, su, u.Sa, sw, w.Sa, sgn(su, w.Sa), sgn(u.Sa, sw)),
                   minor(pv, v.A0, pw, w.A0, sv, v.Sa, sw, w.Sa, sgn(sv, w.Sa), sgn(v.Sa, sw)));
    Fp ly = length(minor(u.B0, pu, w.B0, pw, u.Sb, su, w.Sb, sw, sgn(u.Sb, sw), sgn(su, w.Sb)),
                   minor(v.B0, pv, w.B0, pw, v.Sb, sv, w.Sb, sw, sgn(v.Sb, sw), sgn(sv, w.Sb)));
    Fp best = (lx.bad || (!ly.bad && AtMost(ly, lx))) ? lx : ly;
    // A determinant of zero (the setup's: three equal corners, or a sliver whose products, cut to 24 bits, cancel;
    // the edge unit draws both) makes a length of all ones, as if beyond every number: whatever the numerators are,
    // zero or not, and whatever w is, the level is the last one, and its fraction is that of K's 8 bits 0xFF (times
    // the D adjust) times w * w's 8 bits (below). dreamcast-tests/pvr-tests/tsp2_mipdz (1/w 2^-65 .. 2^-56) and
    // tsp2_mipdz2 (1/w 2^-127 .. 2^126): 4,800 points and 3,722 such slivers each (1,078 points where no sliver was
    // found; 882 in mipdz2), 15 rows of sizes 8 .. 256, D adjust 1/4 .. 15/4, DCalcCtrl 1, bilinear, U to 2^20, 1/w
    // below zero; the level, and trilinear pass B's fraction: 1,200 of 1,200 regions each. 1/det = +0 (polly2-rtl's
    // decision D4: K = 2^(-127 + size)) got 59 of tsp2_mipdz's, 1/det = an infinity (2^(-128 + size)) 36: both put
    // the largest map there; a length of 1.FFFFFF 2^127, the largest number, is the console's up to 1/w = 2^48 and
    // then comes down from the last level, which the console does not (mipdz2: the columns of 1/w 2^64, 2^96, 2^126
    // wrong in every row). (refsw3 divided by zero here: Recip of 0.)
    const bool dzForce = det.zero() && !det.bad && mipDz == 0;
    if (dzForce) {
        best = Fp();
        best.mant = 0xffffff;
        best.exp = -23;
    }
    if (undivided && !infInv && !dzForce && !best.bad && !best.zero()) {
        best = Mul(best, inv2, 24);                     // the longer length, once times 1/det^2
        if (ranged && above(best)) best = inf();
        else if (ranged && below(best)) best = Fp();
    }
    Fp k8, kFull;
    if (best.bad) {
        if (!ranged) return r;
        k8 = Fp::Pow2(-128 + sizeLog2, false);          // field 255 plus the size, wrapped
        kFull = k8;
    } else if (best.zero()) {
        if (!ranged) return r;
        k8 = Fp::Pow2(-127 + sizeLog2, false);          // field 0 plus the size
        kFull = k8;
    } else {
        k8 = Mul(best, Fp::FromHalves(2 * (int64_t)biasQuarters), 62);
        k8.exp += sizeLog2 - 2;
        kFull = k8;
        k8 = k8.cut(8);
        r.kFull = kFull;
        if (MipK() == 98 || MipK() == 99) {                 // one step of 8 bits more, less
            const int b = Fp::Bits(k8.mant), sh = b > 8 ? b - 8 : 0;
            uint64_t m = (k8.mant >> sh) + (MipK() == 98 ? 1 : -1);
            k8.mant = m << sh;
        }
    }
    Fp one = Fp::FromHalves(2);
    Fp wFull = Divide(one, q);
    // w is the divider's reciprocal of the pixel's 1/w, at a 1/w of exactly -1 grid unit (Plane::Value's -2^-143)
    // too, which the divider reads as 0xFFFF 2^-143 (DenominatorSeen): dreamcast-tests/pvr-tests/tsp2_m1 (texfar_m1's
    // triangles with mipmapped textures of 8 .. 256, D adjust 1/4 .. 15/4, DCalcCtrl 1, U flat or sloped, 1/w 2^-20
    // .. 2^100; the level, trilinear passes A and B): 600 of 600 regions; that 1/w read as 2^-143 itself (mantissa
    // 0x8000) or as a zero (2^-128): 200 (the level the last either way, every fraction wrong).
    wFull.neg = false;
    Fp w8 = wFull.cut(8);
    w8.neg = false;
    // w * w does not leave the range: its exponent stops at 127 and the mantissa is kept
    // (dreamcast-tests/pvr-tests/mipmeas, mix wext: with K = 2^-121 the level is the last one from 1/w = 2^-64 all the
    // way down, and with K = 2^-122, where every sum of the setup is an infinity, it is the one before the last with
    // the fraction of w * w)
    Fp ww = Mul(w8, w8, 8);
    if (ranged && !ww.zero() && ww.top() - 1 > 127)
        ww.exp -= (ww.top() - 1) - 127;
    Fp d = Mul(k8, ww, 8);
    int bits = Fp::Bits(d.mant);
    int L = d.exp + bits - 1;                                   // floor(log2(d))
    if (dzForce)
        L = 1 << 16;                                            // a determinant of zero: past the last level (above)
    int M = (int)(bits >= 8 ? d.mant >> (bits - 8) : d.mant << (8 - bits));          // m: 128 .. 255
    // EXPERIMENT (environment MIP_ARITH; 0 or unset: the measured arithmetic above). Other ways to the level, to
    // be scored against the console's mipmeas sets: 1 exponents only; 2 a log with the mantissa taken as linear
    // (log2 m ~ m - 1, and back the same way); 4 K * w^2 uncut; 5 w * w not cut; 6 K * w^2 rounded. (3, an exact log and
    // antilog of the 8 bit K and w in doubles, went with refsw3's last doubles on 2026-11-19: dreamcast-tests README
    // has its score.)
    static const int mipArith = [] { const char* e = getenv("MIP_ARITH"); return e ? atoi(e) : 0; }();
    if (mipArith && !k8.zero() && !w8.zero()) {
        auto top8 = [](const Fp& x, int& e) {
            const int b = Fp::Bits(x.mant);
            e = x.exp + b - 1;
            return (int)(b >= 8 ? x.mant >> (b - 8) : x.mant << (8 - b));
        };
        int eK, eW;
        const int mK = top8(k8, eK), mW = top8(w8, eW);
        if (mipArith == 1) {
            L = eK + 2 * eW;
            M = 128;
        } else if (mipArith == 2) {
            const int t = (eK * 128 + (mK - 128)) + 2 * (eW * 128 + (mW - 128));
            L = t >> 7;
            M = 128 + (t & 127);
        } else if (mipArith == 5) {
            // the model without its cut of w * w to 8 bits (a log domain squares by a shift and rounds nothing there)
            Fp dd = Mul(k8, Mul(w8, w8, 62), 8);
            const int b = Fp::Bits(dd.mant);
            L = dd.exp + b - 1;
            M = (int)(b >= 8 ? dd.mant >> (b - 8) : dd.mant << (8 - b));
        } else if (mipArith == 6) {
            // the model with the product K * w^2 rounded to nearest instead of cut
            Fp dd = Mul(k8, ww, 62);
            const int b = Fp::Bits(dd.mant);
            L = dd.exp + b - 1;
            M = (int)(b >= 8 ? (dd.mant + (1ull << (b - 9))) >> (b - 8) : dd.mant << (8 - b));
            if (M > 255) { M = 128; L++; }
        } else if (mipArith == 4) {
            Fp dd = Mul(kFull, Mul(wFull, wFull, 62), 62);
            const int b = Fp::Bits(dd.mant);
            L = dd.exp + b - 1;
            M = (int)(b >= 8 ? dd.mant >> (b - 8) : dd.mant << (8 - b));
        }
    }
    if (L < 0)
        return r;
    int f = M - 128;
    r.level = L;
    r.k = f == 127 ? 256 : 2 * f;
    return r;
}

} // namespace pvrtsp
