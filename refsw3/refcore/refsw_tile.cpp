/*
	This file is part of libswirl

   Implementes the Reference SoftWare renderer (RefRendInterface) backend for refrend_base.
   This includes buffer operations and rasterization

   Pixel operations are in refsw_pixel.cpp (PixelPipeline interface)
*/
#include "license/bsd"

// refsw3-offline checks that what a render reads was written: the registers it knows by name, and through
// this the fog table and palette entries.
#if defined(REFSW_OFFLINE)
void refsw3_offline_read(unsigned int reg_addr);       // refsw3-offline: a fog table or palette entry read
#define OFFLINE_READ(addr) refsw3_offline_read(addr)
#else
#define OFFLINE_READ(addr)
#endif

#include <cmath>
#include <memory>
#include <set>
#include <cstdio>
#include <cstdlib>

#include "refsw_tile.h"
#include "TexUtils.h"
#include <cassert>

#define STB_IMAGE_WRITE_STATIC         // rend/refsw2 has the writer too: this one stays in this file
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "deps/stb/stb_image_write.h"

// refsw3's core (refcore): in a namespace of its own, beside rend/refsw2, which has the same names
namespace refcore {

extern u8* emu_vram;

TagState       tagStatus[MAX_RENDER_PIXELS];
parameter_tag_t tagBuffer[2] [MAX_RENDER_PIXELS];
StencilType     stencilBuffer[MAX_RENDER_PIXELS];
// The TSP's two tile buffers (dreamcast-tests/pvr-tests/accbuf): a tile is shaded into one (colorBuffer1) while the
// other (colorBuffer2, the secondary accumulation buffer of the select bits) holds what was there before; they change
// places, rather than one being copied into the other.
static u32      colorBufferStore[2][MAX_RENDER_PIXELS];
u32*            colorBuffer1 = colorBufferStore[0];
u32*            colorBuffer2 = colorBufferStore[1];
u32             tilePixelsShaded;       // pixels the tile's entry shaded (RenderParamTags), for keep Z tiles
ZType           depthBuffer[3] [MAX_RENDER_PIXELS];

constexpr const u32 tagBufferA = 0;
constexpr const u32 tagBufferB = 1;
constexpr const u32 depthBufferA = 0;
constexpr const u32 depthBufferB = 1;
constexpr const u32 depthBufferC = 2;

void ClearBuffers(u32 paramValue, ZType depthValue, u32 stencilValue)
{
    auto zb = depthBuffer[depthBufferA];
    auto stencil = stencilBuffer;
    auto pb = tagBuffer[tagBufferA];;

    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        zb[i] = depthValue;
        stencil[i] = stencilValue;
        pb[i] = paramValue;
        tagStatus[i] = { true, false };
    }
}

void ClearParamStatusBuffer() {
    auto ts = tagStatus;

    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        ts[i] = { false, false };
    }
}

// a reference with no tag (see KeyAbove)
static const parameter_tag_t TAG_BELOW = 0xFFFFFFFF;

// In a tile whose punch-through list holds a primitive (drawn or not: dreamcast-tests/pvr-tests/optcull), the opaque
// polygons' runs do not end at volume edges, and the opaque pixels inside a volume are shaded again in the
// punch-through volume pass, in its runs with the punch-through pixels there (optrun kinds 9, 10; volrun; polly2-data
// jgr_car, jgr_tree_shadows*, ff2_fight). Punch-through pixels in front of an opaque polygon do not end its runs
// (optrun kinds 1-8), and a punch-through list with no primitive in it changes nothing (polly2-data sa2_ingame,
// 18wheel_select: an empty list in every tile).
bool OpInPt = false;                                    // set per tile by refsw_lists.cpp
static parameter_tag_t opTag[MAX_RENDER_PIXELS];        // the opaque tag of each pixel
static parameter_tag_t refTagC[MAX_RENDER_PIXELS];      // the tag of depthBufferC's layer: opaque (or background), or resolved

// A pass of punch-through takes in the pixels shaded in the pass before it: those that failed the alpha test go on
// to their next fragment, and so do those that passed, for that one pass only, as part of its runs (shaded, but
// they keep what they resolved to: colour, tag and depth). dreamcast-tests/pvr-tests/ptlayers (14 of 14 pages; kind
// 4: a pixel resolved in the first pass does not take part in the third), polly2-data shenmue_intro*.
int PtPass = 0;                                         // the pass being rendered (refsw_lists.cpp)
static int resPass[MAX_RENDER_PIXELS];                  // the pass a pixel was resolved in
static parameter_tag_t resTag[MAX_RENDER_PIXELS];
static ZType resDepth[MAX_RENDER_PIXELS];
static bool everRendered[MAX_RENDER_PIXELS];            // resolved in a chunk before this one

// The punch-through passes: depthBufferC keeps the opaque depth, depthBufferB / tagBufferB the boundary (none at
// first), depthBufferA / tagBufferA each pass's best and then what each pixel resolved to.
void PeelBuffersPTInitial() {
    memcpy(depthBuffer[depthBufferC], depthBuffer[depthBufferA], sizeof(ZType) * MAX_RENDER_PIXELS);
    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        tagStatus[i] = { false, false };
        stencilBuffer[i] = 0;
        tagBuffer[tagBufferB][i] = TAG_BELOW;
        opTag[i] = tagBuffer[tagBufferA][i];
        refTagC[i] = opTag[i];
        everRendered[i] = false;
        resPass[i] = -10;
    }
}


bool ptResolvedLastPass(u32 index) { return resPass[index] == PtPass - 1; }


// the next chunk of punch-through polygons (refsw_lists.cpp, PtChunkTake): it is peeled on its own, in front of or
// at what the chunks before it resolved (or the opaque depth)
void PeelBuffersPTChunk() {
    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        if (tagStatus[i].rendered) {
            depthBuffer[depthBufferC][i] = resDepth[i];
            refTagC[i] = resTag[i];
        }
        everRendered[i] = everRendered[i] || tagStatus[i].rendered;
        tagStatus[i] = { false, false };
        tagBuffer[tagBufferB][i] = TAG_BELOW;
        resPass[i] = -10;
    }
}

// after the last pass: a pixel no punch-through polygon resolved keeps the opaque depth
void PeelBuffersPTFinal() {
    for (int i = 0; i < MAX_RENDER_PIXELS; i++)
        tagStatus[i].rendered = tagStatus[i].rendered || everRendered[i];
    for (int i = 0; i < MAX_RENDER_PIXELS; i++)
        if (!tagStatus[i].rendered) {
            depthBuffer[depthBufferA][i] = depthBuffer[depthBufferC][i];
            tagBuffer[tagBufferA][i] = opTag[i];        // the sort's first reference is this layer's key (KeyAbove)
        } else {
            depthBuffer[depthBufferA][i] = resDepth[i];
            tagBuffer[tagBufferA][i] = resTag[i];
        }
}

// The translucent sort peels layers in the depth buffers; the depth the tile's next region array entry keeps (keep Z)
// is the one from before it (polly2-data thps2_ingame*: three entries a tile, the second and third keep Z), where a
// layer was drawn the nearest layer's, whether it writes depth or not (dreamcast-tests/pvr-tests/keepz page 3).
static ZType savedDepth[MAX_RENDER_PIXELS];
void SaveDepth() { memcpy(savedDepth, depthBuffer[depthBufferA], sizeof(savedDepth)); }
void SaveDepthAt(u32 index, ZType invW) { savedDepth[index] = invW; }
void RestoreDepth() { memcpy(depthBuffer[depthBufferA], savedDepth, sizeof(savedDepth)); }

void SetTagToMax()
{
    memset(tagBuffer[tagBufferA], 0xFF, sizeof(tagBuffer[tagBufferA]));
}
void PeelBuffers(ZType depthValue, u32 stencilValue)
{
    memcpy(depthBuffer[depthBufferB], depthBuffer[depthBufferA], sizeof(ZType) * MAX_RENDER_PIXELS);
    memcpy(tagBuffer[tagBufferB], tagBuffer[tagBufferA], sizeof(parameter_tag_t) * MAX_RENDER_PIXELS);


    auto zb = depthBuffer[depthBufferA];
    auto stencil = stencilBuffer;

    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        zb[i] = depthValue;            // set the "closest" test to furthest value possible
        tagStatus[i] = { false, false };
        stencil[i] = stencilValue;
    }
}


void SummarizeStencilOr() {
    auto stencil = stencilBuffer;

    // post movdol merge INSIDE
    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        if (stencil[i] & 0b100) {
            stencil[i] |= (stencil[i] >>1);
            stencil[i] &=0b001; // keep only status bit
        }
    }
}

void SummarizeStencilAnd() {
    auto stencil = stencilBuffer;

    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        // post movdol merge OUTSIDE
        if (stencil[i] & 0b100) {
            stencil[i] &= (stencil[i] >>1);
            stencil[i] &=0b001; // keep only status bit
        }
    }
}

bool MoreToDraw;
void ClearMoreToDraw()
{
    MoreToDraw = 0;
}

int PlaneStepper3::SpanStart = 0;

bool GetMoreToDraw()
{
    return MoreToDraw;
}

// From the ISP to the TSP, as the hardware hands a tile over (dreamcast-tests/pvr-tests/PIPELINE_SHAPE.md [2], [2b];
// timemeas/REPORT.md "In what order the TSP shades a tile"):
//   SpanGen    the ISP's side cuts the tag buffer into spans (runs): pixels of one primitive next to each other
//              on a line, in raster order, one span a clock whatever its length
//   SpanSort   the span sorter puts them in the order the TSP shades them: a window of 96 spans filled in raster
//              order; the oldest span's primitive is drained, all its spans in the window oldest first, also those
//              that come in while it lasts; then the next oldest span's primitive. Each tile (and here each pass)
//              on its own. Not modelled: the "early drains" the measurements show but do not explain.
//              SPAN_SORT_CFG (dreamcast-tests README "spansortcfg"): bit 8, offset sort, compares the primitives'
//              parameter offsets only, so a strip's triangles are one primitive to the sorter (timemeas order
//              scenes 39, 40: with it 1 run of 1024 out of order that way, 82 taken triangle by triangle; without
//              it 0 and 90). Bit 0 off: no sorter, the spans in raster order; a console then shades a few runs
//              early (a line's last run, a run of the next line) by the pipeline's timing, which is not modelled.
//              Neither changes a picture (only what the order carries: tspAlphaStuck, offs).
//   the TSP    shades each span from its first pixel (PlaneStepper3::SpanStart: the planes are evaluated at the
//              span's start and stepped from there, dreamcast-tests/pvr-tests/uvspan)
// The order matters to the picture only through what the TSP keeps from one pixel to the next: tspAlphaStuck and
// the offset colour a polygon without one reads (offs).
struct Span {
    parameter_tag_t tag;    // the primitive (ISP_BACKGND_T)
    u8 y, x, len;
};

// The tile's spans of this pass, in raster order. Which pixels are shaded and where a span ends depend on the pass.
template<RenderMode rm>
static int SpanGen(Span* spans)
{
    int n = 0;
    for (int y = 0; y < 32; y++) {
        bool open = false;
        bool openVol = false;
        for (int x = 0; x < 32; x++) {
            const auto index = y * 32 + x;
            const auto tag = tagBuffer[tagBufferA][index];
            bool valid = tagStatus[index].valid;
            const bool volHit = (stencilBuffer[index] & 0b001) == 0b001;
            // The punch-through pixels inside a volume are shaded again after the passes, all of them: a polygon
            // without the shadow bit with its own parameters, in runs over the pixels inside (dreamcast-tests/pvr-tests/
            // uvrun kind 7; polly2-data jgr_tree_shadows*); the opaque ones there too (OpInPt), not alpha tested
            if (rm == RM_PUNCHTHROUGH_MV)
                valid = volHit && (tagStatus[index].rendered || OpInPt);
            // A span ends where the volume state changes, shadow bit or not: opaque (not in a tile with punch-through
            // drawn: see OpInPt), sorted translucent and the punch-through volume pass (uvrun kinds 6, 9, 7; volrun:
            // edges made by the depth test too). Not translucent polygons in the order sent, which an
            // opaque volume's shadow reaches but whose spans it does not cut (uvrun kinds 10, 11; polly2-data
            // sa2_ingame2, taxi3, xtreme_intro, jgr_tree_shadows*).
            const bool vol = rm == RM_TRANSLUCENT_PRESORT || rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN || (rm == RM_OPAQUE && OpInPt) ? false : volHit;
            if (!valid) {
                open = false;
            } else if (!open || tag != spans[n - 1].tag || vol != openVol) {
                open = true;
                openVol = vol;
                spans[n++] = { tag, (u8)y, (u8)x, 1 };
            } else {
                spans[n - 1].len++;
            }
        }
    }
    return n;
}

// The order the TSP shades the spans in: indices into spans
static int SpanSort(const Span* spans, int n, u16* order)
{
    const u32 cfg = SPAN_SORT_CFG;
    if (!(cfg & 1)) {
        for (int i = 0; i < n; i++)
            order[i] = (u16)i;
        return n;
    }
    // the primitive the sorter sees: with offset sort the strip's, without the triangle's (tag_offset: its index)
    const parameter_tag_t key = (cfg & 0x100) ? ~(parameter_tag_t)7 : ~(parameter_tag_t)0;
    constexpr int Window = 96;
    u16 window[Window];         // the spans in the window, oldest first
    int inWindow = 0, next = 0, out = 0;
    auto fill = [&] {
        while (inWindow < Window && next < n)
            window[inWindow++] = (u16)next++;
    };
    fill();
    while (inWindow) {
        const parameter_tag_t prim = spans[window[0]].tag & key;
        // drain the primitive: its oldest span in the window, until it has none there; each span shaded makes room
        // for the next span of the tile, which may be the primitive's too
        for (;;) {
            int i = 0;
            while (i < inWindow && (spans[window[i]].tag & key) != prim)
                i++;
            if (i == inWindow)
                break;
            order[out++] = window[i];
            for (int j = i + 1; j < inWindow; j++)
                window[j - 1] = window[j];
            inWindow--;
            fill();
        }
    }
    return out;
}

