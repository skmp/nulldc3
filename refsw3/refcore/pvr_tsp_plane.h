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
      - a determinant of zero: the plane is flat at the third corner's value (measured for colors).
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
struct PlaneConfig {
    int productBits = 24, sumBits = 24, reach = 25, recipBits = 24;
    int coefBits = 16;          // A, B, C
    int gridBits = 22;          // under the tile's exponent
    int valueBits = 16;         // the plane's value at the pixel
    // EXPERIMENT (dreamcast-tests/model/tspprobe valueFixed= / wfixed=; default 0): drop at least this many low bits of
    // the grid's integer, as a fixed field would, instead of only those below 16 bits from the leading one
    int valueFixed = 0;
    // The numbers of the setup have an exponent field of 8 bits: a result is at least 2^expLow and below
    // 2^(expHigh + 1), or it is a zero or an infinity (see Plane::Setup). range 0: no limits, the arithmetic as
    // it was before dreamcast-tests/pvr-tests/uvmeas measured them (mixes wexp, whuge, wtiny, wspec, wneg).
    int range = 1, expLow = -126, expHigh = 127;
    // The last addition of a sum of three terms, (t1 + t2) + t3: of t1 + t2 and t3, one that is zero counts as
    // 2^-127 there when the other is below 2^(-127 + zeroReach), that is, has the smallest exponent there is
    // (see Plane::Setup).
    int zeroReach = 2;
    // EXPERIMENT (model/tspprobe tileLocal=): 0 a run's coordinates are the screen's (measured: every uvmeas reading);
    // 1 they are the tile's own: the tile's constant C + A Xt + B Yt (exact, rounded down once) plus A x and B y with
    // x, y counted from the tile's corner, on the same grid (15,801 readings wrong); 2 the same with the grid chosen
    // from the tile's constant and the tile's own coordinates (1,277,606); 3 every piece rounded down by itself (111,580)
    int tileLocal = 0;
    // EXPERIMENT (model/tspprobe macOrder=): 0 C, A x and B y each rounded down by itself (measured); 1 C + B y rounded
    // together, then A x added rounded (a MAC for the line, then one for the run); 2 C + A x together, B y alone;
    // 3 all three together, rounded once
    int macOrder = 0;
    // EXPERIMENT (model/tspprobe sumA=, sumW=): the order of the three terms of an attribute plane's slope sums (sumA)
    // and of the 1/w plane's (sumW): 0 (t0 + t1) + t2 (measured), 1 (t0 + t2) + t1, 2 (t1 + t2) + t0. sumA 2 gives set
    // dump_gundam1 too, but 22,582 readings of ugen5 and 1,621 of wtiny1 wrong: the reach does it (above).
    int sumA = 0, sumW = 0;
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

struct Plane {
    Fp A, B, C;
    Fp A0, B0, C0;              // the same before the cut to coefBits: what the mipmap level is made of
    Fp Sa, Sb, Sc;              // the three sums before they are divided by the determinant (MipSelect)
    Fp Sdet;                    // and the determinant
    bool bad = false;           // an infinity or a NaN among the inputs: not handled here
    PlaneConfig cfg;

