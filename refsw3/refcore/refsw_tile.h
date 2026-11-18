#pragma once
/*
	This file is part of libswirl
*/
#include "license/bsd"

#include "hw/pvr/pvr_regs.h"
#include "hw/pvr/pvr_mem.h"
#include "core_structs.h"
#include "pvr_isp_edge.h"
#include "pvr_isp_depth.h"
#include "pvr_tsp_plane.h"



#include "refsw_lists.h"

f32 f16(u16 v);     // a 16 bit float (libswirl: rend/soft/refrend_base.cpp)

// refsw3's core (refcore): in a namespace of its own, beside rend/refsw2, which has the same names
namespace refcore {

// For texture cache

#define MAX_RENDER_WIDTH 32
#define MAX_RENDER_HEIGHT 32
#define MAX_RENDER_PIXELS (MAX_RENDER_WIDTH * MAX_RENDER_HEIGHT)

#define STRIDE_PIXEL_OFFSET MAX_RENDER_WIDTH


typedef float    ZType;
typedef u8       StencilType;
typedef u32      ColorType;
/*
    Surface equation solver
*/
// keep `bits` bits of mantissa, toward zero (the setup arithmetic's rounding)
static inline double SetupTrunc(double v, int bits)
{
    u64 u;
    memcpy(&u, &v, sizeof(u));
    u &= ~((1ull << (53 - bits)) - 1);
    memcpy(&v, &u, sizeof(u));
    return v;
}

/*
    A TSP plane: 1/w, or an attribute times 1/w.

    Fitted to a console through the fog table, which shows the TSP's 1/w directly (dreamcast-tests/pvr-tests/tspw), and
    through the color of steeply sloped Gouraud quads (dreamcast-tests/pvr-tests/zblock, ztile):
      - the plane is set up like the ISP's depth plane (24 bit truncating arithmetic, determinant form on
        absolute screen coordinates, vertices taken as v1, v3, v2), but its three coefficients are then held
        as floats with a 16 bit mantissa
      - a pixel's value is (A x + B y) + C on absolute pixel coordinates, in 20 bit arithmetic; there is no
        per-tile block exponent here, unlike the ISP
    With a steep plane far from the screen's origin the constant C is large against the values on screen, so
    its 16 bits leave a visible error: 1/w comes out up to 0.4 % low at x = 480 for a plane falling from 32 to
    0.5 across a tile, and a flat color of 255 comes out as 254 in places.
    TSP_COEF_BITS / TSP_EVAL_BITS: 16 and 20 reproduce 98 % of 23840 fog readings exactly (99.5 % within one
    step) and 564 of 666 of the console's 254s with 24 false ones.
*/
#ifndef TSP_COEF_BITS
#define TSP_COEF_BITS 16
#endif
#ifndef TSP_EVAL_BITS
#define TSP_EVAL_BITS 20
#endif

// Measured on a console with dreamcast-tests/pvr-tests/uvmeas (plane values read through a wrapping texture coordinate,
// 2.3 million readings, 99.85 % reproduced) and dreamcast-tests/pvr-tests/wmeas (1/w read through the fog table):
//   - setup as for the ISP depth (24 bit truncating arithmetic, sums (v1 + v2) + v3), except that the
//     constant of an attribute plane is also summed (v1 + v2) + v3; the constant of the 1/w plane is summed
//     (v1 + v3) + v2 like the depth's
//   - the three coefficients are kept as 16 bit floats, cut toward zero
//   - a line of a triangle in a tile is evaluated once per RUN of pixels the triangle is visible on (it starts
//     anew after a pixel of another polygon; dreamcast-tests/pvr-tests/uvspan, 411287 of 411287 readings): the value at
// the     run's first pixel is C + floor(A x0) + floor(B y), and each further pixel adds A, all rounded DOWN to
//     one grid. The grid is 22 bits under an exponent E that is fixed for the triangle in the tile: the
//     largest of exp(C), exp(A) + exp(X) - 1 and exp(B) + exp(Y) - 1, X and Y being the tile's far half
//     (tile + 16 .. tile + 32, one exponent), exponents as frexp gives them. A product or step that is a zero
//     with a negative sign (a slope that cancelled exactly, on a triangle whose determinant is negative)
//     counts as one grid unit down.
//   - the value is then cut to 16 bits (down)
//   - an attribute is that times the reciprocal of 1/w; the reciprocal has a 16 bit mantissa in [1, 2), cut
//     down, so the reciprocal of an exact power of two comes out one unit under; the product keeps 17 bits,
//     rounded down. With 1/w = 1 an attribute therefore arrives one unit of its 16th bit low.
struct PlaneStepper3
{
    double ddx, ddy;            // A and B
    double c;                   // C, at the screen's origin
    double left, top;
    float fallback = 0;         // the attribute itself, for depths of 0, infinity or NaN
    int scaleExp = 0;           // the attribute enters the plane times 2^scaleExp (colours: see IPs3::Setup)
    // The plane in the setup units' own arithmetic (pvr_tsp_plane.h): what is used whenever the inputs are
    // finite. ddx, ddy, c above are the same coefficients as doubles, kept for inputs that are not.
    pvrtsp::Plane soft;
    bool useSoft = false;

