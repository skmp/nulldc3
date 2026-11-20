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


// refsw3's core (refcore): in a namespace of its own, beside rend/refsw2, which has the same names
namespace refcore {

// For texture cache

#define MAX_RENDER_WIDTH 32
#define MAX_RENDER_HEIGHT 32
#define MAX_RENDER_PIXELS (MAX_RENDER_WIDTH * MAX_RENDER_HEIGHT)

#define STRIDE_PIXEL_OFFSET MAX_RENDER_WIDTH


typedef pvrisp::StoredDepth ZType;   // the bits of an IEEE single, compared as floats (pvr_isp_depth.h)
typedef u8       StencilType;
typedef u32      ColorType;
/*
    Surface equation solver
*/
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
// A sample point of the tile: pixel (x, y) of the tile, and half a pixel further on both axes when the TSP's
// half pixel offset is on (HALF_OFFSET bit 2; dreamcast-tests/pvr-tests/halfofs2: all 8 values the console's)
struct PixPos { int x, y; bool half; };

struct PlaneStepper3
{
    int left = 0, top = 0;      // the tile's first pixel
    int fallback = 0;           // the attribute itself, for depths of 0, infinity or NaN (the last vertex's: a bump
                                // map's light is that offset colour as it is, PixelFlush_tsp)
    int scaleExp = 0;           // the attribute enters the plane times 2^scaleExp (colours: see IPs3::Setup)
    // The plane in the setup units' own arithmetic (pvr_tsp_plane.h), with the hardware's numbers baked in. Its
    // ranged arithmetic takes every input, infinities and NaNs included (uvmeas mixes wexp, whuge, wtiny, wspec).
    pvrtsp::PlaneT<pvrtsp::PlaneHW> soft;

    // attr: a plane of attribute / w, the attribute being a1, a2, a3 at the corners (false: the plane of 1 / w).
    // An attribute plane is made of the products of the 1/w plane, each multiplied by the corner's attribute and
    // cut again: a_i * (w_i * d), not (a_i * w_i) * d (dreamcast-tests/pvr-tests/uvmeas, mix wflat: 600 of 600 flat
    // cases).
    // attr: the bits of IEEE singles (u, v as the TA gives them), or null for the plane of 1/w
    void Setup(taRECT *rect, const Vertex& v1, const Vertex& v2, const Vertex& v3, const u32* attr)
    {
        static const pvrtsp::PlaneHW planeConfig;
        const u32 sx[3] = { v1.x, v2.x, v3.x }, sy[3] = { v1.y, v2.y, v3.y }, sz[3] = { v1.z, v2.z, v3.z };
        soft.Setup(sx, sy, sz, attr, planeConfig);
        left = rect->left;
        top = rect->top;
    }
    void Setup(taRECT *rect, const Vertex& v1, const Vertex& v2, const Vertex& v3, u32 a1, u32 a2, u32 a3)
    {
        const u32 a[3] = { a1, a2, a3 };
        Setup(rect, v1, v2, v3, a);
    }
    // a colour plane: the 8 bit values times 2^scaleExp (ColorExp)
    void SetupColor(taRECT *rect, const Vertex& v1, const Vertex& v2, const Vertex& v3, u8 c1, u8 c2, u8 c3)
    {
        Setup(rect, v1, v2, v3, pvrfp::SingleBits(c1, scaleExp), pvrfp::SingleBits(c2, scaleExp), pvrfp::SingleBits(c3, scaleExp));
    }

    // Column (in the tile) of the first pixel of the run of pixels being shaded: set by the loop over the tags.
    static int SpanStart;
    // SupSample: the evaluations are of one of the four points (pvr_tsp_plane.h, Value); hx, hy: half a pixel
    // further along x, y
    static inline bool superActive = false;
    static inline int superHx = 0, superHy = 0;

    // The plane's value at the sample point (pvr_tsp_plane.h, Value): the run's first pixel, then whole steps.
    pvrfp::Fp Val(PixPos p) const
    {
        const int n = p.x - SpanStart;                      // whole pixels from the first pixel of the run
        const int64_t x0h = 2 * (int64_t)(left + SpanStart) + (p.half ? 1 : 0);
        const int64_t yh = 2 * (int64_t)(top + p.y) + (p.half ? 1 : 0);
        if (superActive)
            return soft.Value(x0h, yh, n, left, top, true, superHx, superHy);
        return soft.Value(x0h, yh, n, left, top);
    }

    // The attribute at the sample point: the plane's value divided by the pixel's 1/w W as the TSP divides
    // (pvr_tsp_plane.h, Divide: the table reciprocal of W's 16 bit mantissa, the product cut at 17 fractional bits;
    // a quotient below zero stays below zero however small: dreamcast-tests/pvr-tests/uvmeas, mix whuge)
    pvrfp::Fp Quot(PixPos p, const pvrfp::Fp& W) const
    {
        return pvrtsp::Divide(Val(p), W);
    }