    // x, y, z (1/w) of the corners; attr: the attribute at each corner for an attribute / w plane, or null
    void Setup(const float x[3], const float y[3], const float z[3], const float* attr, const PlaneConfig& config)
    {
        using namespace pvrfp;
        cfg = config;
        const int PB = cfg.productBits, SB = cfg.sumBits, RE = cfg.reach;
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
        //             signs they are +0. A result of 2^128 or more is an infinity, one below 2^-126 is +0 (as an
        //             exact cancellation is).
        //   the last addition of a sum of three terms, (t1 + t2) + t3: when one of the two is zero and the other
        //             2^-126 .. 2^-125, the zero is added as 2^-127 with its sign: -2.5 * 2^-127 + 0 is
        //             -1.5 * 2^-127, which is below the range and so +0, and the slope made of it has the other
        //             sign. (As if that adder's path for operands one exponent apart did not know a zero.)
        //             Measured in the sign of slopes that underflow: 5 cells of uvmeas wtiny1 with a zero third
        //             term, 21 of cmeas cwext1 with either. In the first addition, t1 + t2, a zero is a zero (one
        //             cell of cwext1 tells). Whether the cut is at 2^-125 or 2^-124 is not told (zeroReach 2 or 3).
        const bool ranged = cfg.range != 0;
        auto inf = [](bool neg) { Fp r; r.bad = true; r.neg = neg; return r; };
        auto zero = [](bool neg) { Fp r; r.neg = neg; return r; };
        auto above = [&](const Fp& v) { return !v.zero() && v.top() - 1 > cfg.expHigh; };
        auto below = [&](const Fp& v) { return !v.zero() && v.top() - 1 < cfg.expLow; };
        auto load = [&](float f) {
            if (!ranged)
                return Fp::FromFloat(f);
            uint32_t u;
            memcpy(&u, &f, 4);
            const uint32_t field = (u >> 23) & 0xff;
            const bool neg = (u >> 31) != 0;
            if (field == 0)
                return zero(neg);
            if (field == 255)
                return inf(neg);
            Fp r;
            r.neg = neg;
            r.mant = (u & 0x7fffff) | 0x800000;
            r.exp = (int32_t)field - 150;
            return r;
        };
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
        auto addTerm = [&](const Fp& p0, const Fp& q0, bool third) {
            if (!ranged)
                return Add(p0, q0, SB, RE);
            if (p0.bad || q0.bad) {
                if (p0.bad && q0.bad && p0.neg != q0.neg)
                    return zero(false);
                return inf(p0.bad ? p0.neg : q0.neg);
            }
            Fp p = p0, q = q0;
            if (third) {
                auto smallest = [&](const Fp& v) { return !v.zero() && v.top() - 1 < -127 + cfg.zeroReach; };
                if (q0.zero() && smallest(p0)) { q.mant = 1; q.exp = -127; }
                if (p0.zero() && smallest(q0)) { p.mant = 1; p.exp = -127; }
            }
            Fp r = Add(p, q, SB, RE);
            if (above(r)) return inf(r.neg);
            if (below(r)) return zero(false);
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
        const int* o = ord[attr ? cfg.sumA : cfg.sumW];
        Fp a = sum3(ta[o[0]], ta[o[1]], ta[o[2]]);
        Fp b = sum3(tb[o[0]], tb[o[1]], tb[o[2]]);
        Fp c = attr ? sum3(tc[0], tc[1], tc[2]) : sum3(tc[0], tc[2], tc[1]);
        Fp det = sum3(e[0], e[1], e[2]);
        Sa = a; Sb = b; Sc = c; Sdet = det;
        if (det.zero() && !det.bad) {
            // flat at the third corner's value (dreamcast-tests/pvr-tests/covmeas, mix degen --vcol; for 1 / w a guess)
            A = Fp(); B = Fp();
            C = attr ? mul(AT[2], Z[2]).cut(cfg.coefBits) : Z[2].cut(cfg.coefBits);
            A0 = A; B0 = B; C0 = C;
            return;
        }
        // The reciprocal has the range too: of a determinant above 2^126 it is below 2^-126 and so zero, and of an
        // infinite one it is zero. Every coefficient is then a zero with the sign of its sum times the
        // determinant's (dreamcast-tests/pvr-tests/hugepos: triangle c, determinant -2^127.4, is black, the quotient of
        // two planes below zero; b, +2^127.7, is white exactly where x <= y, where -2^-127 x + 2^-127 y + 2^-127 is
        // above zero; a, whose determinant is an infinity from corners of 2^62 on, is white all over).
        Fp inv = det.bad ? zero(det.neg) : Recip(det, cfg.recipBits);
        if (ranged && below(inv))
            inv = zero(inv.neg);
        A0 = mul(a, inv); B0 = mul(b, inv); C0 = mul(c, inv);
        A = A0.cut(cfg.coefBits);
        B = B0.cut(cfg.coefBits);
        C = C0.cut(cfg.coefBits);
    }

    // The plane at a pixel. x0Halves, yHalves: the sample point of the run's first pixel and of the line, in half
    // pixels (absolute); n: whole pixels from the run's first pixel; tileLeft, tileTop: the tile's first pixel.
    Fp Value(int64_t x0Halves, int64_t yHalves, int n, int tileLeft, int tileTop) const
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
inline Fp DenominatorSeen(const Fp& den)
{
    if (!den.neg || den.zero())
        return den;
    Fp d = den;
    d.neg = false;
    int bits = Fp::Bits(d.mant);
    // the 16 bits from the leading one (a value with more was cut by Plane::Value already: only zeros go)
    if (bits > 16) { d.mant >>= (bits - 16); d.exp += bits - 16; bits = 16; }
    while (bits < 16) { d.mant <<= 1; d.exp--; bits++; }            // 32768 .. 65535
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
    unsigned __int128 p = (unsigned __int128)num.mant * (uint64_t)r * 2;
    uint64_t q = (uint64_t)(p >> bn);
    bool lost = (p & (((unsigned __int128)1 << bn) - 1)) != 0;
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
};

// first: null (DCalcCtrl 0: the numerators are taken at the screen's origin), or the first vertex's u, v, 1/w, x
// and y (DCalcCtrl 1: they are taken at that vertex).
inline MipChoice MipSelect(const Plane& u, const Plane& v, const Plane& w, const Fp& q, int sizeLog2, int biasQuarters, const float* first = nullptr)
{
    using namespace pvrfp;
    MipChoice r = { 0, 0 };
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
    // one product of two sums: 0 a number, 1 an infinity, -1 a zero (the value itself is taken from the divided
    // coefficients below, as before)
    auto kind = [&](const Fp& x, const Fp& y) {
        if (!ranged) return 0;
        if (x.bad || y.bad) return ((!x.bad && x.zero()) || (!y.bad && y.zero())) ? -1 : 1;
        if (x.zero() || y.zero()) return -1;
        Fp m = Mul(x, y, 24);
        return above(m) ? 1 : below(m) ? -1 : 0;
    };
    // |a1 b2 - b1 a2| of the divided coefficients; s..: the same four as undivided sums, for the limits
    auto minor = [&](const Fp& a1, const Fp& b1, const Fp& a2, const Fp& b2, const Fp& sa1, const Fp& sb1, const Fp& sa2, const Fp& sb2, bool neg1, bool neg2) {
        const int k1 = kind(sa1, sb2), k2 = kind(sb1, sa2);
        if (k1 == 1 || k2 == 1) {
            if (k1 == 1 && k2 == 1 && neg1 == neg2)
                return Fp();                                // infinity minus infinity
            return inf();
        }
        // a product that is a zero by the sums (zero times anything, also times an infinity) is one here too
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
    // along x: |C_a A_w - A_a C_w| for U and for V; along y: |B_a C_w - C_a B_w|. Each pair is one length,
    // the larger plus a part of the smaller; K is the longer of the two.
    // the two numerators of a direction as one length: the larger plus (sqrt(2) - 1) of the smaller
    auto length = [](const Fp& p, const Fp& q) {
        if (p.bad || q.bad) {
            Fp x; x.bad = true; return x;
        }
        const Fp& hi = p.toDouble() >= q.toDouble() ? p : q;
        const Fp& lo = p.toDouble() >= q.toDouble() ? q : p;
        if (lo.zero())
            return hi;
        Fp part = Mul(lo, Fp::FromHalves(2 * 27146), 62);       // 27146 / 65536
        part.exp -= 16;
        return Add(hi, part, 62, 200);
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
        auto load = [&](float f) {
            if (!ranged)
                return Fp::FromFloat(f);
            uint32_t bits;
            memcpy(&bits, &f, 4);
            const uint32_t field = (bits >> 23) & 0xff;
            Fp x;
            x.neg = (bits >> 31) != 0;
            if (field == 255) x.bad = true;
            else if (field != 0) { x.mant = (bits & 0x7fffff) | 0x800000; x.exp = (int32_t)field - 150; }
            return x;
        };
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
        auto at1 = [&](const Plane& pl) { return radd(radd(rmul(pl.Sa, x1), rmul(pl.Sb, y1)), pl.Sc); };
        su = at1(u); sv = at1(v); sw = at1(w);
        if (!x1.bad && !y1.bad) {
            auto val1 = [&](const Plane& pl) { return Add(Add(Mul(pl.A0, x1, 62), Mul(pl.B0, y1, 62), 62, 200), pl.C0, 62, 200); };
            pu = val1(u); pv = val1(v); pw = val1(w);
        }
    }
    auto sgn = [](const Fp& x, const Fp& y) { return x.neg != y.neg; };
    Fp lx = length(minor(pu, u.A0, pw, w.A0, su, u.Sa, sw, w.Sa, sgn(su, w.Sa), sgn(u.Sa, sw)),
                   minor(pv, v.A0, pw, w.A0, sv, v.Sa, sw, w.Sa, sgn(sv, w.Sa), sgn(v.Sa, sw)));
    Fp ly = length(minor(u.B0, pu, w.B0, pw, u.Sb, su, w.Sb, sw, sgn(u.Sb, sw), sgn(su, w.Sb)),
                   minor(v.B0, pv, w.B0, pw, v.Sb, sv, w.Sb, sw, sgn(v.Sb, sw), sgn(sv, w.Sb)));
    Fp best = (lx.bad || (!ly.bad && lx.toDouble() >= ly.toDouble())) ? lx : ly;
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
    }
    Fp one = Fp::FromHalves(2);
    Fp wFull = Divide(one, q);
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
    int M = (int)(bits >= 8 ? d.mant >> (bits - 8) : d.mant << (8 - bits));          // m: 128 .. 255
    // EXPERIMENT (environment MIP_ARITH; 0 or unset: the measured arithmetic above). Other ways to the level, to
    // be scored against the console's mipmeas sets: 1 exponents only; 2 a log with the mantissa taken as linear
    // (log2 m ~ m - 1, and back the same way); 3 exact log and antilog of the 8 bit K and w; 4 K * w^2 uncut; 5 w * w not cut; 6 K * w^2 rounded.
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
        } else if (mipArith == 3) {
            const double t = std::log2(mK / 128.0) + eK + 2 * (std::log2(mW / 128.0) + eW);
            L = (int)std::floor(t);
            M = (int)std::floor(std::exp2(t - L) * 128);
            if (M > 255) M = 255;
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