    // attr: a plane of attribute / w, the attribute being a1, a2, a3 at the corners (false: the plane of 1 / w).
    // An attribute plane is made of the products of the 1/w plane, each multiplied by the corner's attribute and
    // cut again: a_i * (w_i * d), not (a_i * w_i) * d (dreamcast-tests/pvr-tests/uvmeas, mix wflat: 600 of 600 flat
    // cases).
    void Setup(taRECT *rect, const Vertex& v1, const Vertex& v2, const Vertex& v3, bool attr, double a1, double a2, double a3)
    {
        if (attr && scaleExp) {
            a1 = ldexp(a1, scaleExp); a2 = ldexp(a2, scaleExp); a3 = ldexp(a3, scaleExp);
        }
        auto T = [](double v) { return SetupTrunc(v, 24); };
        const double x1 = v1.x, y1 = v1.y, z1 = v1.z;
        const double x2 = v2.x, y2 = v2.y, z2 = v2.z;
        const double x3 = v3.x, y3 = v3.y, z3 = v3.z;
        const double v1_a = attr ? a1 : z1;
        {
            static const pvrtsp::PlaneConfig planeConfig;
            const float sx[3] = { v1.x, v2.x, v3.x }, sy[3] = { v1.y, v2.y, v3.y }, sz[3] = { v1.z, v2.z, v3.z };
            const float sa[3] = { (float)a1, (float)a2, (float)a3 };
            soft.Setup(sx, sy, sz, attr ? sa : nullptr, planeConfig);
            useSoft = !soft.bad;
        }

        left = rect->left;
        top = rect->top;

        double e1 = T(T(x2 * y3) - T(x3 * y2));
        double e2 = T(T(x3 * y1) - T(x1 * y3));
        double e3 = T(T(x1 * y2) - T(x2 * y1));
        double ta[3] = { T(z1 * T(y2 - y3)), T(z2 * T(y3 - y1)), T(z3 * T(y1 - y2)) };
        double tb[3] = { T(z1 * T(x3 - x2)), T(z2 * T(x1 - x3)), T(z3 * T(x2 - x1)) };
        double tc[3] = { T(z1 * e1), T(z2 * e2), T(z3 * e3) };
        if (attr) {
            const double at[3] = { a1, a2, a3 };
            for (int i = 0; i < 3; i++) {
                ta[i] = T(at[i] * ta[i]);
                tb[i] = T(at[i] * tb[i]);
                tc[i] = T(at[i] * tc[i]);
            }
        }
        double a = T(T(ta[0] + ta[1]) + ta[2]);
        double b = T(T(tb[0] + tb[1]) + tb[2]);
        double cc = attr ? T(T(tc[0] + tc[1]) + tc[2])
                         : T(T(tc[0] + tc[2]) + tc[1]);
        double det = T(T(e1 + e2) + e3);

        if (det == 0 && std::isfinite(a) && std::isfinite(b) && std::isfinite(cc)) {
            // A determinant of zero (three equal corners, three corners on a line, a sliver whose constants
            // cancel): the plane is flat at the THIRD corner's value. A console shows the third corner's color
            // for such a triangle, Gouraud shaded or not (dreamcast-tests/pvr-tests/covmeas, mix degen --vcol: 11,456
            // pixels). For 1 / w that is a guess (not measured).
            ddx = ddy = 0;
            c = attr ? SetupTrunc(T(a3 * z3), TSP_COEF_BITS) : SetupTrunc(z3, TSP_COEF_BITS);
            return;
        }
        if (det == 0 || !std::isfinite(det) || !std::isfinite(a) || !std::isfinite(b) || !std::isfinite(cc)) {
            ddx = ddy = 0;
            c = v1_a;
            return;
        }

        double inv = T(1.0 / det);
        ddx = SetupTrunc(T(a * inv), TSP_COEF_BITS);        // the sign of a zero is kept: 0 * negative = -0
        ddy = SetupTrunc(T(b * inv), TSP_COEF_BITS);
        c = SetupTrunc(T(cc * inv), TSP_COEF_BITS);
    }