// Render to ACCUM from TAG buffer
// TAG holds references to trianes, ACCUM is the tile framebuffer
template<RenderMode rm>
void RenderParamTags(int tileX, int tileY) {
    const bool halfpixel = HALF_OFFSET.tsp_pixel_half_offset;
    taRECT rect;
    rect.left = tileX;
    rect.top = tileY;
    rect.bottom = rect.top + 32;
    rect.right = rect.left + 32;

    static Span spans[32 * 32];
    static u16 order[32 * 32];
    const int n = SpanSort(spans, SpanGen<rm>(spans), order);
    RENDLOG("PASS: %d %d %d", (int)rm, tileX, tileY);

    for (int s = 0; s < n; s++) {
        const Span& span = spans[order[s]];
        const int y = span.y;
        PlaneStepper3::SpanStart = span.x;
        RENDLOG("SPAN: %d %d %d %08X %d", y, span.x, span.len, span.tag, (int)rm);
        for (int x = span.x; x < span.x + span.len; x++) {
            auto index = y * 32 + x;
            ISP_BACKGND_T_type t { .full = span.tag };
            // inside a modifier volume, whatever the polygon's shadow bit; the volume's parameters (or its intensity)
            // only for a polygon with the bit
            const bool volHit = (stencilBuffer[index] & 0b001) == 0b001;
            bool InVolume = volHit && t.shadow;
            if (rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN) {
                InVolume = false;
            }

            tilePixelsShaded++;
            const auto& Entry = GetFpuEntry(&rect, rm, t);
            const PixPos pp = { x, y, halfpixel };
            const pvrfp::Fp invW = Entry.ips.invW.Val(pp);
            // Punch through shaded again inside a modifier volume is alpha tested too: its colour replaces the
            // pixel's, not blended with its alpha (dreamcast-tests/pvr-tests/dumprules, cell 22)
            // a pixel resolved in the pass before: shaded for the run, keeps its colour, goes on below
            const bool keepRes = (rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN) && tagStatus[index].rendered;
            const u32 keep1 = colorBuffer1[index], keep2 = colorBuffer2[index];
            bool AlphaTestPassed = PixelFlush_tsp(rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN || (rm == RM_PUNCHTHROUGH_MV && tagStatus[index].rendered), &Entry, pp, index, invW, InVolume, t);
            if (keepRes) {
                colorBuffer1[index] = keep1; colorBuffer2[index] = keep2;
                depthBuffer[depthBufferB][index] = depthBuffer[depthBufferA][index];
                tagBuffer[tagBufferB][index] = tagBuffer[tagBufferA][index];
                tagStatus[index].valid = false;
                continue;
            }

            if (rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN) {
                // can only happen when rm == RM_PUNCHTHROUGH
                if (!AlphaTestPassed) {
                    // the next pass looks below this fragment's key; the pixel is shaded again only if that pass
                    // draws a polygon there (shading the same one again with another run start could pass:
                    // dreamcast-tests/pvr-tests/ptruns; polly2-data jgr_tree_shadows2)
                    MoreToDraw = true;
                    depthBuffer[depthBufferB][index] = depthBuffer[depthBufferA][index];
                    tagBuffer[tagBufferB][index] = tagBuffer[tagBufferA][index];
                    tagStatus[index].valid = false;
                } else {
                    tagStatus[index].rendered = true;
                    tagStatus[index].valid = false;
                    resPass[index] = PtPass;
                    resTag[index] = tagBuffer[tagBufferA][index];
                    resDepth[index] = depthBuffer[depthBufferA][index];
                    depthBuffer[depthBufferB][index] = depthBuffer[depthBufferA][index];      // the next pass looks below it
                    tagBuffer[tagBufferB][index] = tagBuffer[tagBufferA][index];
                }
            }

            if (rm == RM_TRANSLUCENT_PRESORT) {
                tagStatus[index].valid = false;
            }
            // a sorted translucent layer leaves its depth for a keep Z entry after this one, whether it writes depth
            // or not; drawn back to front, so the nearest stays (dreamcast-tests/pvr-tests/keepz page 3; as
            // polly2-rtl's z_keep restore)
            if (rm == RM_TRANSLUCENT_AUTOSORT)
                SaveDepthAt(index, depthBuffer[depthBufferA][index]);     // the ISP's depth of the layer drawn
        }
    }
}

template void RenderParamTags<RM_OPAQUE>(int tileX, int tileY);
template void RenderParamTags<RM_PUNCHTHROUGH_PASS0>(int tileX, int tileY);
template void RenderParamTags<RM_PUNCHTHROUGH_PASSN>(int tileX, int tileY);
template void RenderParamTags<RM_PUNCHTHROUGH_MV>(int tileX, int tileY);
template void RenderParamTags<RM_TRANSLUCENT_AUTOSORT>(int tileX, int tileY);
template void RenderParamTags<RM_TRANSLUCENT_PRESORT>(int tileX, int tileY);
template void RenderParamTags<RM_MODIFIER>(int tileX, int tileY);

#define vert_packed_color_(to,src) \
	{ \
	u32 t=src; \
	to[0] = (u8)(t);t>>=8;\
	to[1] = (u8)(t);t>>=8;\
	to[2] = (u8)(t);t>>=8;\
	to[3] = (u8)(t);      \
	}

//decode a vertex in the native pvr format
void decode_pvr_vertex(DrawParameters* params, pvr32addr_t ptr, Vertex* cv, u32 two_volumes)
{
    //XYZ
    //UV
    //Base Col
    //Offset Col

    //XYZ are _allways_ there :)
    cv->x=vri(emu_vram, ptr);ptr+=4;
    cv->y=vri(emu_vram, ptr);ptr+=4;
    cv->z=vri(emu_vram, ptr);ptr+=4;

    if (params->isp.Texture)
    {	//Do texture , if any
        if (params->isp.UV_16b)
        {
            u32 uv=vri(emu_vram, ptr);
            cv->u = uv & 0xffff0000;            // the upper halves of IEEE singles
            cv->v = uv << 16;
            ptr+=4;
        }
        else
        {
            cv->u=vri(emu_vram, ptr);ptr+=4;
            cv->v=vri(emu_vram, ptr);ptr+=4;
        }
    }

    //Color
    u32 col=vri(emu_vram, ptr);ptr+=4;
    vert_packed_color_(cv->col,col);
    if (params->isp.Offset)
    {
        //Intensity color
        u32 col=vri(emu_vram, ptr);ptr+=4;
        vert_packed_color_(cv->spc,col);
    }

    if (two_volumes) {
        if (params->isp.Texture)
        {	//Do texture , if any
            if (params->isp.UV_16b)
            {
                u32 uv=vri(emu_vram, ptr);
                cv->u1 = uv & 0xffff0000;
                cv->v1 = uv << 16;
                ptr+=4;
            }
            else
            {
                cv->u1=vri(emu_vram, ptr);ptr+=4;
                cv->v1=vri(emu_vram, ptr);ptr+=4;
            }
        }

        //Color
        u32 col1=vri(emu_vram, ptr);ptr+=4;
        vert_packed_color_(cv->col1,col1);
        if (params->isp.Offset)
        {
            //Intensity color
            u32 col1=vri(emu_vram, ptr);ptr+=4;
            vert_packed_color_(cv->spc1,col1);
        }
    }
}

// With the scan order bit set (the texture is not twiddled) the texture is read row by row, one level,
// uncompressed: the mipmap and VQ bits do nothing to where a texel is fetched. (The mipmap bit still makes
// the trilinear passes fade by the level's fraction.) Palette textures have no such bit: those bits are
// their palette select. dreamcast-tests/pvr-tests/mip: page 17, and the "scan order bit set" variants.
static inline bool TcwRaster(TCW tcw) {
    return tcw.ScanOrder && tcw.PixelFmt != PixelPal4 && tcw.PixelFmt != PixelPal8;
}

// decode an object (params + vertexes)
u32 decode_pvr_vertices(DrawParameters* params, pvr32addr_t base, u32 skip, u32 two_volumes, Vertex* vtx, int count, int offset)
{
    params->isp.full=vri(emu_vram, base);
    params->tsp[0].full=vri(emu_vram, base+4);
    params->tcw[0].full=vri(emu_vram, base+8);

    base += 12;
    if (two_volumes) {
        params->tsp[1].full=vri(emu_vram, base+0);
        params->tcw[1].full=vri(emu_vram, base+4);
        base += 8;
    }

    for (int i = 0; i < offset; i++) {
        base += (3 + skip * (two_volumes+1)) * 4;
    }

    for (int i = 0; i < count; i++) {
        decode_pvr_vertex(params,base, &vtx[i], two_volumes);
        base += (3 + skip * (two_volumes+1)) * 4;
    }

    return base;
}

struct {
    FpuEntry entry;
    u32      tag;
} fpuCache[32];

const FpuEntry& GetFpuEntry(taRECT *rect, RenderMode render_mode, ISP_BACKGND_T_type core_tag)
{
    if (fpuCache[core_tag.param_offs_in_words & 31].tag == core_tag.full) {
        return fpuCache[core_tag.param_offs_in_words & 31].entry;
    }
    FpuEntry &entry = fpuCache[core_tag.param_offs_in_words & 31].entry;
    Vertex vtx[3];
    decode_pvr_vertices(&entry.params, PARAM_BASE + core_tag.param_offs_in_words * 4, core_tag.skip, core_tag.shadow & ~FPU_SHAD_SCALE.intensity_shadow, vtx, 3, core_tag.tag_offset);

    // the rasterizer gets the odd triangles of a strip with their first two vertices swapped; use the same
    // order here, since the setup arithmetic depends on it
    if (core_tag.tag_offset & 1)
        std::swap(vtx[0], vtx[1]);

    entry.ips.Setup(rect, &entry.params, vtx[0], vtx[1], vtx[2], core_tag.shadow & ~FPU_SHAD_SCALE.intensity_shadow);
    if (core_tag.shadow && FPU_SHAD_SCALE.intensity_shadow)
        entry.ips.SetupIntensity(rect, &entry.params, vtx[0], vtx[1], vtx[2], FPU_SHAD_SCALE.scale_factor);

    fpuCache[core_tag.param_offs_in_words & 31].tag = core_tag.full;

    return fpuCache[core_tag.param_offs_in_words & 31].entry ;
}

// A hardware bug of the blend unit, measured on a console (dreamcast-tests/pvr-tests/blendsel, blendsel2; blend
// settings 193 and 196): once a polygon with source and destination both buffer 2 (SrcSelect and DstSelect), source
// instruction ONE or SRC_A and destination instruction ZERO has been drawn in a tile -- one pixel of it is
// enough -- every polygon after it in that tile has its own alpha replaced by 0, unless it is 255, before it is
// blended: (SRC_A, 1-SRC_A) leaves the destination as it was, (ONE, ZERO) stores alpha 0. Alpha that comes
// from buffer 2 (SrcSelect) is not touched, other polygons in between change nothing, and the next tile
// starts clean. Not measured: textured polygons (the alpha after the texture is replaced here).
static bool tspAlphaStuck;

void ClearFpuCache() {
    memset(fpuCache, 0, sizeof(fpuCache));
    tspAlphaStuck = false;          // a new tile
}

bool ptResolvedLastPass(u32 index);

// The order of the fragments in the sorted translucent passes and the punch-through passes: a key of the depth and
// the tag's sort bits (where the polygon's parameters are, which grows with the list order). It is unique per
// fragment, so no two are ever coplanar, and both walks are the same compare in opposite directions (as
// polly2-rtl's isp_depth_cmp_lp): the translucent sort takes the smallest key above the layer drawn last (far to
// near; in one plane in list order, dreamcast-tests/pvr-tests/sort, dumprules page 1), the punch-through passes
// the largest key below the last one tried (near to far; in one plane the last in the list first,
// dumprules page 0). A larger key is nearer, or as near and later in the list. The first reference of either is
// the key of what is there, the opaque polygon's, a punch-through layer's or the background's, tag and all: a
// fragment at exactly its depth is drawn only with a tag above it (dreamcast-tests/pvr-tests/trtie). TAG_BELOW, a
// reference with no tag, is the punch-through boundary before any.
static ALWAYS_INLINE bool KeyAbove(ZType aW, parameter_tag_t aTag, ZType bW, parameter_tag_t bTag)
{
    if (aW != bW)
        return aW > bW;
    if (bTag == TAG_BELOW)
        return aTag != TAG_BELOW;
    return aTag != TAG_BELOW && (aTag & PARAMETER_TAG_SORT_MASK) > (bTag & PARAMETER_TAG_SORT_MASK);
}