    // In the planes a colour is a fraction: the 8 bit value times 2^-8. With ordinary depths that changes
    // nothing (every product just has another exponent); it shows where the setup's numbers reach their limits
    // (dreamcast-tests/pvr-tests/cmeas, mix wext): at 1/w = 2^110 a colour of 0x86 is still 0x86 (as an integer its
    // products would be infinities), and where every product is an infinity or a zero the quotient is just under 1,
    // which is white. Measured: -8 gets 2,333,331 of 2,340,000 colour values, -7 1,688,587 and -9 2,124,860
    // (before the last rule of the sums went in).
    static constexpr int ColorExp = -8;

    // An interpolated colour as the TSP rounds it (pvr_tsp_plane.h, ColorRounded: the rule and its evidence)
    int Rounded(PixPos p, const pvrfp::Fp& W, int max) const
    {
        return pvrtsp::ColorRounded(Quot(p, W), scaleExp, max, fallback);
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
    PlaneStepper3 U[2];
    PlaneStepper3 V[2];
    PlaneStepper3 Col[2][4];
    PlaneStepper3 Ofs[2][4];
    u32 first[2][5];        // u, v, 1/w, x, y of the first vertex, per volume: the mipmap level with DCalcCtrl 1

    void Setup(taRECT *rect, DrawParameters* params, const Vertex& v1, const Vertex& v2, const Vertex& v3, bool TwoVolumes)
    {
        for (int vol = 0; vol < 2; vol++)
            for (int i = 0; i < 4; i++)
                Col[vol][i].scaleExp = Ofs[vol][i].scaleExp = PlaneStepper3::ColorExp;
        first[0][0] = v1.u; first[0][1] = v1.v; first[0][2] = v1.z; first[0][3] = v1.x; first[0][4] = v1.y;
        first[1][0] = v1.u1; first[1][1] = v1.v1; first[1][2] = v1.z; first[1][3] = v1.x; first[1][4] = v1.y;
        invW.Setup(rect, v1, v2, v3, nullptr);
        U[0].Setup(rect, v1, v2, v3, v1.u, v2.u, v3.u);
        V[0].Setup(rect, v1, v2, v3, v1.v, v2.v, v3.v);
        if (params->isp.Gouraud) {
            for (int i = 0; i < 4; i++)
                Col[0][i].SetupColor(rect, v1, v2, v3, v1.col[i], v2.col[i], v3.col[i]);

            for (int i = 0; i < 4; i++)
                Ofs[0][i].SetupColor(rect, v1, v2, v3, v1.spc[i], v2.spc[i], v3.spc[i]);
        } else {
            for (int i = 0; i < 4; i++)
                Col[0][i].SetupColor(rect, v1, v2, v3, v3.col[i], v3.col[i], v3.col[i]);

            for (int i = 0; i < 4; i++)
                Ofs[0][i].SetupColor(rect, v1, v2, v3, v3.spc[i], v3.spc[i], v3.spc[i]);
        }

        for (int i = 0; i < 4; i++) {
            Col[0][i].fallback = v3.col[i];
            Ofs[0][i].fallback = v3.spc[i];
            Col[1][i].fallback = v3.col1[i];
            Ofs[1][i].fallback = v3.spc1[i];
        }

        if (TwoVolumes) {
            U[1].Setup(rect, v1, v2, v3, v1.u1, v2.u1, v3.u1);
            V[1].Setup(rect, v1, v2, v3, v1.v1, v2.v1, v3.v1);
            if (params->isp.Gouraud) {
                for (int i = 0; i < 4; i++)
                    Col[1][i].SetupColor(rect, v1, v2, v3, v1.col1[i], v2.col1[i], v3.col1[i]);

                for (int i = 0; i < 4; i++)
                    Ofs[1][i].SetupColor(rect, v1, v2, v3, v1.spc1[i], v2.spc1[i], v3.spc1[i]);
            } else {
                for (int i = 0; i < 4; i++)
                    Col[1][i].SetupColor(rect, v1, v2, v3, v3.col1[i], v3.col1[i], v3.col1[i]);

                for (int i = 0; i < 4; i++)
                    Ofs[1][i].SetupColor(rect, v1, v2, v3, v3.spc1[i], v3.spc1[i], v3.spc1[i]);
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
                Col[1][i].SetupColor(rect, v1, v2, v3, s[0].col[i], s[1].col[i], s[2].col[i]);
                Ofs[1][i].SetupColor(rect, v1, v2, v3, s[0].spc[i], s[1].spc[i], s[2].spc[i]);
            } else {
                Col[1][i].SetupColor(rect, v1, v2, v3, f.col[i], f.col[i], f.col[i]);
                Ofs[1][i].SetupColor(rect, v1, v2, v3, f.spc[i], f.spc[i], f.spc[i]);
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

void ClearBuffers(u32 paramValue, ZType depthValue, u32 stencilValue);
void ClearParamStatusBuffer();
void SetTagToMax();
void SaveDepth();
void SaveDepthAt(u32 index, ZType invW);
void RestoreDepth();
void PeelBuffers(ZType depthValue, u32 stencilValue);
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
bool PixelFlush_tsp(bool pp_AlphaTest, const FpuEntry* entry, PixPos p, u32 index, const pvrfp::Fp& invW, bool InVolume, ISP_BACKGND_T_type core_tag);
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