    // t rounded down to a multiple of lsb; a negative zero is one unit down
    static double FloorTo(double t, double lsb)
    {
        double q = floor(t / lsb);
        if (t == 0 && std::signbit(t))
            q -= 1;
        return q * lsb;
    }

    // keep `bits` bits of mantissa, rounded down (toward minus infinity)
    static double FloorBits(double v, int bits)
    {
        if (v == 0 || !std::isfinite(v))
            return v;
        int e;
        frexp(v, &e);
        double l = ldexp(1.0, e - bits);
        return floor(v / l) * l;
    }

    // The TSP's reciprocal, read off a console for all 32768 16 bit mantissas (dreamcast-tests/pvr-tests/uvmeas, mix
    // recipscan): a table over the 9 bits below the leading one, t[i] = ceil(2^32 / (64 i)) in half units, and a linear
    // step over the low 6 bits with slope (t[i] - t[i+1] + c[i]) / 128; c[i] are the measured digits below.
    struct RecipTable
    {
        s64 t[513];
        s64 n[512];
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
                u64 d = 64ull * (512 + i);
                t[i] = (s64)(((1ull << 32) + d - 1) / d);
            }
            for (int i = 0; i < 512; i++)
                n[i] = t[i] - t[i + 1] + (c[i] - '0');
        }
        // D: a 16 bit mantissa 32768..65535; the result likewise, as a fraction of 65536 in [0.5, 1)
        s64 Mantissa(s64 D) const
        {
            s64 i = (D >> 6) - 512;
            return (64 * t[i] - 1 - n[i] * (D & 63)) >> 7;
        }
    };

    // num / den as the TSP does it (both 16 bit floats): num times the table reciprocal of den; the product of
    // the two mantissas is cut down (toward minus infinity) at 17 fractional bits, before it is normalized.
    static double Divide(double num, double den)
    {
        // The soft planes (pvr_tsp_plane.h) never give a zero, an infinity or a NaN at a pixel; a 1/w below zero
        // is the divider's business (pvrtsp::DenominatorSeen). The first line is for the planes in double only.
        if (!std::isfinite(den) || !std::isfinite(num))
            return num / den;
        return pvrtsp::Divide(pvrfp::Fp::FromDouble(num), pvrfp::Fp::FromDouble(den)).toDouble();
    }

    // Column (in the tile) of the first pixel of the run of pixels being shaded: set by the loop over the tags.
    static int SpanStart;

    static int Exp(double v)
    {
        if (v == 0)
            return -100000;
        int e;
        frexp(v, &e);
        return e;
    }

    // x, y relative to the tile, including the offset of the sample point in the pixel
    double IpD(float x, float y) const
    {
        double fx = floor(x);
        double n = fx - SpanStart;                          // whole pixels from the first pixel of the run
        if (useSoft) {
            double fy = floor(y);
            int64_t x0h = 2 * (int64_t)(left + SpanStart) + (x - fx != 0 ? 1 : 0);
            int64_t yh = 2 * (int64_t)(top + fy) + (y - fy != 0 ? 1 : 0);
            return soft.Value(x0h, yh, (int)n, (int)left, (int)top).toDouble();
        }
        double X0 = left + SpanStart + (x - fx), Y = top + y;
        if (!std::isfinite(c) || !std::isfinite(ddx) || !std::isfinite(ddy))
            return ddx * (left + x) + ddy * Y + c;
        int e = std::max(Exp(c), std::max(Exp(ddx) + Exp(left + 16.0) - 1, Exp(ddy) + Exp(top + 16.0) - 1));
        if (e < -50000)
            e = 0;                                          // a plane that is zero everywhere
        double lsb = ldexp(1.0, e - 22);
        double v = FloorTo(c, lsb) + FloorTo(ddx * X0, lsb) + FloorTo(ddy * Y, lsb) + n * FloorTo(ddx, lsb);
        return FloorBits(v, 16);
    }

    float Ip(float x, float y) const
    {
        return (float)IpD(x, y);
    }

    // invW: the 1/w plane at the pixel
    float Ip(float x, float y, float invW) const
    {
        const double q = Divide(IpD(x, y), invW);
        const float f = (float)q;
        // a quotient below zero stays below zero however small: its texel is the one before 0
        // (dreamcast-tests/pvr-tests/uvmeas, mix whuge: U/w overflowed on the other winding reads texel 1023)
        if (f == 0 && q < 0)
            return -1e-37f;
        return f;
    }

    // In the planes a colour is a fraction: the 8 bit value times 2^-8. With ordinary depths that changes
    // nothing (every product just has another exponent); it shows where the setup's numbers reach their limits
    // (dreamcast-tests/pvr-tests/cmeas, mix wext): at 1/w = 2^110 a colour of 0x86 is still 0x86 (as an integer its
    // products would be infinities), and where every product is an infinity or a zero the quotient is just under 1,
    // which is white. Measured: -8 gets 2,333,331 of 2,340,000 colour values, -7 1,688,587 and -9 2,124,860
    // (before the last rule of the sums went in).
    static constexpr int ColorExp = -8;

    float IpU8(float x, float y, float W) const
    {
        float rv = ldexpf(Ip(x, y, W), -scaleExp);

        // attribute * depth / depth is undefined when the depth is 0, infinite or not a number; a console
        // still draws such a polygon in its color (dreamcast-tests/pvr-tests/huge)
        if (std::isnan(rv))
            return fallback;

        if (rv < 0) rv = 0;
        if (rv > 255) rv = 255;

        return rv;
    }

    // as IpU8, but not cut at 255 (at 65535): for the cheap shadow's intensity, which comes before the clamp
    float IpWide(float x, float y, float W) const
    {
        float rv = ldexpf(Ip(x, y, W), -scaleExp);
        if (std::isnan(rv))
            return fallback;
        if (rv < 0) rv = 0;
        if (rv > 65535) rv = 65535;
        return rv;
    }
};