// Depth processing for a pixel -- render_mode 0: OPAQ, 1: PT, 2: TRANS
template<RenderMode render_mode>
ALWAYS_INLINE void PixelFlush_isp(u32 depth_mode, u32 ZWriteDis, ZType invW, u32 index, parameter_tag_t tag)
{
    auto pb = tagBuffer[tagBufferA] + index;
    auto ts = tagStatus + index;
    auto pb2 = tagBuffer[tagBufferB] + index;
    auto zb = depthBuffer[depthBufferA] + index;
    auto zb2 = depthBuffer[depthBufferB] + index;
    auto stencil = stencilBuffer + index;

    auto mode = depth_mode;
        
    if (render_mode == RM_PUNCHTHROUGH_PASS0 || render_mode == RM_PUNCHTHROUGH_PASSN || render_mode == RM_TRANSLUCENT_AUTOSORT)
        mode = 7;                                   // the ordered walks below compare for themselves
    else if (render_mode == RM_MODIFIER)
        mode = 4;

    RENDLOG("ISP: %d %d %d %f %08X", index, render_mode, mode, invW.toDouble(), tag);
    switch(mode) {
        // never
        case 0: RENDLOG("ZFAIL"); return; break;
        // less
        case 1: if (invW >= *zb) { RENDLOG("ZFAIL"); return; } break;
        // equal
        case 2: if (invW != *zb) { RENDLOG("ZFAIL"); return; } break;
        // less or equal
        case 3: if (invW > *zb) { RENDLOG("ZFAIL"); return; } break;
        // greater
        case 4: if (invW <= *zb) { RENDLOG("ZFAIL"); return; } break;
        // not equal
        case 5: if (invW == *zb) { RENDLOG("ZFAIL"); return; } break;
        // greater or equal
        case 6: if (invW < *zb) { RENDLOG("ZFAIL"); return; } break;
        // always
        case 7: break;
    }

    switch (render_mode)
    {
        // OPAQ
        case RM_OPAQUE:
        {
            // Z pre-pass only
            if (!ZWriteDis) {
                *zb = invW;
            }
            *pb = tag;
            ts->valid = 1;
            RENDLOG("RENDERED: %f", zb->toDouble());
        }
        break;

        case RM_MODIFIER:
        {
            // Flip on Z pass

            RENDLOG("STENCIL: %08X", *stencil);

            *stencil ^= 0b0010;

            // This pixel has valid stencil for summary
            *stencil |= 0b100;
        }
        break;

        // Punch through, every pass: in front of or at the opaque depth (depthBufferC; compare greater or equal,
        // whatever the polygon's own mode), not resolved (or resolved in the pass before: see resPass), below the
        // boundary (zb2, pb2: the key shaded there last; none in the first pass), the nearest such (zb, pb: this
        // pass's best).
        case RM_PUNCHTHROUGH_PASS0:
        case RM_PUNCHTHROUGH_PASSN:
        {
            if (ts->rendered && !ptResolvedLastPass(index)) {                // resolved before the pass before
                RENDLOG("ALREADY_DRAWN");
                return;
            }
            // above the opaque layer's key (or what an earlier chunk resolved): at exactly its depth only with a tag
            // above its own (dreamcast-tests/pvr-tests/trtie pages 9-11: the background's too)
            if (!KeyAbove(invW, tag, depthBuffer[depthBufferC][index], refTagC[index])) {
                RENDLOG("ZFAIL");
                return;
            }
            if (*pb2 != TAG_BELOW && !KeyAbove(*zb2, *pb2, invW, tag)) {
                RENDLOG("ZFAIL2");
                return;
            }
            if (ts->valid && !KeyAbove(invW, tag, *zb, *pb)) {
                RENDLOG("ZFAIL3");
                return;
            }
            MoreToDraw = true;                      // something to shade
            *zb = invW;
            *pb = tag;
            ts->valid = 1;
            RENDLOG("RENDERED");
        }
        break;

        // Layer Peeling. zb2 holds the reference depth, zb is used to find closest to reference
        case RM_TRANSLUCENT_PRESORT:
        {
            if (!ZWriteDis) {
                *zb = invW;
            }
            *pb = tag;
            ts->valid = true;
            RENDLOG("RENDERED: %f", zb->toDouble());
        }
        break;
        // The translucent sort: above the layer drawn last (zb2, pb2; the opaque depth in the first pass), the
        // farthest such (zb, pb: this pass's best). A nearer one waits for a later pass, as does a best displaced.
        case RM_TRANSLUCENT_AUTOSORT:
        {
            if (ts->valid && KeyAbove(invW, tag, *zb, *pb)) {
                MoreToDraw = true;
                RENDLOG("ZFAIL6");
                return;
            }
            if (!KeyAbove(invW, tag, *zb2, *pb2)) {
                RENDLOG("ZFAIL4");
                return;
            }
            if (ts->valid)
                MoreToDraw = true;
            *zb = invW;
            *pb = tag;
            ts->valid = true;
            RENDLOG("RENDERED");
        }
        break;

        case RM_PUNCHTHROUGH_MV: die("this is invalid here"); break;
    }
}

// The ISP's cull by size: |det| <= |FPU_CULL_VAL|, the determinant as the setup has it (pvrisp::Winding). Equal is
// culled (dreamcast-tests/pvr-tests/raster page 4: legs of 1 pixel with the threshold at 1.0, cullval: every
// determinant of 1/4 .. 192 at 0.5, 1 and 2). The register's sign does not count, and an infinity or any NaN culls
// every triangle (dreamcast-tests/pvr-tests/cullval: -1, -2, -0.5, -0 render as 1, 2, 0.5, +0; -inf, NaN, -NaN and a
// signalling NaN as +inf; 2^-149 culls nothing, 16 of 16 pages). An infinite determinant draws nothing anyway.
static bool CulledBySize(const pvrfp::Fp& det)
{
    using pvrfp::Fp;
    if (det.bad)
        return false;
    const u32 size = PvrReg(FPU_CULL_VAL_addr, u32) & 0x7fffffff;
    if (size >= 0x7f800000)
        return true;
    return pvrfp::AbsAtMost(det, Fp::FromBits(size));
}

// The ISP's cull by facing (CullMode 2, 3): a determinant below zero (-0 is not) or above zero
static bool CulledByFacing(u32 cullMode, const pvrfp::Fp& det)
{
    if (cullMode < 2 || det.bad || det.zero())
        return false;
    return (cullMode & 1) ? !det.neg : det.neg;
}

// Does the ISP's setup cull this triangle (CullMode: too small, or facing away)? The same arithmetic as below.
bool TriangleCulled(const DrawParameters* params, const Vertex& v1, const Vertex& v2, const Vertex& v3, const Vertex* v4)
{
    if (params->isp.CullMode == 0)
        return false;
    static const pvrisp::EdgeHW edgeConfig;          // the hardware's numbers, baked in (pvr_isp_edge.h)
    using pvrfp::Fp;
    const Fp FX[4] = { Fp::FromBits(v1.x), Fp::FromBits(v2.x), Fp::FromBits(v3.x), v4 ? Fp::FromBits(v4->x) : Fp() };
    const Fp FY[4] = { Fp::FromBits(v1.y), Fp::FromBits(v2.y), Fp::FromBits(v3.y), v4 ? Fp::FromBits(v4->y) : Fp() };
    bool badCorner = false;
    Fp det;
    pvrisp::Winding(FX, FY, edgeConfig, &badCorner);
    if (badCorner || FX[3].bad || FY[3].bad)
        return false;
    pvrisp::EdgeWinding(FX, FY, edgeConfig, &det);      // the determinant with the swamp (dreamcast-tests/pvr-tests/faredge_cull)
    return CulledBySize(det) || CulledByFacing(params->isp.CullMode, det);
}

// Rasterize a single triangle to ISP (or ISP+TSP for PT)
template<RenderMode render_mode>
void RasterizeTriangle(DrawParameters* params, parameter_tag_t tag, const Vertex& v1, const Vertex& v2, const Vertex& v3, const Vertex* v4, taRECT* area)
{
    const int stride_bytes = STRIDE_PIXEL_OFFSET * 4;
    //Plane equation

    // The winding, the area for culling and the inside test are computed in the setup units' own arithmetic
    // (pvr_isp_edge.h on pvr_softfp.h; every rule measured on a console with dreamcast-tests/pvr-tests/covmeas and
    // dreamcast-tests/pvr-tests/hugepos). The same code is the model the probes of dreamcast-tests/model run.
    static const pvrisp::EdgeHW edgeConfig;          // the hardware's numbers, baked in (pvr_isp_edge.h)
    using pvrfp::Fp;
    const Fp FX[4] = { Fp::FromBits(v1.x), Fp::FromBits(v2.x), Fp::FromBits(v3.x), v4 ? Fp::FromBits(v4->x) : Fp() };
    const Fp FY[4] = { Fp::FromBits(v1.y), Fp::FromBits(v2.y), Fp::FromBits(v3.y), v4 ? Fp::FromBits(v4->y) : Fp() };
    bool badCorner = false;
    Fp det;
    // -1 when the determinant is positive or zero (a zero counts as positive: covmeas, mix sliver); the determinant the
    // edges are turned by and the culling tests is the one with the setup's overflowing products swamping the rest
    // (pvrisp::EdgeWinding; dreamcast-tests/pvr-tests/faredge_m131, faredge_cull)
    pvrisp::Winding(FX, FY, edgeConfig, &badCorner);
    const int sgn = pvrisp::EdgeWinding(FX, FY, edgeConfig, &det);
    // an infinity or a NaN among the corners: a console draws nothing (dreamcast-tests/pvr-tests/huge)
    if (badCorner || FX[3].bad || FY[3].bad)
        return;
    // cull: det is twice the area as the setup has it; corners at 2^100 are fine here (dreamcast-tests/pvr-tests/huge,
    // dreamcast-tests/pvr-tests/hugepos)
    if (params->isp.CullMode != 0 && (CulledBySize(det) || CulledByFacing(params->isp.CullMode, det))) {
        RENDLOG("CULLED");
        return;
    }

    // The ISP evaluates an edge per tile: a start value at the first sample point and whole steps, and it
    // decides rows and tie pixels in its own way (pvrisp::EdgeTile). The edges are turned to one winding
    // (sgn): hardware draws clockwise and counter-clockwise triangles alike.
    const int64_t halfBit = HALF_OFFSET.fpu_pixel_half_offset ? 1 : 0;
    const int64_t x0h = 2 * (int64_t)area->left + halfBit, y0h = 2 * (int64_t)area->top + halfBit;
    const int last = v4 ? 3 : 0;                                // where the third edge ends
    pvrisp::EdgeTile E12, E23, E31, E41;
    E12.Setup(FX[0], FY[0], FX[1], FY[1], x0h, y0h, sgn, edgeConfig);
    E23.Setup(FX[1], FY[1], FX[2], FY[2], x0h, y0h, sgn, edgeConfig);
    E31.Setup(FX[2], FY[2], FX[last], FY[last], x0h, y0h, sgn, edgeConfig);
    if (v4)
        E41.Setup(FX[3], FY[3], FX[0], FY[0], x0h, y0h, sgn, edgeConfig);
    // The ISP's depth per tile, in the setup units' arithmetic (pvr_isp_depth.h; dreamcast-tests/pvr-tests/zmeas)
    static const pvrisp::DepthHW depthConfig;
    pvrisp::DepthTileT<pvrisp::DepthHW> Z;
    const bool sampleCenters = HALF_OFFSET.fpu_pixel_half_offset;
    {
        // the start at the tile's first sample point, centre or corner (pvr_isp_depth.h, dreamcast-tests/pvr-tests/depth2_halfc)
        const u32 zx[3] = { v1.x, v2.x, v3.x }, zy[3] = { v1.y, v2.y, v3.y }, zz[3] = { v1.z, v2.z, v3.z };
        Z.Setup(zx, zy, zz, area->left, area->top, depthConfig, sampleCenters);
    }

    // The rows a polygon is rasterized on: from the row its top corner is in to the row of its bottom corner's
    // y + 0.5, whatever the sample offset. Ordinary polygons never notice; the truncated edge functions of a
    // sliver accept pixels beyond its ends, and a console cuts those rows off. Modifier volume triangles have
    // no such limit (dreamcast-tests/pvr-tests/covmeas, mix sliver).
    int64_t rowFirst = INT64_MIN, rowLast = INT64_MAX;
    if (render_mode != RM_MODIFIER) {
        pvrisp::RowRange(FY, v4 ? 4 : 3, &rowFirst, &rowLast);
        // A polygon whose first row is the tile's last (31) starts a row earlier in that tile, the walk of its edges as
        // well (dreamcast-tests/pvr-tests/faredge_walk pages 0-3: slivers to a far corner whose top corner is in row
        // 28 .. 31 of the tile; their pushed edges draw in the row above the top corner when the row range lets them:
        // with the plain range 366 of the test's 3,537 tiles wrong, with this 0; rows 30 and 31 both a row earlier 272,
        // a top corner in the last quarter of its row 892; held out, faredge_far32: 390, 0, 36, 440; faredge_m131 page 6
        // tile (17,8), page 11 tile (2,10), faredge_ecap page 9 tile (7,7): row 30 drawn). Every polygon, not only one
        // with an edge whose block exponent was cut: dreamcast-tests/pvr-tests/edge2_walk page 6 (points, three equal
        // corners, whose edges are inside everywhere, and triangles of corners 1/1024 apart, at y = row + 0, 1/4, 1/2,
        // 3/4, 1023/1024 of rows 28 .. 31 of their tile): with the top corner in row 31 rows 30 and 31 are filled (25
        // points, 18 tiny triangles: 43 tiles wrong with the rule for cut edges only, 0 with it for every polygon); in
        // rows 28 .. 30 none starts earlier (225 tiles). The user tile clip has no part in it: dreamcast-tests/pvr-tests/
        // edge2_row31 (300 of 300 tiles; the same probes drawn with no user tile clip, with the clip of their own tile
        // and with one of the 3x3 tiles around it: in each, the 13 points with the top corner in row 31 fill rows 30
        // and 31 and 9, 9, 10 of the 12 tiny triangles there draw in row 30; none of the 75 in rows 28 .. 30 starts
        // earlier). Why row 31 is not known.
        if (rowFirst - (int64_t)area->top == 31)
            rowFirst--;
        // the walk down the tile of an edge with a cut block exponent begins at the polygon's first row (EdgeTile::Walk)
        const int64_t first = rowFirst - (int64_t)area->top;
        E12.SetFirstRow(first);
        E23.SetFirstRow(first);
        E31.SetFirstRow(first);
        if (v4)
            E41.SetFirstRow(first);
    }

    // Loop through ALL pixels in the tile (no smart clipping)
	for (int y = 0; y < 32; y++)
    {
        if (area->top + y < rowFirst || area->top + y > rowLast)
            continue;
        if (E12.RowOutside(y) || E23.RowOutside(y) || E31.RowOutside(y) || (v4 && E41.RowOutside(y)))
            continue;

        for (int x = 0; x < 32; x++)
        {
            bool inTriangle = E12.Inside(x, y) && E23.Inside(x, y) && E31.Inside(x, y) && (!v4 || E41.Inside(x, y));

            if (inTriangle) {
                u32 index = y * 32 + x;
                const ZType invW = Z.At(x, y, sampleCenters);
                // A pixel whose depth is below zero is drawn like any other (dreamcast-tests/pvr-tests/uvmeas, mix
                // wneg: 780,000 texels read off such polygons). An untextured one comes out black, which
                // dreamcast-tests/pvr-tests/huge took for "not drawn": the TSP's quotient has the sign of colour / w
                // alone (pvrtsp::DenominatorSeen), so the colour is below zero.
                PixelFlush_isp<render_mode>(params->isp.DepthMode, params->isp.ZWriteDis, invW, index, tag);
            }
        }
    }
}

