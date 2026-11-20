/*
    The ISP's inside test, in the setup units' own arithmetic (pvr_softfp.h).

    Everything here was measured on a console with dreamcast-tests/pvr-tests/covmeas, hugepos, faredge and the tests
    named next to each rule; the README there has the evidence for each rule. The parameters of EdgeConfig are
    what the probes of dreamcast-tests/model scan.

    Per edge a -> b, with the tile's first sample point (x0, y0):
        c     = xb ya - xa yb                       24 bit products, 24 bit difference
        first = dx y0 - dy x0                       24 bit products; an operand 25 exponents below the other is
                                                    dropped by the adder
                                                    a product or a sum of these that reaches 2^128 becomes a value
                                                    that swamps every other (two of one sign cancel exactly)
        start = first + c                           24 bits, then kept as a 20 bit float; saturates at 2^128 to 2^127
        steps = dx per row, -dy per column          20 bit floats, cut to 21 bits under the larger's exponent
        E     = top(larger step) + 6, or top(start) + 1 if that is more, and at most 129;  ZERO: |v| <= 2^(E - 21)
    and the decision:
        R = value of the row at the tile's first column. R zero: the whole row of the tile is decided by the sign of
            the column step, or by that of the row step when the column step is zero as well.
        otherwise the sign of the pixel's value; a zero pixel is inside when the next pixel to its right is not
        negative; in the tile's last column, when the column step is positive.
        An edge between two equal corners is zero everywhere and counts as inside.
    An edge whose E was cut to 129 (a step of 2^124 or more) keeps its start and steps in units of 2^(E - 27), cut toward
    zero, and is walked down its tile from the polygon's first row in it (EdgeTile::Walk): R wraps in [-2^131, 2^131);
    R and the column step of one sign decide the row; else R + 16 sx, the value at pixel 16, wraps in [-2^131, 2^131)
    as well, and a row whose value there is outside it is R + sign(R) 2^132; the edge ends (the rows after are outside)
    when the row's far value, R + (32 sx mod 2^131) with a column step above zero, R + 2^131 with one below zero that is
    a multiple of 2^126, else R, falls from above ZERO to ZERO or below. A dx or dy that reaches 2^128 steps by +-2^127.
    The winding, and the determinant the culling tests, are the setup's determinant with its products and sums that
    reach 2^128 swamping the rest as the start's do, zero counting as positive (EdgeWinding).
*/
#pragma once
#include "pvr_softfp.h"