/*
    The depth plane as the hardware's setup unit computes it.

    Fitted to a console (dreamcast-tests/pvr-tests/precision: 20880 of 20880 depth comparisons between flat polygons
    with different vertices, sizes, positions and depths):
      - every operation keeps a 24 bit mantissa and truncates toward zero
      - the plane is built from the edge constants on absolute screen coordinates:
            e1 = x2 y3 - x3 y2,  e2 = x3 y1 - x1 y3,  e3 = x1 y2 - x2 y1,  det = (e1 + e2) + e3
            a  = (z1 (y2 - y3) + z2 (y3 - y1)) + z3 (y1 - y2)
            b  = (z1 (x3 - x2) + z2 (x1 - x3)) + z3 (x2 - x1)
            c  = (z1 e1 + z2 e2) + z3 e3
        and each of a, b, c is multiplied by the truncated reciprocal of det
      - with the vertices taken as (v1, v3, v2) of the order the rasterizer gets them in
      - within a tile the depth is block floating point, on the exponent of the depth at the tile's first
        pixel center, (A x0 + B y0) + C:
            that start value is truncated to 20 bits below the exponent,
            A and B are truncated to a 20 bit mantissa and to 26 bits below the same exponent,
            a pixel's depth is start + A' nx + B' ny for whole pixel steps nx, ny,
            and what is compared and stored is that, normalized again: truncated to a 19 bit mantissa on
            its own exponent, not the tile's (dreamcast-tests/pvr-tests/zblock: with the depth falling from 32 to 1
            across a tile, steps of 2^-18 are still resolved near 1)
    This is why a flat polygon does not get exactly its vertices' depth, why two triangles of the same flat
    quad differ, and why a slope that is small against the depth disappears.
    With the block exponent the fit is 59220 of 59220 over dreamcast-tests/pvr-tests/precision and
    dreamcast-tests/pvr-tests/zblock, gentle and steep slopes included (dreamcast-tests/fit/zslope.py); cutting each
    pixel's float depth to a 19 bit mantissa instead gets 43275 of the 43380 precision ones.
*/
// Where in the pixel the depth plane is evaluated, as an offset back from the pixel center. The sloped planes
// of dreamcast-tests/pvr-tests/precision page 3 fit slightly better at the pixel's corner (0.5) than at its center (0).
#ifndef DEPTH_SAMPLE_OFFSET
#define DEPTH_SAMPLE_OFFSET 0.0
#endif