void (*RasterizeTriangle_table[7])(DrawParameters* params, parameter_tag_t tag, const Vertex& v1, const Vertex& v2, const Vertex& v3, const Vertex* v4, taRECT* area) = {
    &RasterizeTriangle<RM_OPAQUE>,
    &RasterizeTriangle<RM_PUNCHTHROUGH_PASS0>,
    &RasterizeTriangle<RM_PUNCHTHROUGH_PASSN>,
    &RasterizeTriangle<RM_PUNCHTHROUGH_MV>,
    &RasterizeTriangle<RM_TRANSLUCENT_AUTOSORT>,
    &RasterizeTriangle<RM_TRANSLUCENT_PRESORT>,
    &RasterizeTriangle<RM_MODIFIER>
};

// The two accumulation buffers change places from tile to tile: what a tile finds in its secondary buffer
// is the finished picture of the tile rendered before it, and its own buffer still holds what was shaded into
// it two tiles before (which the background plane covers, unless the entry keeps Z: dreamcast-tests/pvr-tests/accbuf, a
// chain of keep Z tiles shows every second tile's stripes). Measured on a console (dreamcast-tests/pvr-tests/blendpos):
// tiles with nothing in them but a polygon that reads the secondary buffer show the picture of the tile above, and when
// that polygon multiplies with the buffer's alpha the value drops by one from tile to tile.
// It happens when a tile is written out, and it carries over from the last tile of one frame to the first of
// the next.
void NextTileAccumulationBuffers() {
    std::swap(colorBuffer1, colorBuffer2);
}

u8* GetColorOutputBuffer() {
    return (u8*)colorBuffer1;
}


// Clamp and flip a texture coordinate
template<bool pp_Clamp, bool pp_Flip>
ALWAYS_INLINE int ClampFlip(int coord, int size) {
    if (pp_Clamp) { // clamp
        if (coord < 0) {
            coord = 0;
        } else if (coord >= size) {
            coord = size-1;
        }
    } else if (pp_Flip) { // flip
        coord &= size*2-1;
        if (coord & size) {
            coord ^= size*2-1;
        }
    } else { //wrap
        coord &= size-1;
    }

    return coord;
}


const u32 MipPoint[11] =
{
    0x00003,//1
    0x00001 * 4,//2
    0x00002 * 4,//4
	0x00006 * 4,//8
	0x00016 * 4,//16
	0x00056 * 4,//32
	0x00156 * 4,//64
	0x00556 * 4,//128
	0x01556 * 4,//256
	0x05556 * 4,//512
	0x15556 * 4//1024
};

#if 0
static Color TextureFetchOld(TSP tsp, TCW tcw, int u, int v) {
    auto textel_stride = 8 << tsp.TexU;

    u32 start_address = tcw.TexAddr << 3;
    u32 base_address = start_address;
    
    u32 mip_bpp;
    if (tcw.VQ_Comp) {
        mip_bpp = 2;
    } else if (tcw.PixelFmt == PixelPal8) {
        mip_bpp = 8;
    }
    else if (tcw.PixelFmt == PixelPal4) {
        mip_bpp = 4;
    }
    else {
        mip_bpp = 16;
    }

    if (tcw.MipMapped) {
        base_address += MipPoint[tsp.TexU] * mip_bpp / 2;
    }

    u32 offset;
    if (tcw.VQ_Comp) {
        offset = twop(u, v, tsp.TexU, tsp.TexV) / 4;
    } else if (!tcw.ScanOrder) {
        offset = twop(u, v, tsp.TexU, tsp.TexV);
    } else {
        offset = u + textel_stride * v;
    }

    u16 memtel;
    if (tcw.VQ_Comp) {
        u8 index = emu_vram[(base_address + offset + 256*4*2) & VRAM_MASK];
        u16 *vq_book = (u16*)&emu_vram[start_address];
        memtel = vq_book[index * 4 + (u&1)*2 + (v&1) ];
    } else {
        memtel = (u16&)emu_vram[(base_address + offset *2) & VRAM_MASK];
    }

    u32 textel;
    switch (tcw.PixelFmt)
    {
        case PixelReserved:
        case Pixel1555: textel = ARGB1555_32(memtel); break;
        case Pixel565: textel = ARGB565_32(memtel); break;
        case Pixel4444: textel = ARGB4444_32(memtel); break;
        case PixelYUV: textel = 0xDE000EEF; break;
        case PixelBumpMap: textel = 0xDEADBEEF; break;
        case PixelPal4: textel = 0xFF00FFF0; break;
        case PixelPal8: textel = 0xFF0FF0FF; break;
    }
    return { .raw =  textel };
}
#endif

template<bool ScanOrder /* TODO: Expansion Patterns */>
ALWAYS_INLINE u32 ExpandToARGB8888(u32 color, u32 mode) {
	switch(mode)
	{
        case 0: return ARGB1555_32(color);
        case 1: return ARGB565_32(color);
        case 2: return ARGB4444_32(color);
        case 3: return ARGB8888_32(color);  // this one just shuffles
    }
    return 0xDEADBEEF;
}

template<bool VQ_Comp>
ALWAYS_INLINE u32 TexAddressGen(TCW tcw) {
    u32 base_address = tcw.TexAddr << 3;

    if (VQ_Comp) {
        base_address += 256 * 4 * 2;
    }

    return base_address;
}

template<bool VQ_Comp, bool MipMapped, bool ScanOrder>
ALWAYS_INLINE u32 TexOffsetGen(TSP tsp, int u, int v, u32 stride, u32 MipLevel) {
    u32 mip_offset;
    
    if (MipMapped) {
        mip_offset = MipPoint[3 + tsp.TexU - MipLevel];
    } else {
        mip_offset = 0;
    }

    if (VQ_Comp || !ScanOrder) {
        if (MipMapped) {
            return mip_offset + twop(u, v, (tsp.TexU - MipLevel), (tsp.TexU - MipLevel));
        } else {
            return mip_offset + twop(u, v, tsp.TexU, tsp.TexV);
        }
    } else {
        return mip_offset + u + stride * v;
    }
}

// 4.1 format
template<bool VQ_Comp, u8 PixelFmt>
constexpr u32 fBitsPerPixel() {
    u32 rv = 16;
    if (PixelFmt == PixelPal8) {
        rv = 8;
    }
    else if (PixelFmt == PixelPal4) {
        rv = 4;
    }
    else {
        rv = 16;
    }

    if (VQ_Comp) {
        return 8 * 2 / (64 / rv); // 8 bpp / (pixels per 64 bits)
    } else {
        return rv * 2;
    }
}

// TEXT_CONTROL (0x0E4) above the stride: bit 17, bit 16 and the bank bits 12..8.
#define TEXT_CONTROL_SWAP16      (1u << 17)
#define TEXT_CONTROL_INDEX_SWAP  (1u << 16)

// The byte a VQ texture's index is read from; offset: the index byte's place from the texture's start
// (mipmap level and twiddled place; the code book's 2 KB not included).
//  - The bank bits (TEXT_CONTROL 12..8, b) are XORed into address bits 23..19 of the texture's start plus
//    that offset, before the code book's 2 KB is added: ((start + offset) ^ (b << 19)) + 2048. Only the
//    index moves; code book entries, and every texture that is not VQ, are read where they are. Measured with
//    video memory filled with words that name their own place (every address bit), VQ code books whose
//    entries show the index byte read (dreamcast-tests/pvr-tests):
//      texctl_bank (four VQ 565 textures, b = 0 .. 15): 112,640 of 112,640 decoded index word halves as this
//        rule has it; b's bits 0..3 move address bits 19..22.
//      texctl_bankbase (index addresses past a 512 KB line, reached through the code book's 2 KB, the mipmap
//        offset or the twiddled place; b = 0 .. 15 and with the endian bits): 65,728 of 65,728 halves, where
//        XORing after the 2 KB is added is wrong on 6,098, XORing the start alone on 21,330.
//      texctl_bankmip (ten mipmapped VQ 565 textures whose start plus offset crosses a 512 KB line inside one
//        of the levels 8x8 .. 64x64, every level 64x64 .. 1x1 read, point sampled, bilinear, trilinear passes A
//        and B; b = 0 .. 15 and with the endian bits, 20 pages): 2,716 of 2,716 regions as the console, the same
//        on two console runs; XORing after the 2 KB: 1,214 regions wrong, the start alone 502, start plus the
//        mipmap offset (the twiddled place after) 205.
//      texctl_addr (VQ 565, mipmapped VQ 565, VQ palette 8 and 4 bpp, b = 0 .. 15 and with the endian bits, 22
//        pages): 31,410 of 31,410 halves; the textures that are not VQ (565 twiddled and strided, palette 8 and
//        4 bpp, YUV422 twiddled and strided, bump map, 4444, at 0x4b2000 .. 0x6d0000) read their own places,
//        every address bit, with every bank value: 565 twiddled and strided 1,441,792 of 1,441,792 words, 233
//        of 233 regions as the console (the same on two console runs); with the bank bits XORed into their
//        addresses too, 144 of the 233 regions are wrong.
//    GAP: bank bit 4 is taken to be address bit 23: inferred, not measured. With it set (b = 16 .. 31), or with
//    a sum past 8 MB, the console reads data that differs from run to run, as it would outside video memory (an
//    earlier texctl_bank, b = 16 .. 31: 8,800 to 17,716 of 65,536 pixels per texture differ between two runs;
//    an earlier texctl with bank values 16, 20, 31: its VQ textures differed between two runs, 1,424 to 2,728
//    pixels each). Here the address wraps at 8 MB.
//    GAP: an index read that lands in the framebuffer of the render that reads it gives what is there at that
//    moment, which depends on whether the tile has been written out yet (the earlier texctl_bank: 376 to 864
//    pixels differed on 3 pages); refsw3 reads video memory as it is when it renders the tile, so this is the
//    framebuffer write-out's timing, not modelled. The tests above keep their reads out of the framebuffer.
//  - Bit 16 swaps the index bytes in pairs: byte address ^ 1 (texctl page 1: the VQ 565 and palette 8 bpp VQ
//    textures read entry e ^ 1 where the index byte is e, all 4096 texels; texctl_addr pages 1, 3, 6, 7, 19,
//    21 and texctl_bankbase pages 16, 18: every decoded index word half in byte order 1 0 3 2, palette 4 bpp
//    VQ too). Nothing else moves (texctl page 1: the 565, palette 8 and 4 bpp textures read as laid out).
static inline u32 VQIndexAddress(u32 start_address, u32 offset) {
    u32 at = ((start_address + offset) ^ (((TEXT_CONTROL >> 8) & 31) << 19)) + 256 * 4 * 2;
    if (TEXT_CONTROL & TEXT_CONTROL_INDEX_SWAP)
        at ^= 1;
    return at;
}

u64 VQLookup(u32 start_address, u32 offset) {
    u64 *vq_book = (u64*)&emu_vram[start_address & (VRAM_MASK-7)];
    u8 index = emu_vram[VQIndexAddress(start_address, offset) & VRAM_MASK];
    return vq_book[index];
}

template<u32 StrideSel, u32 ScanOrder>
ALWAYS_INLINE u32 TexStride(u32 TexU, u32 MipLevel) {
    if (StrideSel && ScanOrder)
		return (TEXT_CONTROL&31)*32;
    else
        return (8U << TexU) >> MipLevel;
}

