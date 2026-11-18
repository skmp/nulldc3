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

struct DepthConfig {
    int productBits = 24;       // z * (difference), z * e, a * x0, b * y0
    int sumBits = 24;
    int reach = 25;             // the adders' alignment reach (measured on the edge unit)
    int recipBits = 24;         // 1 / determinant
    int coefBits = 24;          // a * inv, b * inv, c * inv, start * inv
    int startBits = 20;
    int slopeBits = 20;
    int pairBits = 21;
    int tileBits = 26;
    int storedBits = 19;
    // the start cut to the slopes' grid too: 1 any start (a console: zstart), 2 a negative one only, 0 not (the
    // model before); 0 and 2 are kept for the generators and depthprobe --scan
    int startOnSlopes = 1;
    // The setup's numbers have the limits measured for the TSP's (pvr_tsp_plane.h, Plane::Setup): exponent
    // field 0 is zero and 255 infinity, a result of 2^128 or more is an infinity and one below 2^-126 a zero,
    // with the same rules for sums and products of them. Not measured here number by number: with them
    // dreamcast-tests/pvr-tests/huge is the console's in 250 of 250 regions (228 without), including the quads of which
    // a console draws one triangle only at 2^127, the largest number, infinity and NaN (the two triangles'
    // sums come to infinities of opposite sign). range 0: no limits, as before.
    int range = 1;
};

using pvrfp::Recip;

struct DepthTile {
    bool valid = false, block = false;
    float first_z = 0;                                  // not valid: the depth every pixel gets
    // block form: twice the depth in units of 2^unit is d0 + sx2 * dx + sy2 * dy, with sx2, sy2 the steps from
    // the tile's first pixel center in half pixels
    pvrfp::i128 d0 = 0, dx = 0, dy = 0;
    int unit = 0;
    // float form (a plane of zero)
    Fp A, B, C;
    int left = 0, top = 0;
    DepthConfig cfg;

    // x, y, z of the three corners in rasterizer order; (left, top): the tile's first pixel
    void Setup(const float x[3], const float y[3], const float z[3], int tileLeft, int tileTop, const DepthConfig& config)
    {
        using namespace pvrfp;
        cfg = config;
        left = tileLeft; top = tileTop;
        first_z = z[0];
        const int PB = cfg.productBits, SB = cfg.sumBits, RE = cfg.reach;
        Fp X[3], Y[3], Z[3];
        const bool ranged = cfg.range != 0;
        auto inf = [](bool neg) { Fp r; r.bad = true; r.neg = neg; return r; };
        auto zero = [](bool neg) { Fp r; r.neg = neg; return r; };
        auto load = [&](float f) {
            if (!ranged)
                return Fp::FromFloat(f);
            uint32_t u;
            memcpy(&u, &f, 4);
            const uint32_t field = (u >> 23) & 0xff;
            const bool neg = (u >> 31) != 0;
            if (field == 0) return zero(neg);
            if (field == 255) return inf(neg);
            Fp r; r.neg = neg; r.mant = (u & 0x7fffff) | 0x800000; r.exp = (int32_t)field - 150;
            return r;
        };
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
            first_z = 0.0f;
            return;
        }
        if (ranged && !det.zero() && !det.bad) {
            // the depth of the tile's first pixel with these numbers; an infinity is stored as one
            Fp inv0 = Recip(det, cfg.recipBits);
            Fp x00 = Fp::FromHalves(2 * (int64_t)left + 1), y00 = Fp::FromHalves(2 * (int64_t)top + 1);
            Fp first0 = mul(add(add(mul(a, x00), mul(b, y00)), c), inv0);
            if (first0.bad || a.bad || b.bad || c.bad) {
                valid = false; block = false;
                first_z = first0.bad ? (first0.neg ? -INFINITY : INFINITY) : (float)first0.cut(cfg.storedBits).toDouble();
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
                first_z = (float)mean.cut(cfg.storedBits).toDouble();
            }
            return;
        }
        Fp inv = Recip(det, cfg.recipBits);
        A = Mul(a, inv, cfg.coefBits);
        B = Mul(b, inv, cfg.coefBits);
        C = Mul(c, inv, cfg.coefBits);
        Fp x0 = Fp::FromHalves(2 * (int64_t)left + 1), y0 = Fp::FromHalves(2 * (int64_t)top + 1);
        Fp first = Mul(Add(Add(Mul(a, x0, PB), Mul(b, y0, PB), SB, RE), c, SB, RE), inv, cfg.coefBits);
        int pair = A.top() > B.top() ? A.top() : B.top();
        if (first.zero() && pair == pvrfp::NoExp)
            return;                                     // a plane of zero: the float form, which gives 0
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
    float At(int i, int j, bool centers) const
    {
        using namespace pvrfp;
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
            const int PB = cfg.productBits, SB = cfg.sumBits, RE = cfg.reach;
            Fp X = Fp::FromHalves(2 * (int64_t)(left + i) + (centers ? 1 : 0)), Y = Fp::FromHalves(2 * (int64_t)(top + j) + (centers ? 1 : 0));
            v = Add(Add(Mul(A, X, PB), Mul(B, Y, PB), SB, RE), C, SB, RE);
        }
        return (float)v.cut(cfg.storedBits).toDouble();
    }
};

} // namespace pvrisp