struct DepthPlane
{
    double A, B, C;
    double left, top;
    bool valid;

    // the tile's block floating point form
    bool block;                 // false: the start value is 0 or not finite, use the float form
    int exponent;               // of the start value, as frexp gives it
    double start, stepX, stepY;

    // truncate v to `bits` bits below the exponent e
    static double Grid(double v, int e, int bits)
    {
        double unit = ldexp(1.0, e - bits);
        return trunc(v / unit) * unit;
    }

    // keep `bits` bits of mantissa, toward zero
    static double Trunc(double v, int bits) { return SetupTrunc(v, bits); }
    static double T(double v) { return Trunc(v, 24); }

    // Measured on a console with dreamcast-tests/pvr-tests/zmeas (28,686 stored depths, all reproduced):
    //  - 24 bit truncating arithmetic; the order of the adds is (v1 + v2) + v3 for the slopes and the
    //    determinant, (v1 + v3) + v2 for the constant
    //  - the depth at the tile's first pixel is the plane BEFORE its division by the determinant, evaluated
    //    there, times the reciprocal: ((a x0 + b y0) + c) * inv
    //  - the slopes are a * inv and b * inv as 20 bit floats; the pair shares the larger one's exponent, under
    //    which 21 bits are kept; then both are cut to the tile's grid, 26 bits under the start's exponent
    void Setup(taRECT *rect, const Vertex& v1, const Vertex& v2, const Vertex& v3)
    {
        const double x1 = v1.x, y1 = v1.y, z1 = v1.z;
        const double x2 = v2.x, y2 = v2.y, z2 = v2.z;
        const double x3 = v3.x, y3 = v3.y, z3 = v3.z;

        double e1 = T(T(x2 * y3) - T(x3 * y2));
        double e2 = T(T(x3 * y1) - T(x1 * y3));
        double e3 = T(T(x1 * y2) - T(x2 * y1));

        double a = T(T(T(z1 * T(y2 - y3)) + T(z2 * T(y3 - y1))) + T(z3 * T(y1 - y2)));
        double b = T(T(T(z1 * T(x3 - x2)) + T(z2 * T(x1 - x3))) + T(z3 * T(x2 - x1)));
        double c = T(T(T(z1 * e1) + T(z3 * e3)) + T(z2 * e2));
        double det = T(T(e1 + e2) + e3);

        left = rect->left;
        top = rect->top;
        valid = det != 0 && std::isfinite(det) && std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
        if (!valid) {
            // degenerate or out of range: fall back to the first vertex's depth
            A = B = 0;
            C = z1;
            return;
        }

        double inv = T(1.0 / det);
        A = T(a * inv);
        B = T(b * inv);
        C = T(c * inv);

        double first = T(T(T(T(a * (left + 0.5)) + T(b * (top + 0.5))) + c) * inv);
        block = first != 0 && std::isfinite(first) && std::isfinite(A) && std::isfinite(B);
        if (block) {
            int pairExponent;
            frexp(std::max(fabs(A), fabs(B)), &pairExponent);
            frexp(first, &exponent);
            start = Grid(first, exponent, 20);
            stepX = Grid(Grid(Trunc(A, 20), pairExponent, 21), exponent, 26);
            stepY = Grid(Grid(Trunc(B, 20), pairExponent, 21), exponent, 26);
        }
    }