template<u32 PixelFmt>
ALWAYS_INLINE u32 DecodeTextel(u32 PalSelect, u64 memtel, u32 offset, bool twiddled) {
    auto memtel_32 = (u32*)&memtel;
    auto memtel_16 = (u16*)&memtel;
    auto memtel_8 = (u8*)&memtel;

    switch (PixelFmt)
    {
        case PixelReserved:
        case Pixel1555:
        case Pixel565:
        case Pixel4444:
        case PixelBumpMap:
            return memtel_16[offset & 3]; break;

        case PixelYUV: {
            // A texel is Y and one chroma byte: the texel at an even x has U, its neighbour at x + 1 in the
            // same row has V, and the two share both. In a texture that is not twiddled they are next to each
            // other in memory; in a twiddled one bit 1 of the index is x's lowest bit, so they are two apart
            // (hardware pairs by x there too, not by memory order: dreamcast-tests/pvr-tests/yuvbil).
            u32 self = offset & 3;
            u32 tu = twiddled ? (self & 1) : (self & 2);
            u32 tv = twiddled ? (self | 2) : (self | 1);
            (void)memtel_32;
            return YUV422(memtel_8[self * 2 + 1], memtel_8[tu * 2], memtel_8[tv * 2]);
            }

        case PixelPal4: {
            auto local_idx = (memtel >> (offset & 15)*4) & 15;
            auto idx = PalSelect * 16 | local_idx;
            OFFLINE_READ(PALETTE_RAM_START_addr + idx * 4);
            return PALETTE_RAM[idx];
        }
        break;
        case PixelPal8: {
            auto local_idx = memtel_8[offset & 7];
            auto idx = (PalSelect / 16) * 256 | local_idx;
            OFFLINE_READ(PALETTE_RAM_START_addr + idx * 4);
            return PALETTE_RAM[idx];
        }
        break;
    }
    return 0xDEADBEEF;
}

template<u32 PixelFmt>
ALWAYS_INLINE u32 GetExpandFormat() {
    if (PixelFmt == PixelPal4 || PixelFmt == PixelPal8) {
        return PAL_RAM_CTRL&3;
    } else if (PixelFmt == PixelBumpMap || PixelFmt == PixelYUV) {
        return 3;
    } else {
        return PixelFmt & 3;
    }
}

Color MipDebugColor[11] = {
    {.raw = 0xFF000060},
    {.raw = 0xFF000090},
    {.raw = 0xFF0000A0},

    {.raw = 0xFF006000},
    {.raw = 0xFF009000},
    {.raw = 0xFF00A000},

    {.raw = 0xFF600000},
    {.raw = 0xFF900000},
    {.raw = 0xFFA00000},

    {.raw = 0xFFA0A0A0},
    {.raw = 0xFFF0F0F0},
};

template<bool VQ_Comp, bool MipMapped, bool ScanOrder_, bool StrideSel_, u8 PixelFmt>
static Color TextureFetch(TSP tsp, TCW tcw, int u, int v, u32 MipLevel) {

    if (MipLevel == (tsp.TexU + 3)) {
        if (PixelFmt == PixelYUV) {
            return TextureFetch<VQ_Comp, MipMapped, ScanOrder_, StrideSel_, Pixel565>(tsp, tcw, u, v, MipLevel);
        }
     }

    // These are fixed to zero for pal4/pal8
    constexpr u32 ScanOrder = ScanOrder_ & ~(PixelFmt == PixelPal4 || PixelFmt == PixelPal8);
    constexpr u32 StrideSel = StrideSel_ & ~(PixelFmt == PixelPal4 || PixelFmt == PixelPal8);
    
    u32 stride = TexStride<StrideSel, ScanOrder>(tsp.TexU, MipLevel);

    u32 start_address = tcw.TexAddr << 3;

    auto fbpp = fBitsPerPixel<VQ_Comp, PixelFmt>();
    
    auto base_address = TexAddressGen<VQ_Comp>(tcw);
    auto offset = TexOffsetGen<VQ_Comp, MipMapped, ScanOrder>(tsp, u, v, stride, MipLevel);

    u64 memtel;
    if (VQ_Comp) {
        memtel = VQLookup(start_address, offset * fbpp / 16);
    } else {
        memtel = (u64&)emu_vram[(base_address + offset * fbpp / 16) & (VRAM_MASK-7)];
    }

    // TEXT_CONTROL bit 17: the two bytes of every 16 bit unit of texture data swapped, in every format: 16 bit
    // texels, palette indices (8 bpp: byte address ^ 1; 4 bpp: nibble address ^ 2) and VQ code book entries;
    // not VQ index bytes, and no address changes (dreamcast-tests/pvr-tests/texctl pages 2, 3 and 11 (0x30200,
    // bank value 2 too): 565 raster and twiddled, palette 8 and 4 bpp, VQ 565, VQ palette 8 and 4 bpp, every
    // texel; page 3 with bit 16: VQ palette 8 bpp read at ^ 9, both at once; 84 of 84 regions as the console;
    // texctl_addr pages 2, 3, 7, 20, 21, textures read straight from memory that names its places: 565 twiddled
    // and strided, all 32,768 words each their own place with the bytes swapped; YUV422 twiddled and strided,
    // bump map (its intensity shown through SRC_ALPHA: 3,954 of 4,096 pixels change with the swap), 4444,
    // palette and VQ textures: every region as the console; texctl_bankmip pages 17, 18: mipmapped VQ 565,
    // bilinear and trilinear too, every region as the console).
    if (TEXT_CONTROL & TEXT_CONTROL_SWAP16)
        memtel = ((memtel & 0x00ff00ff00ff00ffull) << 8) | ((memtel >> 8) & 0x00ff00ff00ff00ffull);

    u32 textel = DecodeTextel<PixelFmt>(tcw.PalSelect, memtel, offset, VQ_Comp || MipMapped || !ScanOrder);

    u32 expand_format = GetExpandFormat<PixelFmt>();

    

    textel = ExpandToARGB8888<ScanOrder>(textel, expand_format);

    // auto old = TextureFetch2(texture, u, v);
    // if (textel != old.raw) {
    //     textel = TextureFetch2(texture, u, v).raw;
    //     textel = TextureFetch(texture, u, v).raw;
    //     die("Missmatch");
    // }
    // auto old = raw_GetTexture(tsp, tcw)[u + v * stride];
    // if (textel != old) {
    //     die("Missmatch");
    // }
    // This uses the old path for debugging
    // return { .raw = raw_GetTexture(tsp, tcw)[u + v * textel_stride] };
    RENDLOG("T: %08X %d %d %d", textel, u, v, MipLevel);
    return { .raw =  textel };
    // return MipDebugColor[10-MipLevel];
}
u32 to_u8_256(u8 v) {
    return v + (v >> 7);
}
// The multiplier, out of 256, that the hardware uses for a coefficient value x (0..255): 255 counts as 256,
// everything else as itself, and an inverted coefficient is 256 minus that, so a factor and its inverse
// always add up to exactly 256 (dreamcast-tests/pvr-tests/blend).
ALWAYS_INLINE u32 BlendMul(u32 x, bool inverted) {
    u32 k = x + (x == 255);
    return inverted ? 256 - k : k;
}

using TextureFetch_fp = decltype(&TextureFetch<false, false, false, false, 0>);

// A coordinate in 1/256 texel as refsw3 carries it: as it is inside +-2^30, beyond that the sign and the low 26 bits
// (all that wrapping, flipping and clamping look at; see TexCoordFixed).
ALWAYS_INLINE int CoordKeep(int64_t f) {
    if (f >= 1073741824)
        return (int)(1073741824 + (f & 67108863));
    if (f < -1073741824)
        return (int)(-1073741824 + (f & 67108863));
    return (int)f;
}

// A texture coordinate in 1/256 texel as hardware sees it. Hardware's coordinates come out a hair toward
// zero: a coordinate that is exactly on a boundary belongs to the texel before it, except at 0
// (dreamcast-tests/pvr-tests/sample: point sampling with half-pixel shifts, bilinear with the weights of page 2).
// Values within 1/32 of a unit of an integer are treated as being exactly on it.
ALWAYS_INLINE int TexCoordFixed(const pvrfp::Fp& u, int shift) {
    // u * 2^shift (the size of the texture in 1/256 texel: exact, u has 17 bits), rounded down in two's complement,
    // below zero too (dreamcast-tests/pvr-tests/coordfix: every weight step from -4 to +4 texels and at up to 2^17
    // texels, clamp and flip; toward zero gets 437 of its 1568 regions wrong). The coordinate the TSP delivers is
    // already rounded down (PlaneStepper3), which is what makes a coordinate exactly on a texel boundary show the
    // texel before it (dreamcast-tests/pvr-tests/sample, uvmeas). Far outside an int (coordinates of thousands of
    // texture repeats): the sign and the low 26 bits, which is all that wrapping, flipping and clamping look at.
    // That holds at 2^128 and beyond too: there is no overflow to texel 0 there (as the old float code had it), a
    // clamped one is the last texel, below zero the first, wrapped and flipped the low bits are zeros
    // (dreamcast-tests/pvr-tests/texfar_edge: quotients of exactly 2^128, (1 + 2^-15) 2^128, 1.5 2^128, 2^129,
    // 2^132 and up to the largest float times the size, in 1/256 texel, of 8, 64 and 1024 texel textures, along U
    // and V, point and bilinear: 63 clamped regions the last texel, as above 2^127; texfar: 28 regions;
    // texfar_quot: quotients over a zero 1/w up to 2^260 in 1/256 texel, 103 regions; texfar_m1q: up to 2^261).
    // The low bits are the integer's own up to where the quotient's 17 bits leave them (texfar_bits: tops 2^9 ..
    // 2^36, wrap and flip, both signs, U and V: 300 of 300, as refsw3 had it).
    // A quotient is never an infinity or a NaN (Divide); u.bad is for a caller that has one.
    if (u.bad || u.zero())
        return 0;
    const int top = u.top() + shift;                    // |u| 2^shift is in [2^(top - 1), 2^top)
    const int e = u.exp + shift;                        // u 2^shift = mant 2^e
    if (top <= 31) {
        int64_t f;
        if (e >= 0)
            f = (int64_t)(u.mant << e);
        else {
            const int s = -e;
            const uint64_t lost = s >= 64 ? u.mant : (u.mant & ((1ull << s) - 1));
            f = s >= 64 ? 0 : (int64_t)(u.mant >> s);
            if (u.neg && lost)
                f += 1;
        }
        if (u.neg)
            f = -f;
        return CoordKeep(f);
    }
    // 2^30 and more: an integer; its low 26 bits (two's complement for a negative one)
    const uint64_t low = e >= 26 ? 0 : (uint64_t)(u.mant << e) & 67108863;
    if (!u.neg)
        return (int)(1073741824 + low);
    return (int)(-1073741824 + (int64_t)((67108864 - low) & 67108863));
}