namespace pvrisp {

using pvrfp::Fp;

// The edge unit's numbers as the hardware has them, baked in (pvr_softfp.h: C<N>). Every one measured with
// dreamcast-tests/pvr-tests/covmeas (98 archived sets, 0 differences), hugepos and faredge; EdgeConfig below is the same with
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
    static constexpr pvrfp::C<128> overflowExp{};   // a product or sum of the start that reaches 2^overflowExp swamps the rest;
    static constexpr pvrfp::C<127> saturateExp{};   // the start then saturates to 2^saturateExp
    static constexpr std::true_type lastColumnByStep{};   // a zero pixel in column 31: inside iff the column step is positive
    static constexpr pvrfp::C<129> blockMax{};      // E is at most this
    static constexpr pvrfp::C<27> cutUnitBits{};    // an edge whose E was cut keeps its values in units of 2^(E - cutUnitBits)
    static constexpr pvrfp::C<131> walkExp{};       // the walk of an edge whose E was cut keeps R and R + 16 sx in
                                                    // [-2^walkExp, 2^walkExp) (= blockMax + 2), 32 sx below 2^walkExp
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
    int blockMax = EdgeHW::blockMax;
    int cutUnitBits = EdgeHW::cutUnitBits;
    int walkExp = EdgeHW::walkExp;
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

// The winding the edges are turned to: the sign of the determinant as Winding() has it, but with every product and sum
// that reaches 2^128 swamping the rest, as in the edges' start (EdgeTile::Setup): two such values of one sign cancel
// exactly, so that the sign comes from what is left. Measured with dreamcast-tests/pvr-tests/faredge_m131 (pages 6-15:
// 1,878 slivers whose two long edges run to a corner at y +-2^127 .. +-2^127 (1 + 3/32)): with the plain determinant
// 296 tiles of the test are wrong, with this one 0; they are triangles whose plain and swamped determinants differ in
// sign, drawn by the console as the swamped one turns them. Held out: faredge_ecap pages 6-15 (1,957 such slivers of
// another seed): plain 1,007 tiles wrong, swamped 0; faredge_walk 647 and faredge_far32 408 against 0. The culling uses
// this determinant too, its size and its sign (*detOut): dreamcast-tests/pvr-tests/faredge_cull (300 triangles, the
// same on four pages with CullMode none, small, negative, positive; in 240 the plain and the swamped determinant have
// opposite signs): every one is culled by the facing of the swamped determinant (12 with it negative by the negative
// mode, 14 positive by the positive mode), and the 214 whose swamped products cancel to 0 by size, in modes 1-3 alike;
// the 60 controls as before. With the plain determinant 484 of the test's 1,200 regions are wrong.
template<class Cfg>
inline int EdgeWinding(const Fp x[3], const Fp y[3], const Cfg& cfg, Fp* detOut = nullptr)
{
    const int OE = cfg.overflowExp;
    auto swamp = [&](const Fp& v) { return v.atLeastPow2(OE) ? Fp::Pow2(OE + 72, v.neg) : v; };
    auto e = [&](int j, int k) {
        return swamp(pvrfp::Sub(swamp(pvrfp::Mul(x[j], y[k], cfg.productBits)), swamp(pvrfp::Mul(x[k], y[j], cfg.productBits)), cfg.sumBits, cfg.reach));
    };
    Fp e1 = e(1, 2), e2 = e(2, 0), e3 = e(0, 1);
    Fp det = swamp(pvrfp::Add(swamp(pvrfp::Add(e1, e2, cfg.sumBits, cfg.reach)), e3, cfg.sumBits, cfg.reach));
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
    // An edge whose block exponent was cut (a step of 2^124 or more): its rows are decided by the walk down the tile
    // (Walk), in the edge's units (2^102, twice): f0 the start, fx the column step, fy the row step, h 2^131, z ZERO.
    bool big = false;
    int firstRow = 0;
    int64_t f0 = 0, fx = 0, fy = 0, h = 0, z = 0;
    uint32_t rowMask[32] = {};

    // a -> b, sgn from EdgeWinding(); (x0Halves, y0Halves): the tile's first sample point in half pixels
    template<class Cfg>
    void Setup(const Fp& xa, const Fp& ya, const Fp& xb, const Fp& yb, int64_t x0Halves, int64_t y0Halves, int sgn, const Cfg& cfg)
    {
        using namespace pvrfp;
        constant = true;
        constantInside = false;
        big = false;
        firstRow = 0;
        lastColumnByStep = cfg.lastColumnByStep;
        if (xa.bad || ya.bad || xb.bad || yb.bad)
            return;                                     // an infinity or a NaN: nothing is inside
        const auto PB = cfg.productBits;
        const auto SB = cfg.sumBits;
        const auto RE = cfg.reach;
        // Overflow, measured with dreamcast-tests/pvr-tests/faredge_cmax (16 pages, one edge per tile, 4,800 of 4,800
        // tiles) and hugepos: a product of the start (xb ya, xa yb, dx y0, dy x0), or the sum c or first, that reaches
        // 2^128 is replaced by a value that swamps every other operand of the adds (here 2^(128 + 72), far beyond the
        // adder's reach), so that the start saturates to 2^127 with its sign. It is the product that counts, not c or
        // the start: faredge page 4's first-column strip (its edge v1 -> v2: xa yb = 2^128.23, the start 2^127.68:
        // saturated, the strip not drawn) and page 1's control strip (xb ya = 2^128.12, the start 2^125.62: 2^127,
        // the edge 18 pixels into the tile, 9 more than exact); faredge_cmax group A (4,446 tiles, among them 755
        // where one product is exactly 2^128, cut to 24 bits, and the other takes 2^105 from c: c and the start below
        // 2^128, saturated all the same). Two such values of one sign cancel exactly: faredge_cmax page 11 (c's
        // products 2^128.5 and 2^128.0 of one sign: the start is first alone, its sign changes with the row at y = 181
        // as first's does), pages 14 and 15 (c = -2^139.7 and dx y0 = +2^128.05 in the tile's first row: 0, the row
        // decided by the column step; the rows below by j dx), and faredge_k132's RV tiles (4,560: near-vertical edges
        // with |far y| past 2^127 in columns 1-19, c's product and dy x0 past 2^128 with opposite signs: the start 0,
        // the rows j dx). As hugepos had it, first past 2^128 gives the start its sign, c past 2^128 its own.
        const auto OE = cfg.overflowExp;
        auto swamp = [&](const Fp& v) { return v.atLeastPow2(OE) ? Fp::Pow2(OE + 72, v.neg) : v; };
        Fp c = swamp(Sub(swamp(Mul(xb, ya, PB)), swamp(Mul(xa, yb, PB)), SB, RE));
        // A coordinate difference that reaches 2^128 swamps too in the products of the start (first), but steps by
        // +-2^127, the saturated value: dreamcast-tests/pvr-tests/hugepos_v4, _v5, _v6 (differences past 2^128: as an
        // infinite step instead, 68,397 / 67,565 pixels of hugepos_v5 / v6 wrong), hugepos_cov pages 6-9 and 26-29,
        // hugepos_sw2 pages 7-10, 18-21 and 29-32, hugepos_rules page 8 (|dx| or |dy| of 2^128 .. 1.75 2^128; hugepos's
        // triangles a and c are not drawn, a at 2^125 with dx = -2^128 exactly has its 4 pixels in row 0 only).
        Fp dx = Sub(xa, xb, SB, RE), dy = Sub(ya, yb, SB, RE);
        const Fp dxP = swamp(dx), dyP = swamp(dy);
        dx = dx.atLeastPow2(OE) ? Fp::Pow2(cfg.saturateExp, dx.neg) : dx;
        dy = dy.atLeastPow2(OE) ? Fp::Pow2(cfg.saturateExp, dy.neg) : dy;
        Fp x0 = Fp::FromHalves(x0Halves), y0 = Fp::FromHalves(y0Halves);
        const auto GD = cfg.guard;
        Fp first = swamp(Add(swamp(Mul(dxP, y0, PB)), -swamp(Mul(dyP, x0, PB)), SB, RE, GD));
        Fp start = Add(first, c, SB, RE, GD);
        if (start.atLeastPow2(OE))
            start = Fp::Pow2(cfg.saturateExp, start.neg);
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
        // E at most 129 (dreamcast-tests/pvr-tests/faredge_m131 pages 0-5, columns 1-19: edges with the column step
        // -(2^127 + d 2^108) and a start of 0, the rows R = j dx: R from 2^112 up is not ZERO, pixel 0 is drawn
        // (E = 134 made rows up to 2^113 ZERO: 1,165 of the 1,800 tiles wrong, 0 with the cut); faredge_k132's RV
        // tiles: 2^108 exactly is ZERO, 2^108 + 2^104 not). Which cut: faredge_ecap pages 0-5 (1,440 such edges with
        // |far y| in [2^123, 2^127), so that E is 130 .. 133 before the cut, the rows R = j dx crossing 2^107 .. 2^112
        // in row 4, 8 or 16): 129 gives every tile, 130 has 1,424 wrong, 131, 132 or no cut 1,440. With a row step of
        // 2^124 or more and a small column step it is the walk below that shows it (hugepos_rules pages 0-3, 1,200 tiles
        // of the H family, every one as modelled).
        const bool cut = E > cfg.blockMax;
        if (cut)
            E = cfg.blockMax;
        // The exponent of the steps' last bit: 21 bits under the larger step's exponent; for an edge whose E was cut,
        // 2^(E - 27) (2^102), the steps and the start cut toward zero there, the same units for the walk (Walk).
        // Measured with dreamcast-tests/pvr-tests/edge2_walk pages 3-5 (900 tiles, one triangle per tile: a near-
        // horizontal edge to a far corner at x 2^124 .. 2^128, its column step with bits below 2^104 and near zero in a
        // row of the tile, kept where the alternatives differ): with 2^(E - 27) every tile, 2^(E - 26) 240 wrong,
        // 2^(E - 28) 428, 2^(ep - 25) 435, 2^(ep - 26) 478, 2^(ep - 27) 557, the steps floored 135, the earlier
        // 2^(ep - 21) for the rows and 2^(ep - 27) for the walk 339 (faredge_m131 alone allows 2^101 .. 2^103; the
        // hugepos_* and faredge_* sets are exact with it). hugepos_rules page 11 (10 tiles) was this: its slivers'
        // column step 629,145 2^106 (0.3 2^127) lost its last bit in 2^(ep - 21) = 2^107, and pixel 7 of row 2 came out
        // +2^109, inside; it is +2^106, in the ZERO band, a tie that the next pixel (negative) makes outside.
        const int unit = cut ? E - cfg.cutUnitBits : ep - cfg.pairBits;
        if (E - unit > 56) {
            // the start is 2^30 times a step or more: no pixel of the tile is near zero or changes sign
            constantInside = !s20.neg;
            return;
        }
        sx = sx.cutBelow(unit);
        sy = sy.cutBelow(unit);
        auto unitsAt = [&](const Fp& v, bool* frac, int unit) -> int64_t {      // floor(v / 2^unit); *frac: not a whole number
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
        int64_t s0 = unitsAt(s20, &frac, unit);
        t0 = 2 * s0 + (frac ? 1 : 0);
        // An edge whose E was cut: the start cut toward zero to the unit, no half unit (edge2_walk pages 3-5: 5 tiles
        // with a start just below zero, -120.x units, whose pixel at -64.x units, just outside ZERO when floored, is
        // drawn: in the band as -64; floored, with or without the half unit, 5 tiles wrong, cut toward zero 0)
        if (cut)
            t0 = 2 * unitsAt(s20.cutBelow(unit), nullptr, unit);
        tx = 2 * unitsAt(sx, nullptr, unit);
        ty = 2 * unitsAt(sy, nullptr, unit);
        zero2 = (int64_t)2 << (E - cfg.zeroShift - unit);
        tie2 = (int64_t)2 << (E - cfg.tieShift - unit);
        constant = false;
        // The rows of an edge whose E was cut (a step of 2^124 or more: E is 129) are walked in the same units, 2^102
        // (the values up to 2^134 in at most 2^32 of them; the walk's register [-2^131, 2^131) is 2^29 of them).
        big = cut && ep >= cfg.blockMax - 5;
        if (big) {
            f0 = t0;
            fx = tx;
            fy = ty;
            h = (int64_t)2 << (cfg.walkExp - unit);
            z = zero2;
            Walk();
        }
    }

    // The first row of the tile the polygon has (RowRange), 0 when it starts above the tile: where the walk begins.
    void SetFirstRow(int64_t row)
    {
        const int s = row < 0 ? 0 : row > 31 ? 31 : (int)row;
        if (big && s != firstRow) {
            firstRow = s;
            Walk();
        }
    }

    bool InsideOrdinary(int i, int j) const
    {
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

    // the pixels of a row whose value at the tile's first column is r (fine units)
    uint32_t RowMask(int64_t r) const
    {
        if (r >= -z && r <= z)
            return fx > 0 ? ~0u : 0;
        uint32_t m = 0;
        for (int i = 0; i < 32; i++) {
            const int64_t v = r + fx * i;
            bool in;
            if (v > z)
                in = true;
            else if (v < -z)
                in = false;
            else if (i == 31 && lastColumnByStep)
                in = fx > 0;
            else
                in = v + fx >= -z;
            if (in)
                m |= 1u << i;
        }
        return m;
    }

    // The walk of an edge whose block exponent was cut, down its tile from the polygon's first row in it (SetFirstRow).
    // Measured with dreamcast-tests/pvr-tests/faredge_walk (16 pages, 3,537 tiles, one triangle per tile, made by
    // agents/nulldc3/hunt/faredge/r3/m/gen3.cpp where the alternatives differ), faredge_m131, faredge_ecap, faredge_k132,
    // faredge_cmax, faredge, the hugepos_* sets and edge2_walk, edge2_end, edge2_mult (scored by agents/nulldc3/hunt/
    // edge2/score.cpp: the 43,612 tiles of the sets of one triangle per tile all exact; regress.sh has the others);
    // per row j, R = start + j dx:
    //  - R is held in [-2^131, 2^131): it wraps by 2^132 (hugepos_rules pages 0-3: the H family's long edges, a column
    //    step of 0 and rows R = start + j dx of -2^131 .. -2^132 inside, as R + 2^132; faredge_m131 page 11 tile (1,9),
    //    rows with R past +-2^132 drawn as R -+ 2^132; faredge_walk pages 8-11: a polygon whose first row in the tile
    //    has R past 2^131 is wrapped like any other).
    //  - R (as wrapped) and the column step of one sign, or a column step of 0: the row is on R's side; else the row's
    //    value at pixel 16, M = R + 16 sx, is held in [-2^131, 2^131) too, and a row whose M is outside it is
    //    evaluated as R + sign(R) 2^132 (the push: faredge_k132 group K, 240 tiles, the switch row where |R + 16 sx|
    //    reaches 2^131; faredge_m131 pages 0-5, 1,800 tiles: -2^131 exactly is not pushed, +2^131 is; faredge's K 127
    //    vertical triangles, modvol; the far edge of K, R = 2^127 and the step of one sign, stays inside). Since R is
    //    wrapped first, only a column step past 2^127 takes M out of range.
    //  - The edge ends (every row after this one is outside) when its far value falls from above ZERO to ZERO or below:
    //    the row's largest value, R + 32 sx with a column step above zero (at pixel 32), else R, where 32 sx is held
    //    below 2^131: its bits from 2^131 up are lost (32 sx mod 2^131). For an ordinary edge (32 sx < 2^131) that is
    //    only the row going outside to its end, which a later row cannot undo; with a column step of 2^126 or more it
    //    ends the edge early. It is what made the rows after the one whose value at pixel 32 is at most 2^132 dark in
    //    faredge_m131 pages 6-15 (32 sx = 2^132 + rho: the far value is R + rho, at most 0 there; 9,694 of 9,708 rows,
    //    the 14 others in slivers that start inside the tile, which count from their own first row), the slivers of
    //    hugepos_rules page 11 and hugepos_v3 page 6 whose line ends after the row whose value at pixel 32 is at most
    //    2^131 (32 sx = 2^131 + rho), the lines of faredge_walk pages 4-7 (32 sx past 3 2^131) and hugepos_sw3 pages 36
    //    and 37 (two edges wrapped past 2^131 that keep a one-pixel line between them when rho is just above the size of
    //    the wrapped R; sw3 pages 0 and 9, where 32 sx is 2^132 and 3 2^131, rho 0, do not), and the edge of
    //    hugepos_rules page 0 tile (0,0) whose R falls from +2^127 to 0 (outside after it, not wrapped back past -2^131).
    //    With 32 sx kept whole 15,863 tiles of all sets are wrong, with 0 counting as above 0 7,869; counted from row
    //    0 instead of the polygon's first row 1,462 (faredge_walk 1,351). Above ZERO, not above 0: edge2_walk pages
    //    3-5, 12 tiles (an edge with a start of 0 and a column step of one unit, R + 32 sx = 2^107 in its first row: it
    //    does not end in the next row; with above 0 the 12 wrong). With a column step below zero the far value is R
    //    alone (482 tiles of these sets need the end there; R + sx, pixel 1, 54 of them wrong, R + 32 sx 623).
    //  - With a column step below zero that is a multiple of 2^126 (32 sx = 0 mod 2^131: -2^126, -2^127, -1.5 2^127;
    //    a saturated difference or not) the far value is R + 2^131: the edge ends only where R falls to -2^131 + ZERO or
    //    below. dreamcast-tests/pvr-tests/edge2_walk pages 0-1 (600 tiles: two far corners on opposite sides in y, |dy|
    //    from 2^128, a saturated column step of -2^127) and page 2 (61 tiles: a far corner at y = +-2^127 exactly):
    //    every one goes on after the row where R falls from +15.x 2^127 to -15.x 2^127 (the end with R alone has all
    //    661 wrong; hugepos_v5 page 0 tile (0,7), 79 pixels, was one); faredge_ecap pages 7 and 9 tiles (0,4) and (0,9),
    //    where R reaches -2^131 exactly, do end (with no end at all 2 tiles wrong). pvr-tests/edge2_end, 280 tiles, all
    //    as modelled: page 0, 56 tiles with -2^127 (R alone: 56 wrong); page 1, 37 whose R reaches -2^131 exactly or
    //    nearly (no end at all: 37 wrong; R alone 4); page 2, 55 with -2^126 and -1.5 2^127 (R + 2^131 for -2^127
    //    alone: 55 wrong); page 3, 66 with -2^127 (1 +- 2^-19 .. 2^-8), R alone (R + 2^131 within 2^117 of -2^127: 66
    //    wrong); page 4, 66 where +2^127 ends with R alone (R + 2^131 with +2^127 too: 66 wrong). Odd multiples of
    //    2^125 and 2^124 end with R alone: pvr-tests/edge2_mult, 58 tiles (-2^125, -3, -5, -7 2^125 and -1 .. -15 2^124:
    //    R + 2^131 for multiples of 2^125 29 wrong, of 2^124 58). One datapath gives exactly this: 32 sx reaches the
    //    far value's adder as a sign and its bits below 2^131, a negative one made two's complement by invert + 1 on
    //    those bits, which are then gated off by the sign while the carry out of the + 1 (there exactly when the bits
    //    are all zero) is not and lands at 2^131. It is the same function of sx as the rule above, so no test can
    //    separate the two; the mechanism is a reading, the rule is what is measured (edge2_walk, edge2_end, edge2_mult
    //    re-run 2026-11-20 after the reboot: 2,100, 1,500 and 600 of 600 regions as modelled).
    //  - A difference at 2^128 steps by +-2^127 (Setup), so its edge is walked as any other with a step past 2^124.
    // The walk's units are the edge's, 2^102 (Setup). Not known: why row 31 starts a row earlier (refsw_tile.cpp).
    void Walk()
    {
        const int s = firstRow;
        const int64_t H = h, Z = z;
        auto wrap = [&](int64_t v) { int64_t w = v % (2 * H); if (w >= H) w -= 2 * H; else if (w < -H) w += 2 * H; return w; };
        // 32 sx below 2^131 with a column step above zero; 2^131 with one below zero that is a multiple of 2^126
        // (32 sx = 0 mod 2^131; see above), else 0
        const int64_t far32 = fx > 0 ? (32 * fx) % H : fx < 0 && (-32 * fx) % H == 0 ? H : 0;
        bool dead = false, wasAbove = false;
        for (int j = 0; j < 32; j++) {
            uint32_t ord = 0;
            for (int i = 0; i < 32; i++)
                if (InsideOrdinary(i, j))
                    ord |= 1u << i;
            rowMask[j] = ord;
            if (j < s)
                continue;
            if (dead) {
                rowMask[j] = 0;
                continue;
            }
            const int64_t r = f0 + fy * j, rw = wrap(r);
            const bool above = rw + far32 > Z;
            const bool endsAfter = j > s && wasAbove && !above;
            wasAbove = above;
            uint32_t m;
            if (rw >= -Z && rw <= Z)
                m = rw == r ? ord : (fx > 0 ? ~0u : fx == 0 ? (fy > 0 ? ~0u : 0) : 0);
            else if (fx == 0 || (rw > 0) == (fx > 0))
                m = rw == r ? ord : (rw > 0 ? ~0u : 0);
            else {
                const int64_t mid = rw + 16 * fx;
                if (mid >= H || mid < -H)
                    m = RowMask(rw + (rw > 0 ? 2 * H : -2 * H));
                else
                    m = rw == r ? ord : RowMask(rw);
            }
            rowMask[j] = m;
            if (endsAfter)
                dead = true;
        }
    }

    bool Inside(int i, int j) const
    {
        if (constant)
            return constantInside;
        if (big)
            return rowMask[j] >> i & 1;
        return InsideOrdinary(i, j);
    }

    // no pixel of row j of the tile is inside (cheap; false when in doubt)
    bool RowOutside(int j) const
    {
        if (constant)
            return !constantInside;
        if (big)
            return rowMask[j] == 0;
        int64_t r = t0 + ty * j;
        if (r >= -zero2 && r <= zero2)
            return !((tx >= -tie2 && tx <= tie2) ? ty > 0 : tx > 0);
        return r < -zero2 && r + tx * 31 < -zero2;
    }
};

} // namespace pvrisp