    // x, y relative to the tile (including the half pixel)
    float Ip(float x, float y) const
    {
        if (!valid)
            return (float)C;
        if (block)
            return (float)Trunc(start + stepX * (x - 0.5) + stepY * (y - 0.5), 19);
        double X = left + x - DEPTH_SAMPLE_OFFSET, Y = top + y - DEPTH_SAMPLE_OFFSET;
        return (float)Trunc(T(T(T(A * X) + T(B * Y)) + C), 19);
    }

    // the float form without the cut to 19 bits
    float IpFull(float x, float y) const
    {
        if (!valid)
            return (float)C;
        double X = left + x - DEPTH_SAMPLE_OFFSET, Y = top + y - DEPTH_SAMPLE_OFFSET;
        return (float)T(T(T(A * X) + T(B * Y)) + C);
    }

    // Twice the signed area of the triangle as the setup computes it: the determinant the planes are divided
    // by. Its sign is the winding. (The exact area can have another sign or none: with one corner at 2^62 the
    // products of the other corners are below the last bit, and a console still draws the triangle right;
    // dreamcast-tests/pvr-tests/hugepos.)
    static double Det(double x1, double y1, double x2, double y2, double x3, double y3)
    {
        double e1 = T(T(x2 * y3) - T(x3 * y2));
        double e2 = T(T(x3 * y1) - T(x1 * y3));
        double e3 = T(T(x1 * y2) - T(x2 * y1));
        return T(T(e1 + e2) + e3);
    }

    // One edge of a polygon, from (xa, ya) to (xb, yb), evaluated at (X, Y) in absolute screen coordinates
    // with the same arithmetic: (xa - xb) Y - (ya - yb) X + (xb ya - xa yb).
    static double Edge(double xa, double ya, double xb, double yb, double X, double Y)
    {
        double c = T(T(xb * ya) - T(xa * yb));
        return T(T(T(T(xa - xb) * Y) - T(T(ya - yb) * X)) + c);
    }
};

// The ISP's inside test (edge functions per tile, rows and tie pixels) is in pvr_isp_edge.h, written on the
// soft floating point of pvr_softfp.h; the standalone probes of dreamcast-tests/model run the same code.

/*
    Interpolation helper
*/
struct IPs3
{
    PlaneStepper3 invW;
    DepthPlane invWhw;      // 1/w as the setup unit computes it (used for the mipmap level)
    PlaneStepper3 U[2];
    PlaneStepper3 V[2];
    PlaneStepper3 Col[2][4];
    PlaneStepper3 Ofs[2][4];
    float first[2][5];      // u, v, 1/w, x, y of the first vertex, per volume: the mipmap level with DCalcCtrl 1