// Fetch pixels from UVs, interpolate
template<bool pp_IgnoreTexA,  bool pp_ClampU, bool pp_ClampV, bool pp_FlipU, bool pp_FlipV, u32 pp_FilterMode>
static Color TextureFilter(TSP tsp, TCW tcw, const pvrfp::Fp& u, const pvrfp::Fp& v, u32 MipLevel, TextureFetch_fp fetch) {
        
    // The half texel offset belongs to bilinear filtering only: a point sample is taken at the pixel center
    // (dreamcast-tests/pvr-tests/sample).
    int halfpixel = (HALF_OFFSET.texure_pixel_half_offset && pp_FilterMode != 0) ? -128 : 0;

    if (MipLevel >= (tsp.TexU + 3)) {
        MipLevel = tsp.TexU+3;
    }
    int sizeU, sizeV;

    if (tcw.MipMapped && !TcwRaster(tcw)) {
        sizeU = (8 << tsp.TexU) >> MipLevel;
        sizeV = (8 << tsp.TexU) >> MipLevel;
    } else {
        sizeU = 8 << tsp.TexU;
        sizeV = 8 << tsp.TexV;
    }

    auto log2 = [](int n) { return pvrfp::TrailingZeros((uint64_t)n); };     // the sizes are powers of two
    int ui = TexCoordFixed(u, log2(sizeU) + 8) + halfpixel;
    int vi = TexCoordFixed(v, log2(sizeV) + 8) + halfpixel;
    if (tcw.MipMapped && !TcwRaster(tcw)) {
        // A mipmapped texture's coordinates are made at the full size, the half texel taken off there, and
        // only then shifted down to the level: at level L the bilinear footprint sits 1/2 - 1/2^(L+1) texel
        // further on than with a half texel of the level's own size. polly2-data dumps sa_slow1-3 (dreamster):
        // all of video memory = the console's after the render, where a half texel of the level's size got
        // 74673 of 921600 pixels wrong, every one of them a bilinear sample below the full size.
        // It is as tall as it is wide whatever its V size says, but the V coordinate is made with the V size,
        // half texel and all, and then scaled to the full height: declared 16x8, the bilinear footprint sits
        // one whole row higher than declared 16x16 (dreamcast-tests/pvr-tests/sample, "declared").
        const int d = (int)tsp.TexU - (int)tsp.TexV;
        // Scaled up (declared shorter), the coordinate keeps the fraction bits the scaling brings up: it is
        // v at the full height, 8 fraction bits there, less the half texel of the declared height, 128 << d, which
        // tells only from d = 4 on, where the lowest of them reaches the 5 bit weight (dreamcast-tests/pvr-tests/
        // texfar_mipv row 12, 256x256 declared 256x8, V flipped, bilinear, v below zero: 5 of 20 regions one step
        // of green off with the declared height's coordinate scaled, 20 of 20 so; texfar_decl, every V weight
        // step of 256 declared x8, x16, x32, 128x8, 64x8 at levels 0, 1, 2, wrap, flip, clamp, both signs: 122 of
        // 300 regions wrong scaled, every one with d >= 4 + level, 300 of 300 so). The scaling is of the whole
        // coordinate, however far out: a clamped one past the declared V size is the last row, not an int that
        // overflowed to row 0 (dreamcast-tests/pvr-tests/texfar_mip rows 11 and 12, 64x64 declared 64x8, V
        // clamped, point and bilinear: 24 regions of v from 2^64 to the largest float times 2^11 in 1/256 texel,
        // every one the last row; texfar_mipv: the low bits after the scaling, d = 1, 2, 3, 5, 280 regions).
        int v0 = d > 0 ? CoordKeep((int64_t)TexCoordFixed(v, 3 + tsp.TexU + 8) + (int64_t)halfpixel * (1 << d))
                       : (TexCoordFixed(v, 3 + tsp.TexV + 8) + halfpixel) >> -d;
        ui = (TexCoordFixed(u, 3 + tsp.TexU + 8) + halfpixel) >> MipLevel;
        vi = v0 >> MipLevel;
    }

    // Clamped, a coordinate below 0 (after the half texel) is 0 itself: texel 0 and texel 1 with weight 0, not
    // texel 0 twice. A colour cannot tell, the bump map's angle R can: its circular step is not exact at
    // weight 0 when it starts from the second texel (dreamcast-tests/pvr-tests/bumpclamp pages 0 and 2;
    // polly2-data rayman_ingame's bump maps, V clamped, pvr-tests/bumpiso). Past the last texel the second
    // texel is clamped as well, it does not wrap round (bumpclamp pages 1, 3, 5, 6).
    if (pp_ClampU && ui < 0)
        ui = 0;
    if (pp_ClampV && vi < 0)
        vi = 0;
    // A strided texture (not twiddled, stride select; the row length in memory comes from TEXT_CONTROL) is read
    // in two ways of its own (dreamcast-tests/pvr-tests/sample, the "stride 32" variants):
    //  - the flip bits do nothing: the texture repeats, or clamps, but is never mirrored;
    //  - with bilinear filtering, 565, 1555 and 4444 texels are widened to 8 bits by shifting only, the low
    //    bits zero, where every other texture (and this one point sampled) repeats the top bits there
    //    (dreamcast-tests/pvr-tests/texfmt, "stride 32, bilinear": the colours and the alpha of 4444).
    const bool pal = tcw.PixelFmt == PixelPal4 || tcw.PixelFmt == PixelPal8;
    const bool strided = tcw.StrideSel && tcw.ScanOrder && !pal;
    auto wrapU = [&](int c) { return strided ? ClampFlip<pp_ClampU, false>(c, sizeU) : ClampFlip<pp_ClampU, pp_FlipU>(c, sizeU); };
    auto wrapV = [&](int c) { return strided ? ClampFlip<pp_ClampV, false>(c, sizeV) : ClampFlip<pp_ClampV, pp_FlipV>(c, sizeV); };

    auto offset00 = fetch(tsp, tcw, wrapU((ui >> 8) + 1), wrapV((vi >> 8) + 1), MipLevel);
    auto offset01 = fetch(tsp, tcw, wrapU((ui >> 8) + 0), wrapV((vi >> 8) + 1), MipLevel);
    auto offset10 = fetch(tsp, tcw, wrapU((ui >> 8) + 1), wrapV((vi >> 8) + 0), MipLevel);
    auto offset11 = fetch(tsp, tcw, wrapU((ui >> 8) + 0), wrapV((vi >> 8) + 0), MipLevel);

    if (strided && pp_FilterMode != 0 && tcw.PixelFmt != PixelYUV && tcw.PixelFmt != PixelBumpMap) {
        const u8 rb = tcw.PixelFmt == Pixel4444 ? 0xf0 : 0xf8;
        const u8 g = tcw.PixelFmt == Pixel4444 ? 0xf0 : tcw.PixelFmt == Pixel565 ? 0xfc : 0xf8;
        for (Color* c : { &offset00, &offset01, &offset10, &offset11 }) {
            c->bgra[0] &= rb;
            c->bgra[1] &= g;
            c->bgra[2] &= rb;
            if (tcw.PixelFmt == Pixel4444)      // its alpha too; the one alpha bit of 1555 stays 0 or 255
                c->bgra[3] &= 0xf0;
        }
    }

    Color textel = {0xAF674839};

    if (pp_FilterMode == 0) {
        // Point sampling
        for (int i = 0; i < 4; i++)
        {
            textel = offset11;
        }
    } else {
        // Bilinear filtering
        // Hardware's weights have 5 bits: a 0 -> 255 step over 32 pixels reads 0, 7, 15, 23 .. and does
        // not move for shifts of less than 1/32 texel (dreamcast-tests/pvr-tests/sample page 2).
        int ublend = (ui & 255) >> 3;
        int vblend = (vi & 255) >> 3;

        int nublend = 32 - ublend;
        int nvblend = 32 - vblend;

        // Two steps, each truncated: along u within each row, then between the rows
        // (dreamcast-tests/pvr-tests/sample page 1: 52224 of 52224 pixels; one sum over the four texels gets 38523).
        for (int i = 0; i < 4; i++)
        {
            u32 row0 = (offset11.bgra[i] * nublend + offset10.bgra[i] * ublend) >> 5;
            u32 row1 = (offset01.bgra[i] * nublend + offset00.bgra[i] * ublend) >> 5;
            textel.bgra[i] = (row0 * nvblend + row1 * vblend) >> 5;
        };

        if (tcw.PixelFmt == PixelBumpMap) {
            // A bump texel's low byte is the angle R, and the filter takes it round the circle by the shorter
            // way, in the same two steps (dreamcast-tests/pvr-tests/bumpbil): it starts at the end from which the other
            // is at most half a turn upward, and adds (distance * weight) >> 5. The weight of the end it does not
            // start at is the 5 bit weight when that is the second texel, and its complement, 31 - weight,
            // when it is the first: never 32. Halfway from 210 to 0 is 233; from 64 to 0 the 32 pixels read
            // 62, 60 .. 0, from 0 to 64 they read 0, 2 .. 62. S, the high byte, is filtered as a colour is.
            auto circular = [](u32 first, u32 second, int weight) -> u32 {
                // (not "small": the Windows headers define it as a type)
                const u32 high = first > second ? first : second, low = first > second ? second : first;
                const u32 apart = high - low;
                const u32 start = apart < 128 ? low : high;
                const u32 distance = apart < 128 ? apart : 256 - apart;
                const int w = start == first ? weight : 31 - weight;
                return (start + ((distance * w) >> 5)) & 255;
            };
            const u32 row0 = circular(offset11.bgra[0], offset10.bgra[0], ublend);
            const u32 row1 = circular(offset01.bgra[0], offset00.bgra[0], ublend);
            textel.bgra[0] = circular(row0, row1, vblend);
        }

        // A texture without mipmaps is filtered bilinearly by either trilinear pass, at full strength
        // (dreamcast-tests/pvr-tests/mip, "565 without mipmaps"). The trilinear fade is PixelFlush_tsp's, of the
        // finished colour.
    }
        

    if (pp_IgnoreTexA)
    {
        textel.a = 255;
    }

    RENDLOG("TF: %08X", textel.raw);

    return textel;
}

// Combine Base, Textel and Offset colors
template<bool pp_Texture, bool pp_Offset, u32 pp_ShadInstr>
static Color ColorCombiner(Color base, Color textel, Color offset) {

    Color rv = base;
    if (pp_Texture)
    {
        if (pp_ShadInstr == 0)
        {
            //color.rgb = texcol.rgb;
            //color.a = texcol.a;

            rv = textel;
        }
        else if (pp_ShadInstr == 1)
        {
            //color.rgb *= texcol.rgb;
            //color.a = texcol.a;
            // hardware: a plain (a * b) >> 8, so 255 * 255 is 254 (dreamcast-tests/pvr-tests/env)
            for (int i = 0; i < 3; i++)
            {
                rv.bgra[i] = (textel.bgra[i] * base.bgra[i]) >> 8;
            }

            rv.a = textel.a;
        }
        else if (pp_ShadInstr == 2)
        {
            //color.rgb=mix(color.rgb,texcol.rgb,texcol.a);
            // hardware: the blend unit's arithmetic (255 counts as 256, each product shifted on its own)
            u32 tb = BlendMul(textel.a, false);
            u32 cb = 256 - tb;

            for (int i = 0; i < 3; i++)
            {
                rv.bgra[i] = ((textel.bgra[i] * tb) >> 8) + ((base.bgra[i] * cb) >> 8);
            }

            rv.a = base.a;
        }
        else if (pp_ShadInstr == 3)
        {
            //color*=texcol
            for (int i = 0; i < 3; i++)
            {
                rv.bgra[i] = (textel.bgra[i] * base.bgra[i]) >> 8;
            }
            // alpha: a value of 255 counts as 256 on either side (0x80 * 255 stays 0x80 whichever of the two
            // is the 255, dreamcast-tests/pvr-tests/env page 1)
            rv.a = std::min((BlendMul(textel.a, false) * BlendMul(base.a, false)) >> 8, 255U);
        }

        if (pp_Offset) {
            // mix only color, saturate
            for (int i = 0; i < 3; i++)
            {
                rv.bgra[i] = std::min(rv.bgra[i] + offset.bgra[i], 255);
            }
        }
    }

    RENDLOG("CC: %08X", rv.raw);
    return rv;
}

// The bump map function as a console computes it (dreamcast-tests/pvr-tests/bumpfn: every S and R under fourteen
// lights):   I = K1 + ((K2 * sin[S]) >> 8) +- ((((K3 * cos[S]) >> 8) * cos[4 * x]) >> 8), clamped to 0..255
// with a = |R - Q| (the magnitude of the difference, not its low 8 bits), x = a & 63 in the quadrants a >> 6 = 0, 2
// and 63 - x in the quadrants 1, 3, and the last term subtracted in the quadrants 1 and 2. Every product loses its
// low 8 bits before the next step. The two tables are not a sine and its mirror: each is a line per 8 entries
// (slope (32 - i) >> 1 eighths per entry in the sine's segment i), and they were read from the console's
// pictures. sin[0] is 0 or 1 and cos[255] is 0, 1 or 2: no light can show which.
static const u8 BM_HW_SIN[256] = {
      0,  2,  4,  6,  8, 10, 12, 14, 15, 17, 19, 21, 23, 25, 27, 29,
     29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 47, 49, 50, 52, 54, 56,
     56, 58, 60, 62, 63, 65, 67, 69, 69, 71, 73, 74, 76, 78, 79, 81,
     81, 83, 85, 86, 88, 90, 91, 93, 93, 95, 96, 98, 99,101,102,104,
    104,106,107,109,110,112,113,115,115,117,118,120,121,122,124,125,
    125,127,128,130,131,132,134,135,135,137,138,139,140,142,143,144,
    144,146,147,148,149,151,152,153,153,154,156,157,158,159,160,161,
    161,163,164,165,166,167,168,169,169,170,171,172,173,174,175,176,
    177,178,179,180,181,182,183,184,186,187,188,189,190,191,192,193,
    194,195,196,197,198,199,200,200,202,203,204,205,205,206,207,208,
    209,210,211,212,212,213,214,215,216,217,218,218,219,220,220,221,
    222,223,224,224,225,226,226,227,228,229,229,230,230,231,231,232,
    233,234,234,235,235,236,236,237,238,239,239,240,240,240,241,241,
    242,243,243,244,244,244,245,245,246,247,247,247,247,248,248,248,
    249,250,250,250,250,251,251,251,252,252,253,253,253,253,253,253,
    254,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};
static const u8 BM_HW_COS[256] = {
    255,255,255,255,255,255,255,255,254,254,254,254,254,254,254,254,
    252,252,252,252,252,252,252,251,250,250,250,250,249,249,249,249,
    247,247,247,247,246,246,246,246,244,244,244,243,243,243,242,242,
    240,240,240,239,239,239,238,238,236,236,235,235,234,234,233,233,
    231,231,230,230,229,229,228,228,226,226,225,225,224,223,223,222,
    220,220,219,219,218,217,217,216,214,214,213,212,211,211,210,209,
    207,207,206,205,204,204,203,202,200,199,199,198,197,196,195,194,
    192,192,191,190,189,188,187,186,184,183,182,181,180,179,178,177,
    176,175,174,173,172,171,170,169,169,168,167,166,165,164,163,162,
    161,160,159,158,157,156,155,153,153,152,151,150,148,147,146,145,
    144,143,142,141,139,138,137,136,135,134,133,131,130,129,127,126,
    125,124,123,121,120,119,117,116,115,114,112,111,109,108,106,105,
    104,103,101,100, 98, 97, 95, 94, 93, 92, 90, 89, 87, 85, 84, 82,
     81, 80, 78, 77, 75, 73, 72, 70, 69, 68, 66, 64, 62, 61, 59, 57,
     56, 55, 53, 51, 49, 48, 46, 44, 43, 41, 40, 38, 36, 34, 32, 30,
     29, 28, 26, 24, 22, 20, 18, 16, 15, 13, 11,  9,  7,  5,  3,  1,
};

static Color BumpMapper(Color textel, Color offset) {
    u8 K1 = offset.a;
    u8 K2 = offset.r;
    u8 K3 = offset.g;
    u8 Q = offset.b;

    u8 R = textel.b;
    u8 S = textel.g;

    s32 a = (s32)R - (s32)Q;
    if (a < 0)
        a = -a;
    const s32 quad = a >> 6, x = a & 63;
    const s32 across = BM_HW_COS[4 * ((quad & 1) ? 63 - x : x)];
    const s32 t3 = (((K3 * BM_HW_COS[S]) >> 8) * across) >> 8;
    s32 I = K1 + ((K2 * BM_HW_SIN[S]) >> 8) + ((quad == 1 || quad == 2) ? -t3 : t3);
    if (I < 0) {
        I = 0;
    } else if (I > 255) {
        I = 255;
    }

	Color res;
	res.b = 255;
	res.g = 255;
	res.r = 255;
	res.a = I;

    RENDLOG("BM: %08X", res.raw);
    return res;
}