    void Setup(taRECT *rect, DrawParameters* params, const Vertex& v1, const Vertex& v2, const Vertex& v3, bool TwoVolumes)
    {
        for (int vol = 0; vol < 2; vol++)
            for (int i = 0; i < 4; i++)
                Col[vol][i].scaleExp = Ofs[vol][i].scaleExp = PlaneStepper3::ColorExp;
        first[0][0] = v1.u; first[0][1] = v1.v; first[0][2] = v1.z; first[0][3] = v1.x; first[0][4] = v1.y;
        first[1][0] = v1.u1; first[1][1] = v1.v1; first[1][2] = v1.z; first[1][3] = v1.x; first[1][4] = v1.y;
        invW.Setup(rect, v1, v2, v3, false, 0, 0, 0);
        invWhw.Setup(rect, v1, v2, v3);
        U[0].Setup(rect, v1, v2, v3, true, v1.u, v2.u, v3.u);
        V[0].Setup(rect, v1, v2, v3, true, v1.v, v2.v, v3.v);
        if (params->isp.Gouraud) {
            for (int i = 0; i < 4; i++)
                Col[0][i].Setup(rect, v1, v2, v3, true, v1.col[i], v2.col[i], v3.col[i]);

            for (int i = 0; i < 4; i++)
                Ofs[0][i].Setup(rect, v1, v2, v3, true, v1.spc[i], v2.spc[i], v3.spc[i]);
        } else {
            for (int i = 0; i < 4; i++)
                Col[0][i].Setup(rect, v1, v2, v3, true, v3.col[i], v3.col[i], v3.col[i]);

            for (int i = 0; i < 4; i++)
                Ofs[0][i].Setup(rect, v1, v2, v3, true, v3.spc[i], v3.spc[i], v3.spc[i]);
        }

        for (int i = 0; i < 4; i++) {
            Col[0][i].fallback = v3.col[i];
            Ofs[0][i].fallback = v3.spc[i];
            Col[1][i].fallback = v3.col1[i];
            Ofs[1][i].fallback = v3.spc1[i];
        }

        if (TwoVolumes) {
            U[1].Setup(rect, v1, v2, v3, true, v1.u1, v2.u1, v3.u1);
            V[1].Setup(rect, v1, v2, v3, true, v1.v1, v2.v1, v3.v1);
            if (params->isp.Gouraud) {
                for (int i = 0; i < 4; i++)
                    Col[1][i].Setup(rect, v1, v2, v3, true, v1.col1[i], v2.col1[i], v3.col1[i]);

                for (int i = 0; i < 4; i++)
                    Ofs[1][i].Setup(rect, v1, v2, v3, true, v1.spc1[i], v2.spc1[i], v3.spc1[i]);
            } else {
                for (int i = 0; i < 4; i++)
                    Col[1][i].Setup(rect, v1, v2, v3, true, v3.col1[i], v3.col1[i], v3.col1[i]);

                for (int i = 0; i < 4; i++)
                    Ofs[1][i].Setup(rect, v1, v2, v3, true, v3.spc1[i], v3.spc1[i], v3.spc1[i]);
            }
        }
    }

    // Intensity shadows (FPU_SHAD_SCALE bit 8): inside a modifier volume a polygon with the shadow bit takes the
    // colours of its vertices scaled, (c * intensity) >> 8 each, red, green and blue, not alpha (a translucent
    // shadow polygon keeps its alpha: polly2-data jgr_tree_shadows2), interpolated like any others. Scaling the interpolated colour instead is one level off at a time where a
    // vertex colour is odd (polly2-data sc_ingame, sc_ingame2: Gouraud floors in shadow at 0x80).
    void SetupIntensity(taRECT *rect, DrawParameters* params, const Vertex& v1, const Vertex& v2, const Vertex& v3, u32 scale)
    {
        Vertex s[3] = { v1, v2, v3 };
        for (auto& v : s) {
            for (int i = 0; i < 3; i++) {
                v.col[i] = (u8)((v.col[i] * scale) >> 8);
                v.spc[i] = (u8)((v.spc[i] * scale) >> 8);
            }
        }
        const Vertex& f = s[2];
        for (int i = 0; i < 4; i++) {
            if (params->isp.Gouraud) {
                Col[1][i].Setup(rect, v1, v2, v3, true, s[0].col[i], s[1].col[i], s[2].col[i]);
                Ofs[1][i].Setup(rect, v1, v2, v3, true, s[0].spc[i], s[1].spc[i], s[2].spc[i]);
            } else {
                Col[1][i].Setup(rect, v1, v2, v3, true, f.col[i], f.col[i], f.col[i]);
                Ofs[1][i].Setup(rect, v1, v2, v3, true, f.spc[i], f.spc[i], f.spc[i]);
            }
            Col[1][i].fallback = f.col[i];
            Ofs[1][i].fallback = f.spc[i];
        }
    }
};