// Added to an interpolated color before it is truncated to 8 bits: hardware rounds to nearest, and a value
// exactly half way goes down (the middle of a 0..255 ramp over an even number of pixels is 127;
// dreamcast-tests/pvr-tests/shade). The threshold is a little under one half: anything from 0.4967 to 0.4975 reproduces
// all 80256 samples of that page, 0.4961 and 0.499 do not.
// (PlaneStepper3::Rounded: floor(v + 1/2), exact on the quotient's 17 bits.)

// Interpolate the base color, also cheap shadows modifier
template<bool pp_UseAlpha, bool pp_CheapShadows>
ALWAYS_INLINE Color InterpolateBase(const PlaneStepper3* Col, PixPos p, const pvrfp::Fp& W, bool InVolume) {
    Color rv;
    u32 mult = 256;

    // intensity shadows: Col is then the plane set of the scaled vertex colours (IPs3::SetupIntensity); per vertex
    // (c * intensity) >> 8, so 0x80 halves exactly and 0xff still darkens by one level (dreamcast-tests/pvr-tests/modvol)

    // the intensity scales the colour before it is clamped: a colour far above 255 (a quotient of extreme
    // depths) stays white at 0x80 and 0xff, and only 0 makes it black (dreamcast-tests/pvr-tests/modvol, pages 0 to 2)
    for (int i = 0; i < 4; i++) {
        u32 c = (u32)Col[i].Rounded(p, W, 65535);
        c = (c * mult) >> 8;
        rv.bgra[i] = c > 255 ? 255 : c;
    }

    if (!pp_UseAlpha)
    {
        rv.a = 255;
    }

    RENDLOG("IB: %08X", rv.raw);
    return rv;
}

// Interpolate the offset color, also cheap shadows modifier
template<bool pp_CheapShadows>
ALWAYS_INLINE Color InterpolateOffs(const PlaneStepper3* Ofs, PixPos p, const pvrfp::Fp& W, bool InVolume) {
    Color rv;
    u32 mult = 256;

    for (int i = 0; i < 3; i++) {
        u32 c = (u32)Ofs[i].Rounded(p, W, 255);
        rv.bgra[i] = (c * mult) >> 8;
    }
    rv.bgra[3] = (u8)Ofs[3].Rounded(p, W, 255);

    RENDLOG("IO: %08X", rv.raw);
    return rv;
}

// select/calculate blend coefficient for the blend unit
template<u32 pp_AlphaInst, bool srcOther>
ALWAYS_INLINE Color BlendCoefs(Color src, Color dst) {
    Color rv;

    switch(pp_AlphaInst>>1) {
        // zero
        case 0: rv.raw = 0; break;
        // other color
        case 1: rv = srcOther? src : dst; break;
        // src alpha
        case 2: for (int i = 0; i < 4; i++) rv.bgra[i] = src.a; break;
        // dst alpha
        case 3: for (int i = 0; i < 4; i++) rv.bgra[i] = dst.a; break;
    }

    return rv;
}

// Blending Unit implementation. Alpha blend, accum buffers and such
template<u32 pp_SrcSel, u32 pp_DstSel, u32 pp_SrcInst, u32 pp_DstInst, bool pp_AlphaTest>
static bool BlendingUnit(u32 index, Color col)
{
    bool at = true;

    if (pp_AlphaTest) {
        if (col.a < PT_ALPHA_REF) {
            col.a = 0;
            at = false;
        } else {
            col.a = 255;
        }
    }

    Color rv;
    Color src = {.raw  = pp_SrcSel ? colorBuffer2[index] : col.raw };
    Color dst = {.raw = pp_DstSel ? colorBuffer2[index] : colorBuffer1[index] };
        
    Color src_blend = BlendCoefs<pp_SrcInst, false>(src, dst);
    Color dst_blend = BlendCoefs<pp_DstInst, true>(src, dst);

    // Each product is shifted down on its own, then they are added and saturated (dreamcast-tests/pvr-tests/blend:
    // 3072 of 3072 squares, color and alpha).
    for (int j = 0; j < 4; j++)
    {
        u32 s = (src.bgra[j] * BlendMul(src_blend.bgra[j], pp_SrcInst & 1)) >> 8;
        u32 d = (dst.bgra[j] * BlendMul(dst_blend.bgra[j], pp_DstInst & 1)) >> 8;
        rv.bgra[j] = std::min(s + d, 255U);
    }

    // A polygon that reads buffer 2 and writes buffer 2 writes nothing on hardware: buffer 2 stays as it was
    // with all 64 pairs of instructions, in every pixel (dreamcast-tests/pvr-tests/blend, blendsel). With ONE or SRC_A
    // and ZERO it has a side effect, though: see tspAlphaStuck.
    if (!(pp_SrcSel && pp_DstSel))
        (pp_DstSel ? colorBuffer2[index] : colorBuffer1[index]) = rv.raw;
    
    RENDLOG("BU: %08X %08X %08X %08X %08X %d", rv.raw, src_blend.raw, dst_blend.raw, src.raw, dst.raw, at);

    return at;
}

// weight of the fog side, out of 256 (experiment: FOG_VARIANT selects the mapping)
#ifndef FOG_VARIANT
#define FOG_VARIANT 0
#endif
ALWAYS_INLINE u32 FOG_K(u32 a) {
    return FOG_VARIANT == 0 ? a : FOG_VARIANT == 1 ? a + (a == 255) : a + (a >> 7);
}

ALWAYS_INLINE u8 LookupFogTable(const pvrfp::Fp& W) {
    // FOG_DENSITY: an 8 bit mantissa (bit 7 the units, bits 6..0 the fraction) and a signed 8 bit exponent. The
    // product with the pixel's 1/w (the TSP's own: the plane's value, 16 bits, dreamcast-tests/pvr-tests/tspw) is the
    // fog's w, exact: 8 x 16 bits fit in a float's 24, so no rounding mode is ever asked for. Then clamped to
    // 1 .. 256 - 2^-16 (dreamcast-tests/pvr-tests/fog, tspw). The table index is its exponent's low 3 bits (plus 1)
    // and the top 4 bits of its 23 bit fraction; the blend between two entries the next 8 bits.
    static_assert(8 + pvrtsp::PlaneHW::valueBits <= 24, "the fog product is exact only with a 16 bit 1/w");
    const u8* fog_density = (const u8*)&FOG_DENSITY;
    const int dm = fog_density[1], de = (s8)fog_density[0];
    uint64_t M;                         // fogW = M 2^E, M in [2^23, 2^24)
    int E;
    // A 1/w below zero gets no fog, as one below 1 (dreamcast-tests/pvr-tests/slivers_dzspec: 1,501 rows of triangles
    // with a flat depth of -2^-8 .. -2^8 and 575 of determinant zero whose mean is below zero, all black under a
    // white fog as the console has them, where its magnitude would give 18,012 and 18,400 pixels grey; slivers_tsp:
    // 9 pixels of slivers whose 1/w is below zero)
    if (W.bad || W.neg || W.zero() || dm == 0) {
        M = 1ull << 23; E = -23;        // below 1: 1
    } else {
        uint64_t m = (uint64_t)dm * W.mant;     // exact, at most 24 bits (W >= 0 has at most 16)
        int e = W.exp + de - 7;
        const int n = pvrfp::Fp::Bits(m);
        m <<= 24 - n; e -= 24 - n;
        M = m; E = e;
        if (E + 23 < 0) { M = 1ull << 23; E = -23; }                    // below 1: 1
        else if (E + 23 > 7) { M = (1ull << 24) - 1; E = -16; }         // 256 and above: 256 - 2^-16
    }
    const u32 exponent = (u32)(E + 23 + 127), fraction = (u32)(M - (1ull << 23));

    u32 index = (((exponent + 1) & 7) << 4) | ((fraction >> 19) & 15);

    u8 blendFactor = (fraction >> 11) & 255;
    u8 blend_inv = 255^blendFactor;

    OFFLINE_READ(FOG_TABLE_START_addr + index * 4);
    auto fog_entry = (u8*)&FOG_TABLE[index];

    u32 fk = FOG_K(blendFactor);
    u8 fog_alpha = (fog_entry[0] * fk + fog_entry[1] * (256 - fk)) >> 8;

    return fog_alpha;
}

// Color Clamp and Fog a pixel
template<bool pp_Offset, bool pp_ColorClamp, u32 pp_FogCtrl>
ALWAYS_INLINE Color FogUnit(Color col, const pvrfp::Fp& invW, u8 offs_a) {
    if (pp_ColorClamp) {
        Color clamp_max = { FOG_CLAMP_MAX };
        Color clamp_min = { FOG_CLAMP_MIN };

        for (int i = 0; i < 4; i++)
        {
            col.bgra[i] = std::min(col.bgra[i], clamp_max.bgra[i]);
            col.bgra[i] = std::max(col.bgra[i], clamp_min.bgra[i]);
        }

        RENDLOG("FC: %08X", col.raw);
    }

    switch(pp_FogCtrl) {
        // Look up mode 1
        case 0b00:
        // look up mode 2
        case 0b11:
            {
                u8 fog_alpha = LookupFogTable(invW);
                    
                u8 fog_inv = 255^fog_alpha;

                Color col_ram = { FOG_COL_RAM };

                if (pp_FogCtrl == 0b00) {
                    u32 fa = FOG_K(fog_alpha);
                    for (int i = 0; i < 3; i++) {
                        col.bgra[i] = (col.bgra[i] * (256 - fa) + col_ram.bgra[i] * fa) >> 8;
                    }
                } else {
                    for (int i = 0; i < 3; i++) {
                        col.bgra[i] = col_ram.bgra[i];
                    }
                    col.a = fog_alpha;
                }
            }
            break;

        // Per Vertex
        case 0b01:
            if (pp_Offset) {
                Color col_vert = { FOG_COL_VERT };
                u8 alpha = offs_a;
                u8 inv = 255^alpha;
                  
                // (c (256 - a) + f a) >> 8 with the offset colour's alpha as it is: 12242 of 12242 pixels of
                // polly2-data sc_ingame, where the blend unit's weights (255 counts as 256, 128.. as one more) get 5201
                (void)inv;
                for (int i = 0; i < 3; i++)
                {
                    col.bgra[i] = (col.bgra[i] * (256 - alpha) + col_vert.bgra[i] * alpha) >> 8;
                }
            }
            break;

                
        // No Fog
        case 0b10:
            break;
    }

    RENDLOG("FU: %08X", col.raw);
    return col;
}

void DumpTexture(TSP tsp, TCW tcw, TextureFetch_fp fetch) {
    char tex_dump[256];
    int max_mipmap = 0;
    int texU = tsp.TexU + 3;
    int texV = tsp.TexV + 3;
    if (tcw.MipMapped) {
        max_mipmap = texU;
        texV = texU;
    }
    for (int mip=0; mip<=max_mipmap; mip++) {
        int width = (1 << texU) >> mip;
        int height = (1 << texV) >> mip;
        snprintf(tex_dump, sizeof(tex_dump), "%s/texture-%08x-%08x-%d-%dx%d.png", dump_textures, tsp.full, tcw.full, mip, height, width);
        Color* tex = new Color[width * height];

        for (int t = 0; t < height; t++) {
            for (int s = 0; s < width; s++) {
                tex[t * width + s] = fetch(tsp, tcw, s, t, mip);
                std::swap(tex[t * width + s].r, tex[t * width + s].b);
            }   
        }

        stbi_write_png(tex_dump, width, height, 4, tex, width * 4);
        delete[] tex;
    }
}
const char* dump_textures = nullptr;
std::set<u64> texture_dumps;
using BlendingUnit_fp = decltype(&BlendingUnit<0,0,0,0,0>);
using ColorCombiner_fp = decltype(&ColorCombiner<0,0,0>);
using TextureFilter_fp = decltype(&TextureFilter<0,0,0,0,0,0>);
// Implement the full texture/shade pipeline for a pixel

// The offset colour the core keeps (found by purist): a bump map without the offset bit is lit by it. It is the last
// vertex's offset colour, as it was sent, of the last primitive with the offset bit the TSP shaded, in the TSP's
// order (span sorter), across tiles, renders and core resets; about zero after the console was switched on
// (dreamcast-tests/pvr-tests/offspersist: 293 of 299 cells and more on every page; a Gouraud, flat, SupSample or
// bump-map setter all the same; a reader shaded before its tile's setter reads the tile before's). Decal,
// modulate and vertex fog without the offset bit do not read it (page 10).
static Color offsKept = { 0 };
template<bool pp_UseAlpha, bool pp_Texture, bool pp_Offset, bool pp_ColorClamp, u32 pp_FogCtrl, bool pp_CheapShadows>
static bool PixelFlush_tsp(const FpuEntry *entry, PixPos p, const pvrfp::Fp& W, bool InVolume, u32 index, TextureFetch_fp fetch, TextureFilter_fp filter, ColorCombiner_fp combiner, BlendingUnit_fp blending)
{
    u32 two_voume_index = InVolume & !pp_CheapShadows;
    auto cb = (Color*)colorBuffer1 + index;
  
    Color base = { 0 }, textel = { 0 };

    base = InterpolateBase<pp_UseAlpha, pp_CheapShadows>(entry->ips.Col[InVolume ? 1 : 0], p, W, InVolume);

    int dTrilinear = 0;             // of 256: the share of the next level (MipSelect's k)
    u32 MipLevel = 0;
    const bool bump = pp_Texture && entry->params.tcw[two_voume_index].PixelFmt == PixelBumpMap;
    // SupSample: the pixel is made at four points and averaged (below)
    const bool super = entry->params.tsp[two_voume_index].SupSample;
    Color col;
    // A bump map's light (K1 K2 K3 Q, the offset colour) is the last vertex's offset colour as it is: not
    // interpolated on a Gouraud polygon, and not through the 1/w planes on a flat one (pvr-tests/supsample2 page 30
    // and its control: interpolated, 123,578 pixels miss; flat through the planes, 15 and the 6 of pages 29 and 31,
    // next to a corner of large w; pvr-tests/bumplight, strips of four triangles at depths 2^-8 .. 4, with
    // SupSample, fog and an intensity shadow). The planes' fallback is that colour (with an intensity shadow's
    // scaling). Nor is it a fog: vertex fog does nothing to a bump map (bumplight page 5: K1 as the fog's alpha
    // misses every cell).
    auto bumpLight = [&]() {
        Color l;
        for (int i = 0; i < 4; i++)
            l.bgra[i] = (u8)entry->ips.Ofs[InVolume ? 1 : 0][i].fallback;
        return l;
    };
    // the texture environment: the texel t with the base colour b and the offset colour o. A bump map's result takes
    // the texel's place (with modulate, hardware multiplies it by the base color, dreamcast-tests/pvr-tests/bump); the
    // offset color holds the light and is not added.
    auto shade = [&](Color b, Color t, Color o) {
        if (!bump)
            return combiner(b, t, o);
        Color none = { 0 };
        switch (entry->params.tsp[two_voume_index].ShadInstr) {
            case 1: return ColorCombiner<true, false, 1>(b, BumpMapper(t, o), none);
            case 2: return ColorCombiner<true, false, 2>(b, BumpMapper(t, o), none);
            case 3: return ColorCombiner<true, false, 3>(b, BumpMapper(t, o), none);
            default: return ColorCombiner<true, false, 0>(b, BumpMapper(t, o), none);
        }
    };

    // the level and the trilinear coefficient where 1/w is q; `finer`: a SupSample point's, one level finer
    auto mipChoose = [&](const pvrfp::Fp& q, bool finer, u32& level, int& dTri) {
        level = 0;
        dTri = 0;
        if (entry->params.tcw[two_voume_index].MipMapped) {
            // The level and the trilinear coefficient come from the polygon's planes and the pixel's 1/w
            // (pvr_tsp_plane.h, MipSelect; measured with dreamcast-tests/pvr-tests/mipmeas).
            const auto& ips = entry->ips;
            auto choice = pvrtsp::MipSelect(ips.U[two_voume_index].soft, ips.V[two_voume_index].soft, ips.invW.soft,
                                            q, 3 + entry->params.tsp[two_voume_index].TexU,
                                            // D adjust 0 (the manual: "illegal") is 4/4 on a console: Berserk's
                                            // ground (polly2-data berserk_*), dreamcast-tests mipmeas --bias0
                                            entry->params.tsp[two_voume_index].MipMapD ? (int)entry->params.tsp[two_voume_index].MipMapD : 4,
                                            entry->params.isp.DCalcCtrl ? ips.first[two_voume_index] : nullptr);
            if (finer) {
                // half a pixel apart, as if the texels a pixel were halved (pvr-tests/supsample2: every mipmapped
                // page, flat and perspective, every filter); what would be below the largest map is the largest
                // map without blending
                if (choice.level == 0)
                    choice.k = 0;
                else
                    choice.level--;
            }
            // Trilinear pass B samples the next smaller level. Neither goes past the 1x1 level (3 + TexU: a
            // mipmapped texture is square), and the fraction fades both passes all the same: a level past it reads
            // the 1x1 level, and so does pass B at the 1x1 level (dreamcast-tests/pvr-tests/trimin: levels 1x1 - 1
            // .. 20 of 8 x 8 and 64 x 64 textures, fractions 0, 1/2 and the last, passes A, B, both, bilinear,
            // point, VQ, SupSample: 180 of 180 cells; the texels in front of the 1x1 level are never read).
            const u32 last = 3 + entry->params.tsp[two_voume_index].TexU;
            const bool passB = entry->params.tsp[two_voume_index].FilterMode == 3;
            level = (u32)choice.level + (passB ? 1 : 0);
            if (level > last)
                level = last;
            dTri = choice.k;
            if (TcwRaster(entry->params.tcw[two_voume_index]))
                level = 0;       // no levels to choose from; the fraction still fades the trilinear passes
        }
    };
    // Trilinear: pass A (mode 2) draws this level fading out toward the next one, pass B (mode 3) the next level
    // fading in; the two passes are added by the blend unit. The fade is of the finished colour, after the
    // shading, the offset colour, the colour clamp and the fog, alpha too, bump maps too (pvr-tests/supsample2 page
    // 21 and its control: of the texel, as it was, misses 149,148 of 153,600 pixels; pvr-tests/trifade, 22 pages of
    // flat cells). A texture without mipmaps is not faded (pvr-tests/mip, "565 without mipmaps").
    auto fade = [&](Color c, int dTri) {
        if (!pp_Texture || entry->params.tsp[two_voume_index].FilterMode < 2 || !entry->params.tcw[two_voume_index].MipMapped)
            return c;
        int k = dTri;
        if (k < 0) k = 0;
        if (k > 256) k = 256;
        const u32 w = entry->params.tsp[two_voume_index].FilterMode == 2 ? 256 - k : k;
        for (int i = 0; i < 4; i++)
            c.bgra[i] = (c.bgra[i] * w) >> 8;
        return c;
    };

    Color offs = offsKept;      // this pixel's offset colour (without the offset bit: the kept one, for a bump map)
    if (pp_Texture) {
        if (dump_textures) {

            u64 uid = entry->params.tsp[two_voume_index].full;
            uid = (uid << 32) | entry->params.tcw[two_voume_index].full;

            if (texture_dumps.count(uid) == 0) {
                texture_dumps.insert(uid);
                DumpTexture(entry->params.tsp[two_voume_index],  entry->params.tcw[two_voume_index], fetch);
            }
        }
        if (!super) {
            const pvrfp::Fp u = entry->ips.U[two_voume_index].Quot(p, W);
            const pvrfp::Fp v = entry->ips.V[two_voume_index].Quot(p, W);
            mipChoose(W, false, MipLevel, dTrilinear);
            textel = filter(entry->params.tsp[two_voume_index], entry->params.tcw[two_voume_index], u, v, MipLevel, fetch);
        }
        if (pp_Offset) {
            offs = bump ? bumpLight() : InterpolateOffs<pp_CheapShadows>(entry->ips.Ofs[InVolume ? 1 : 0], p, W, InVolume);
            offsKept = bumpLight();
        }
    }

    if (super) {
        // SupSample (dreamcast-tests/pvr-tests/supsample, supsample2, ssmip, sscolor; polly2-data outtrigger_menu,
        // mrdriller_*): the pixel is made at four points, its centre and half a pixel right, down and both. The planes
        // are evaluated for them in half pixel steps (pvr_tsp_plane.h), and each point has everything of its own: its
        // 1/w, coordinates, level and trilinear coefficient (one level finer than the pixel's would be), texel, base
        // and offset colours, shading, fog (with its own 1/w, or its own offset alpha) and trilinear fade. The four
        // colours are then averaged per channel, each row's two and then the rows. (The base and offset colours of
        // the centre: 125k pixels of supsample2's Gouraud pages miss; the fog of the average: every fog page misses;
        // the texels averaged before they are shaded: 56k pixels of supsample page 18.)
        using Stepper = std::remove_reference_t<decltype(entry->ips.invW)>;
        Color c4[4];
        for (int k = 0; k < 4; k++) {
            Stepper::superActive = true;
            Stepper::superHx = k & 1;
            Stepper::superHy = k >> 1;
            const pvrfp::Fp ws = entry->ips.invW.Val(p);
            const pvrfp::Fp us = pp_Texture ? entry->ips.U[two_voume_index].Quot(p, ws) : pvrfp::Fp(), vs = pp_Texture ? entry->ips.V[two_voume_index].Quot(p, ws) : pvrfp::Fp();
            RENDLOG("SS%d: w %a u %a v %a", k, ws.toDouble(), us.toDouble(), vs.toDouble());
            const Color bk = InterpolateBase<pp_UseAlpha, pp_CheapShadows>(entry->ips.Col[InVolume ? 1 : 0], p, ws, InVolume);
            const Color ok = pp_Texture && pp_Offset && !bump ? InterpolateOffs<pp_CheapShadows>(entry->ips.Ofs[InVolume ? 1 : 0], p, ws, InVolume) : offs;
            Stepper::superActive = false;
            Stepper::superHx = 0;
            Stepper::superHy = 0;
            u32 level = 0;
            int dTri = 0;
            Color t = { 0 };
            if (pp_Texture) {
                mipChoose(ws, true, level, dTri);
                t = filter(entry->params.tsp[two_voume_index], entry->params.tcw[two_voume_index], us, vs, level, fetch);
            }
            c4[k] = fade(FogUnit<pp_Offset, pp_ColorClamp, pp_FogCtrl>(shade(bk, t, ok), ws, bump ? 0 : ok.a), dTri);
        }
        for (int c = 0; c < 4; c++)
            col.bgra[c] = (((c4[0].bgra[c] + c4[1].bgra[c]) >> 1) + ((c4[2].bgra[c] + c4[3].bgra[c]) >> 1)) >> 1;
    } else {
        col = shade(base, textel, offs);
        // the fog table is indexed with the TSP's own 1/w (dreamcast-tests/pvr-tests/tspw)
        col = FogUnit<pp_Offset, pp_ColorClamp, pp_FogCtrl>(col, entry->ips.invW.Val(p), bump ? 0 : offs.a);
        col = fade(col, dTrilinear);
    }

    // if (pp_Texture) {
    //     col = MipDebugColor[10-MipLevel];
    // } else {
    //     col = { .raw = 0 };
    // }

    if (tspAlphaStuck)
        col.a = col.a == 255 ? 255 : 0;

	return blending(index, col);
}
using PixelFlush_tsp_fp = decltype(&PixelFlush_tsp<0,0,0,0,0,0>);

#include "gentable.h"

// Lookup/create cached TSP parameters, and call PixelFlush_tsp
bool PixelFlush_tsp(bool pp_AlphaTest, const FpuEntry* entry, PixPos p, u32 index, const pvrfp::Fp& invW, bool InVolume, ISP_BACKGND_T_type core_tag)
{
    u32 two_voume_index = InVolume & !FPU_SHAD_SCALE.intensity_shadow;

    RENDLOG("TSP: %d %f %f %d %f %d %08X %08X %08X %08X", index, p.x + (p.half ? 0.5 : 0.0), p.y + (p.half ? 0.5 : 0.0), InVolume, invW.toDouble(), pp_AlphaTest, entry->params.isp.full, entry->params.tsp[two_voume_index].full, entry->params.tcw[two_voume_index].full, core_tag.full);

    const bool raster = TcwRaster(entry->params.tcw[two_voume_index]);
    auto fetch = TextureFetch_table
        [entry->params.tcw[two_voume_index].VQ_Comp && !raster]
        [entry->params.tcw[two_voume_index].MipMapped && !raster]
        [entry->params.tcw[two_voume_index].ScanOrder]
        [entry->params.tcw[two_voume_index].StrideSel]
        [entry->params.tcw[two_voume_index].PixelFmt];

    auto filter = TextureFilter_table
        [entry->params.tsp[two_voume_index].IgnoreTexA]
        [entry->params.tsp[two_voume_index].ClampU]
        [entry->params.tsp[two_voume_index].ClampV]
        [entry->params.tsp[two_voume_index].FlipU]
        [entry->params.tsp[two_voume_index].FlipV]
        // a trilinear pass on a texture without mipmaps samples a single texel, at full strength (polly2-data
        // shenmue_intro: a 4444 texture with alpha, every pixel's alpha one texel's; a flat texture showed nothing)
        [entry->params.tsp[two_voume_index].FilterMode >= 2 && !entry->params.tcw[two_voume_index].MipMapped ? 0
                                                                                                  : entry->params.tsp[two_voume_index].FilterMode];

    auto combiner = ColorCombiner_table
        [entry->params.isp.Texture]
        [entry->params.isp.Offset]
        [entry->params.tsp[two_voume_index].ShadInstr];

    auto blending = BlendingUnit_table
        [entry->params.tsp[two_voume_index].SrcSelect]
        [entry->params.tsp[two_voume_index].DstSelect]
        [entry->params.tsp[two_voume_index].SrcInstr]
        [entry->params.tsp[two_voume_index].DstInstr]
        [pp_AlphaTest];
    
    auto pixel = PixelFlush_tsp_table
        [entry->params.tsp[two_voume_index].UseAlpha]
        [entry->params.isp.Texture]
        [entry->params.isp.Offset]
        [entry->params.tsp[two_voume_index].ColorClamp]
        [entry->params.tsp[two_voume_index].FogCtrl]
        [FPU_SHAD_SCALE.intensity_shadow];

    const bool rv = pixel(entry, p, invW, InVolume, index, fetch, filter, combiner, blending);

    const auto& tsp = entry->params.tsp[two_voume_index];
    if (tsp.SrcSelect && tsp.DstSelect && tsp.DstInstr == 0 && (tsp.SrcInstr == 1 || tsp.SrcInstr == 4))
        tspAlphaStuck = true;       // see tspAlphaStuck

    return rv;
}

}   // namespace refcore