// Used for deferred TSP processing lookups
struct FpuEntry
{
    IPs3 ips;
    DrawParameters params;
};

union Color {
    u32 raw;
    u8 bgra[4];
    struct {
        u8 b;
        u8 g;
        u8 r;
        u8 a;
    };
};

extern u32* colorBuffer1;
extern u32* colorBuffer2;
extern u32  tilePixelsShaded;
extern const char* dump_textures;

void ClearBuffers(u32 paramValue, float depthValue, u32 stencilValue);
void ClearParamStatusBuffer();
void SetTagToMax();
void SaveDepth();
void SaveDepthAt(u32 index, float invW);
void RestoreDepth();
void PeelBuffers(float depthValue, u32 stencilValue);
void PeelBuffersPTInitial();
void PeelBuffersPTFinal();
void SummarizeStencilOr();
void SummarizeStencilAnd();
void ClearMoreToDraw();
bool GetMoreToDraw();

// Render to ACCUM from TAG buffer
// TAG holds references to triangles, ACCUM is the tile framebuffer
template<RenderMode rm>
void RenderParamTags(int tileX, int tileY);


//decode a vertex in the native pvr format
void decode_pvr_vertex(DrawParameters* params, pvr32addr_t ptr,Vertex* cv, u32 shadow);
// decode an object (params + vertexes)
u32 decode_pvr_vertices(DrawParameters* params, pvr32addr_t base, u32 skip, u32 two_volumes, Vertex* vtx, int count, int offset);

const FpuEntry& GetFpuEntry(taRECT *rect, RenderMode render_mode, ISP_BACKGND_T_type core_tag);
// Lookup/create cached TSP parameters, and call PixelFlush_tsp
bool PixelFlush_tsp(bool pp_AlphaTest, const FpuEntry* entry, float x, float y, u32 index, float invW, bool InVolume, ISP_BACKGND_T_type core_tag);
// Rasterize a single triangle to ISP (or ISP+TSP for PT)

extern void (*RasterizeTriangle_table[])(DrawParameters* params, parameter_tag_t tag, const Vertex& v1, const Vertex& v2, const Vertex& v3, const Vertex* v4, taRECT* area);

u8* GetColorOutputBuffer();
void NextTileAccumulationBuffers();


/*
    Main renderer class
*/

void RenderTriangle(RenderMode render_mode, DrawParameters* params, parameter_tag_t tag, const Vertex& v1, const Vertex& v2, const Vertex& v3, const Vertex* v4, taRECT* area);
// called on vblank
bool RenderFramebuffer();
u32 ReadRegionArrayEntry(u32 base, RegionArrayEntry* entry);
ISP_BACKGND_T_type CoreTagFromDesc(u32 cache_bypass, u32 shadow, u32 skip, u32 param_offs_in_words, u32 tag_offset);
void RenderTriangleStrip(RenderMode render_mode, ObjectListEntry obj, taRECT* rect);
void RenderTriangleArray(RenderMode render_mode, ObjectListEntry obj, taRECT* rect);
void RenderQuadArray(RenderMode render_mode, ObjectListEntry obj, taRECT* rect);
void RenderObjectList(RenderMode render_mode, pvr32addr_t base, taRECT* rect);
void RenderCORE();
void Hackpresent();
void ClearFpuCache();

}   // namespace refcore
