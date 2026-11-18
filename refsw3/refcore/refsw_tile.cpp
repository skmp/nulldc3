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
#include <float.h>
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

static float mmin(float a, float b, float c, float d)
{
    float rv = std::min(a, b);
    rv = std::min(c, rv);
    return std::max(d, rv);
}

static float mmax(float a, float b, float c, float d)
{
    float rv = std::max(a, b);
    rv = std::max(c, rv);
    return std::min(d, rv);
}


// Z buffer doesn't store sign, and has 19 bits of m
f32  mask_w(f32 w) {
    // u32 wu = (u32&)w;
    // wu = wu & 0x7FFFFFF8;
    // return (f32&)wu;
    return w;
}

void ClearBuffers(u32 paramValue, float depthValue, u32 stencilValue)
{
    auto zb = depthBuffer[depthBufferA];
    auto stencil = stencilBuffer;
    auto pb = tagBuffer[tagBufferA];;

    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        zb[i] = mask_w(depthValue);
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

// In a tile whose first punch-through pass draws something, the opaque polygons' runs do not end at volume edges,
// and the opaque pixels inside a volume are shaded again in the punch-through volume pass, in its runs with the
// punch-through pixels there (dreamcast-tests/pvr-tests/optrun kinds 9, 10; volrun; polly2-data jgr_car,
// jgr_tree_shadows*). Punch-through pixels in front of an opaque polygon do not end its runs (optrun kinds 1-8), and
// a punch-through list that draws nothing in the tile changes nothing (polly2-data sa2_ingame, 18wheel_select).
bool OpInPt = false;                                    // set per tile by refsw_lists.cpp
static parameter_tag_t opTag[MAX_RENDER_PIXELS];        // the opaque tag of each pixel
static parameter_tag_t refTagC[MAX_RENDER_PIXELS];      // the tag of depthBufferC's layer: opaque (or background), or resolved
// the look at the first punch-through pass (refsw_lists.cpp) leaves the tile as it found it
static ZType snapDepth[MAX_RENDER_PIXELS];
static parameter_tag_t snapTag[MAX_RENDER_PIXELS];
static TagState snapStatus[MAX_RENDER_PIXELS];
static StencilType snapStencil[MAX_RENDER_PIXELS];
void SnapshotTile(bool save) {
    if (save) {
        memcpy(snapDepth, depthBuffer[depthBufferA], sizeof(snapDepth)); memcpy(snapTag, tagBuffer[tagBufferA], sizeof(snapTag));
        memcpy(snapStatus, tagStatus, sizeof(snapStatus)); memcpy(snapStencil, stencilBuffer, sizeof(snapStencil));
    } else {
        memcpy(depthBuffer[depthBufferA], snapDepth, sizeof(snapDepth)); memcpy(tagBuffer[tagBufferA], snapTag, sizeof(snapTag));
        memcpy(tagStatus, snapStatus, sizeof(snapStatus)); memcpy(stencilBuffer, snapStencil, sizeof(snapStencil));
    }
}

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
void SaveDepthAt(u32 index, float invW) { savedDepth[index] = invW; }
void RestoreDepth() { memcpy(depthBuffer[depthBufferA], savedDepth, sizeof(savedDepth)); }

void SetTagToMax()
{
    memset(tagBuffer[tagBufferA], 0xFF, sizeof(tagBuffer[tagBufferA]));
}
void PeelBuffers(float depthValue, u32 stencilValue)
{
    memcpy(depthBuffer[depthBufferB], depthBuffer[depthBufferA], sizeof(ZType) * MAX_RENDER_PIXELS);
    memcpy(tagBuffer[tagBufferB], tagBuffer[tagBufferA], sizeof(parameter_tag_t) * MAX_RENDER_PIXELS);


    auto zb = depthBuffer[depthBufferA];
    auto stencil = stencilBuffer;

    for (int i = 0; i < MAX_RENDER_PIXELS; i++) {
        zb[i] = mask_w(depthValue);    // set the "closest" test to furthest value possible
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

    // Render to ACCUM from TAG buffer
// TAG holds references to trianes, ACCUM is the tile framebuffer
template<RenderMode rm>
void RenderParamTags(int tileX, int tileY) {
    float halfpixel = HALF_OFFSET.tsp_pixel_half_offset ? 0.5f : 0;
    taRECT rect;
    rect.left = tileX;
    rect.top = tileY;
    rect.bottom = rect.top + 32;
    rect.right = rect.left + 32;

    for (int y = 0; y < 32; y++) {
        // The TSP evaluates its planes once per run of pixels of one triangle on a line and steps from there
        // (dreamcast-tests/pvr-tests/uvspan): PlaneStepper3 needs the run's first column.
        bool runOpen = false;
        bool runVolume = false;
        int runStart = 0;
        auto runTag = tagBuffer[tagBufferA][0];
        for (int x = 0; x < 32; x++) {
            auto index = y * 32 + x;
            auto tag =  tagBuffer[tagBufferA][index];
            bool TagValid = tagStatus[index].valid;
            ISP_BACKGND_T_type t { .full = tag };
            // inside a modifier volume, whatever the polygon's shadow bit; the volume's parameters (or its intensity)
            // only for a polygon with the bit
            const bool volHit = (stencilBuffer[index] & 0b001) == 0b001;
            bool InVolume = volHit && t.shadow;

            // The punch-through pixels inside a volume are shaded again after the passes, all of them: a polygon
            // without the shadow bit with its own parameters, in runs over the pixels inside (dreamcast-tests/pvr-tests/
            // uvrun kind 7; polly2-data jgr_tree_shadows*); the opaque ones there too (OpInPt), not alpha tested
            if (rm == RM_PUNCHTHROUGH_MV) {
                if (!volHit) {
                    runOpen = false;
                    continue;
                } else {
                    TagValid = tagStatus[index].rendered || OpInPt;
                }
            }

            if (rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN) {
                InVolume = false;
            }

            // A run ends where the volume state changes, shadow bit or not: opaque (not in a tile with punch-through
            // drawn: see OpInPt), sorted translucent and the punch-through volume pass (uvrun kinds 6, 9, 7; volrun:
            // edges made by the depth test too). Not translucent polygons in the order sent, which an
            // opaque volume's shadow reaches but whose runs it does not cut (uvrun kinds 10, 11; polly2-data
            // sa2_ingame2, taxi3, xtreme_intro, jgr_tree_shadows*).
            const bool runVol = rm == RM_TRANSLUCENT_PRESORT || rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN || (rm == RM_OPAQUE && OpInPt) ? false : volHit;
            if (!TagValid) {
                runOpen = false;
            } else if (!runOpen || tag != runTag || runVol != runVolume) {
                runOpen = true;
                runTag = tag;
                runVolume = runVol;
                runStart = x;
            }
            PlaneStepper3::SpanStart = runStart;

            if (TagValid) {
                tilePixelsShaded++;
                const auto& Entry = GetFpuEntry(&rect, rm, t);
                auto invW = Entry.ips.invW.Ip(x + halfpixel, y + halfpixel);
                // Punch through shaded again inside a modifier volume is alpha tested too: its colour replaces the
                // pixel's, not blended with its alpha (dreamcast-tests/pvr-tests/dumprules, cell 22)
                // a pixel resolved in the pass before: shaded for the run, keeps its colour, goes on below
                const bool keepRes = (rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN) && tagStatus[index].rendered;
                const u32 keep1 = colorBuffer1[index], keep2 = colorBuffer2[index];
                bool AlphaTestPassed = PixelFlush_tsp(rm == RM_PUNCHTHROUGH_PASS0 || rm == RM_PUNCHTHROUGH_PASSN || (rm == RM_PUNCHTHROUGH_MV && tagStatus[index].rendered), &Entry, x + halfpixel, y + halfpixel, index, invW, InVolume, t);
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
    cv->x=vrf(emu_vram, ptr);ptr+=4;
    cv->y=vrf(emu_vram, ptr);ptr+=4;
    cv->z=vrf(emu_vram, ptr);ptr+=4;

    if (params->isp.Texture)
    {	//Do texture , if any
        if (params->isp.UV_16b)
        {
            u32 uv=vri(emu_vram, ptr);
            cv->u = f16((u16)(uv >>16));
            cv->v = f16((u16)(uv >> 0));
            ptr+=4;
        }
        else
        {
            cv->u=vrf(emu_vram, ptr);ptr+=4;
            cv->v=vrf(emu_vram, ptr);ptr+=4;
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
                cv->u1 = f16((u16)(uv >>16));
                cv->v1 = f16((u16)(uv >> 0));
                ptr+=4;
            }
            else
            {
                cv->u1=vrf(emu_vram, ptr);ptr+=4;
                cv->v1=vrf(emu_vram, ptr);ptr+=4;
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

// this is disabled for now, as it breaks game scenes
ALWAYS_INLINE bool IsTopLeft(float x, float y) {
    bool IsTop = y == 0 && x > 0;
    bool IsLeft = y < 0;

    return IsTop || IsLeft;
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
static ALWAYS_INLINE bool KeyAbove(float aW, parameter_tag_t aTag, float bW, parameter_tag_t bTag)
{
    if (aW != bW)
        return aW > bW;
    if (bTag == TAG_BELOW)
        return aTag != TAG_BELOW;
    return aTag != TAG_BELOW && (aTag & PARAMETER_TAG_SORT_MASK) > (bTag & PARAMETER_TAG_SORT_MASK);
}

// Depth processing for a pixel -- render_mode 0: OPAQ, 1: PT, 2: TRANS
template<RenderMode render_mode>
ALWAYS_INLINE void PixelFlush_isp(u32 depth_mode, u32 ZWriteDis, float x, float y, float invW, u32 index, parameter_tag_t tag)
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

    RENDLOG("ISP: %d %d %d %f %f %f %08X", index, render_mode, mode, x, y, invW, tag);
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
                *zb = mask_w(invW);
            }
            *pb = tag;
            ts->valid = 1;
            RENDLOG("RENDERED: %f", *zb);
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
            *zb = mask_w(invW);
            *pb = tag;
            ts->valid = 1;
            RENDLOG("RENDERED");
        }
        break;

        // Layer Peeling. zb2 holds the reference depth, zb is used to find closest to reference
        case RM_TRANSLUCENT_PRESORT:
        {
            if (!ZWriteDis) {
                *zb = mask_w(invW);
            }
            *pb = tag;
            ts->valid = true;
            RENDLOG("RENDERED: %f", *zb);
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
            *zb = mask_w(invW);
            *pb = tag;
            ts->valid = true;
            RENDLOG("RENDERED");
        }
        break;

        case RM_PUNCHTHROUGH_MV: die("this is invalid here"); break;
    }
}

// Rasterize a single triangle to ISP (or ISP+TSP for PT)
template<RenderMode render_mode>
void RasterizeTriangle(DrawParameters* params, parameter_tag_t tag, const Vertex& v1, const Vertex& v2, const Vertex& v3, const Vertex* v4, taRECT* area)
{
    const int stride_bytes = STRIDE_PIXEL_OFFSET * 4;
    //Plane equation

#define FLUSH_NAN(a) std::isnan(a) ? 0 : a

    const float Y1 = FLUSH_NAN(v1.y);
    const float Y2 = FLUSH_NAN(v2.y);
    const float Y3 = FLUSH_NAN(v3.y);
    const float Y4 = v4 ? FLUSH_NAN(v4->y) : 0;

    // The winding, the area for culling and the inside test are computed in the setup units' own arithmetic
    // (pvr_isp_edge.h on pvr_softfp.h; every rule measured on a console with dreamcast-tests/pvr-tests/covmeas and
    // dreamcast-tests/pvr-tests/hugepos). The same code is the model the probes of dreamcast-tests/model run.
    static const pvrisp::EdgeConfig edgeConfig;
    using pvrfp::Fp;
    const Fp FX[4] = { Fp::FromFloat(v1.x), Fp::FromFloat(v2.x), Fp::FromFloat(v3.x), v4 ? Fp::FromFloat(v4->x) : Fp() };
    const Fp FY[4] = { Fp::FromFloat(v1.y), Fp::FromFloat(v2.y), Fp::FromFloat(v3.y), v4 ? Fp::FromFloat(v4->y) : Fp() };
    bool badCorner = false;
    Fp det;
    // -1 when the determinant is positive or zero (a zero counts as positive: covmeas, mix sliver)
    const int sgn = pvrisp::Winding(FX, FY, edgeConfig, &badCorner, &det);
    // an infinity or a NaN among the corners: a console draws nothing (dreamcast-tests/pvr-tests/huge)
    if (badCorner || FX[3].bad || FY[3].bad)
        return;
    // twice the area as the setup has it; corners at 2^100 are fine here (dreamcast-tests/pvr-tests/huge,
    // dreamcast-tests/pvr-tests/hugepos)
    const double tri_area = det.toDouble();

    // cull
    if (params->isp.CullMode != 0) {
        //area: (X1-X3)*(Y2-Y3)-(Y1-Y3)*(X2-X3)

        double abs_area = fabs(tri_area);

        // hardware also drops a triangle whose area value equals the threshold (dreamcast-tests/pvr-tests/raster page
        // 4: legs of 1 pixel with the threshold at 1.0)
        if (abs_area <= FPU_CULL_VAL)
            return;

        if (params->isp.CullMode >= 2) {
            u32 mode = params->isp.CullMode & 1;

            if (
                (mode == 0 && tri_area < 0) ||
                (mode == 1 && tri_area > 0)) {
                RENDLOG("CULLED");
                return;
            }
        }
    }

    // Bounding rectangle
//  int minx = mmin(X1, X2, X3, area->left);
//  int miny = mmin(Y1, Y2, Y3, area->top);

//  int spanx = mmax(X1+1, X2+1, X3+1, area->right - 1) - minx + 1;
//  int spany = mmax(Y1+1, Y2+1, Y3+1, area->bottom - 1) - miny + 1;

    //Inside scissor area?
//  if (spanx < 0 || spany < 0)
//      return;

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
    static const pvrisp::DepthConfig depthConfig;
    pvrisp::DepthTile Z;
    {
        const float zx[3] = { v1.x, v2.x, v3.x }, zy[3] = { v1.y, v2.y, v3.y }, zz[3] = { v1.z, v2.z, v3.z };
        Z.Setup(zx, zy, zz, area->left, area->top, depthConfig);
    }
    const bool sampleCenters = HALF_OFFSET.fpu_pixel_half_offset;

    float halfpixel = HALF_OFFSET.fpu_pixel_half_offset ? 0.5f : 0;
    float y_ps    = halfpixel;
    float minx_ps = halfpixel;

    // The rows a polygon is rasterized on: from the row its top corner is in to the row of its bottom corner's
    // y + 0.5, whatever the sample offset. Ordinary polygons never notice; the truncated edge functions of a
    // sliver accept pixels beyond its ends, and a console cuts those rows off. Modifier volume triangles have
    // no such limit (dreamcast-tests/pvr-tests/covmeas, mix sliver).
    double rowFirst = -INFINITY, rowLast = INFINITY;
    if (render_mode != RM_MODIFIER) {
        double ymin = std::min(std::min((double)Y1, (double)Y2), (double)Y3), ymax = std::max(std::max((double)Y1, (double)Y2), (double)Y3);
        if (v4) {
            ymin = std::min(ymin, (double)Y4);
            ymax = std::max(ymax, (double)Y4);
        }
        rowFirst = floor(ymin);
        rowLast = floor(ymax + 0.5);
    }

    // Loop through ALL pixels in the tile (no smart clipping)
	for (int y = 0; y < 32; y++)
    {
        float x_ps = minx_ps;
        if (area->top + y < rowFirst || area->top + y > rowLast)
            goto next_y;
        if (E12.RowOutside(y) || E23.RowOutside(y) || E31.RowOutside(y) || (v4 && E41.RowOutside(y)))
            goto next_y;

        for (int x = 0; x < 32; x++)
        {
            bool inTriangle = E12.Inside(x, y) && E23.Inside(x, y) && E31.Inside(x, y) && (!v4 || E41.Inside(x, y));

            if (inTriangle) {
                u32 index = y * 32 + x;
                float invW = Z.At(x, y, sampleCenters);
                // A pixel whose depth is below zero is drawn like any other (dreamcast-tests/pvr-tests/uvmeas, mix
                // wneg: 780,000 texels read off such polygons). An untextured one comes out black, which
                // dreamcast-tests/pvr-tests/huge took for "not drawn": the TSP's quotient has the sign of colour / w
                // alone (pvrtsp::DenominatorSeen), so the colour is below zero.
                PixelFlush_isp<render_mode>(params->isp.DepthMode, params->isp.ZWriteDis, x_ps, y_ps, invW, index, tag);
            }

            x_ps = x_ps + 1;
        }
    next_y:
        y_ps = y_ps + 1;
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

u64 VQLookup(u32 start_address, u64 memtel, u32 offset) {
    u8* memtel8 = (u8*)&memtel;

    u64 *vq_book = (u64*)&emu_vram[start_address & (VRAM_MASK-7)];
    u8 index = memtel8[offset & 7];
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

    u64 memtel = (u64&)emu_vram[(base_address + offset * fbpp / 16) & (VRAM_MASK-7)];

    if (VQ_Comp) {
        memtel = VQLookup(start_address, memtel, offset * fbpp / 16);
    }

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

// A texture coordinate in 1/256 texel as hardware sees it. Hardware's coordinates come out a hair toward
// zero: a coordinate that is exactly on a boundary belongs to the texel before it, except at 0
// (dreamcast-tests/pvr-tests/sample: point sampling with half-pixel shifts, bilinear with the weights of page 2).
// Values within 1/32 of a unit of an integer are treated as being exactly on it.
ALWAYS_INLINE int TexCoordFixed(float x) {
    // The coordinate the TSP delivers is already rounded down (see PlaneStepper3): with 1/w = 1 it arrives one
    // unit of its 16th bit under the nominal value, which is what makes a coordinate exactly on a texel
    // boundary show the texel before it (dreamcast-tests/pvr-tests/sample, dreamcast-tests/pvr-tests/uvmeas).
    // Rounded down in two's complement, below zero too: dreamcast-tests/pvr-tests/coordfix (every weight step from
    // -4 to +4 texels and at up to 2^17 texels, clamp and flip; truncating toward zero gets 437 of its 1568 regions
    // wrong, weights from the magnitude 489).
    double f = floor((double)x);
    // far outside an int (coordinates of thousands of texture repeats): keep the sign and the low 26 bits,
    // which is all that wrapping, flipping and clamping look at
    if (f >= 1073741824.0)
        f = 1073741824.0 + fmod(f, 67108864.0);
    else if (f < -1073741824.0)
        f = -1073741824.0 + fmod(fmod(f, 67108864.0) + 67108864.0, 67108864.0);
    if (!(f == f))
        f = 0;                  // not a number, or too large to wrap (infinity): texel 0
    return (int)f;
}

// Fetch pixels from UVs, interpolate
template<bool pp_IgnoreTexA,  bool pp_ClampU, bool pp_ClampV, bool pp_FlipU, bool pp_FlipV, u32 pp_FilterMode>
static Color TextureFilter(TSP tsp, TCW tcw, float u, float v, u32 MipLevel, f32 dTrilinear, TextureFetch_fp fetch) {
        
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

    int ui = TexCoordFixed(u * sizeU * 256) + halfpixel;
    int vi = TexCoordFixed(v * sizeV * 256) + halfpixel;
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
        int v0 = TexCoordFixed(v * (8 << tsp.TexV) * 256) + halfpixel;
        v0 = d > 0 ? v0 * (1 << d) : v0 >> -d;
        ui = (TexCoordFixed(u * (8 << tsp.TexU) * 256) + halfpixel) >> MipLevel;
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
        // (dreamcast-tests/pvr-tests/mip, "565 without mipmaps").
        if (pp_FilterMode >= 2 && tcw.MipMapped) {
            // Trilinear: pass A (mode 2) draws this level fading out toward the next one, pass B (mode 3) is
            // called with the next level and fades it in; the two passes are added by the blend unit.
            // dTrilinear is the next level's coefficient, of 256 (pvrtsp::MipSelect).
            int k = (int)(dTrilinear * 256);
            if (k < 0) k = 0;
            if (k > 256) k = 256;
            u32 w = pp_FilterMode == 2 ? 256 - k : k;
            for (int i = 0; i < 4; i++)
                textel.bgra[i] = (textel.bgra[i] * w) >> 8;
        }
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
#define COLOR_BIAS 0.5f

// Interpolate the base color, also cheap shadows modifier
template<bool pp_UseAlpha, bool pp_CheapShadows>
ALWAYS_INLINE Color InterpolateBase(const PlaneStepper3* Col, float x, float y, float W, bool InVolume) {
    Color rv;
    u32 mult = 256;

    // intensity shadows: Col is then the plane set of the scaled vertex colours (IPs3::SetupIntensity); per vertex
    // (c * intensity) >> 8, so 0x80 halves exactly and 0xff still darkens by one level (dreamcast-tests/pvr-tests/modvol)

    // the intensity scales the colour before it is clamped: a colour far above 255 (a quotient of extreme
    // depths) stays white at 0x80 and 0xff, and only 0 makes it black (dreamcast-tests/pvr-tests/modvol, pages 0 to 2)
    for (int i = 0; i < 4; i++) {
        u32 c = (u32)(COLOR_BIAS + Col[i].IpWide(x, y, W));
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
ALWAYS_INLINE Color InterpolateOffs(const PlaneStepper3* Ofs, float x, float y, float W, bool InVolume) {
    Color rv;
    u32 mult = 256;

    for (int i = 0; i < 3; i++) {
        u32 c = (u32)(COLOR_BIAS + Ofs[i].IpU8(x, y, W));
        rv.bgra[i] = (c * mult) >> 8;
    }
    rv.bgra[3] = COLOR_BIAS + Ofs[3].IpU8(x, y, W);

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

ALWAYS_INLINE u8 LookupFogTable(float invW) {
    u8* fog_density=(u8*)&FOG_DENSITY;
    float fog_den_mant=fog_density[1]/128.0f;  //bit 7 -> x. bit, so [6:0] -> fraction -> /128
    s32 fog_den_exp=(s8)fog_density[0];
        
    float fog_den = fog_den_mant*powf(2.0f,fog_den_exp);

    f32 fogW = fog_den * invW;

    fogW = std::max((float)fogW, 1.0f);
    fogW = std::min((float)fogW, 255.999985f);

    union f32_fields {
        f32 full;
        struct {
            u32 m: 23;
            u32 e: 8;
            u32 s: 1;
        };
    };

    f32_fields fog_fields = { fogW };

    u32 index = (((fog_fields.e +1) & 7) << 4) |  ((fog_fields.m>>19) & 15);

    u8 blendFactor = (fog_fields.m>>11) & 255;
    u8 blend_inv = 255^blendFactor;

    OFFLINE_READ(FOG_TABLE_START_addr + index * 4);
    auto fog_entry = (u8*)&FOG_TABLE[index];

    u32 fk = FOG_K(blendFactor);
    u8 fog_alpha = (fog_entry[0] * fk + fog_entry[1] * (256 - fk)) >> 8;

    return fog_alpha;
}

// Color Clamp and Fog a pixel
template<bool pp_Offset, bool pp_ColorClamp, u32 pp_FogCtrl>
ALWAYS_INLINE Color FogUnit(Color col, float invW, u8 offs_a) {
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

static Color offs = { 0x20004080 }; // this one persists across invocations, as tested via bump maps. Default value was randomly chosen.
template<bool pp_UseAlpha, bool pp_Texture, bool pp_Offset, bool pp_ColorClamp, u32 pp_FogCtrl, bool pp_CheapShadows>
static bool PixelFlush_tsp(const FpuEntry *entry, float x, float y, float W, bool InVolume, u32 index, TextureFetch_fp fetch, TextureFilter_fp filter, ColorCombiner_fp combiner, BlendingUnit_fp blending)
{
    u32 two_voume_index = InVolume & !pp_CheapShadows;
    auto cb = (Color*)colorBuffer1 + index;
  
    Color base = { 0 }, textel = { 0 };

    base = InterpolateBase<pp_UseAlpha, pp_CheapShadows>(entry->ips.Col[InVolume ? 1 : 0], x, y, W, InVolume);

    float dTrilinear;
    u32 MipLevel;
    if (pp_Texture) {
        if (dump_textures) {

            u64 uid = entry->params.tsp[two_voume_index].full;
            uid = (uid << 32) | entry->params.tcw[two_voume_index].full;

            if (texture_dumps.count(uid) == 0) {
                texture_dumps.insert(uid);
                DumpTexture(entry->params.tsp[two_voume_index],  entry->params.tcw[two_voume_index], fetch);
            }
        }
        float u = entry->ips.U[two_voume_index].Ip(x, y, W);
        float v = entry->ips.V[two_voume_index].Ip(x, y, W);

        if (entry->params.tcw[two_voume_index].MipMapped) {
            // The level and the trilinear coefficient come from the polygon's planes and the pixel's 1/w
            // (pvr_tsp_plane.h, MipSelect; measured with dreamcast-tests/pvr-tests/mipmeas).
            const auto& ips = entry->ips;
            auto choice = pvrtsp::MipSelect(ips.U[two_voume_index].soft, ips.V[two_voume_index].soft, ips.invW.soft,
                                            pvrfp::Fp::FromFloat(W), 3 + entry->params.tsp[two_voume_index].TexU,
                                            entry->params.tsp[two_voume_index].MipMapD,
                                            entry->params.isp.DCalcCtrl ? ips.first[two_voume_index] : nullptr);
            MipLevel = choice.level > 11 ? 11 : choice.level;
            dTrilinear = choice.k / 256.0f;
            if (TcwRaster(entry->params.tcw[two_voume_index]))
                MipLevel = 0;       // no levels to choose from; the fraction still fades the trilinear passes
        } else {
            dTrilinear = 0;
            MipLevel = 0;
        }
        
        // trilinear pass B samples the next smaller level
        if (entry->params.tsp[two_voume_index].FilterMode == 3 && entry->params.tcw[two_voume_index].MipMapped
            && !TcwRaster(entry->params.tcw[two_voume_index]))
            MipLevel++;
        textel = filter(entry->params.tsp[two_voume_index], entry->params.tcw[two_voume_index], u, v, MipLevel, dTrilinear, fetch);
        if (pp_Offset) {
            offs = InterpolateOffs<pp_CheapShadows>(entry->ips.Ofs[InVolume ? 1 : 0], x, y, W, InVolume);
        }
    }

    Color col;
    if (pp_Texture && entry->params.tcw[two_voume_index].PixelFmt == PixelBumpMap) {
        // The bump result takes the texel's place in the texture environment (with modulate, hardware
        // multiplies it by the base color, dreamcast-tests/pvr-tests/bump); the offset color holds the light and is not
        // added.
        Color none = { 0 };
        col = ColorCombiner<true, false, 0>(base, BumpMapper(textel, offs), none);
        switch (entry->params.tsp[two_voume_index].ShadInstr) {
            case 1: col = ColorCombiner<true, false, 1>(base, BumpMapper(textel, offs), none); break;
            case 2: col = ColorCombiner<true, false, 2>(base, BumpMapper(textel, offs), none); break;
            case 3: col = ColorCombiner<true, false, 3>(base, BumpMapper(textel, offs), none); break;
        }
    } else {
        col = combiner(base, textel, offs);
    }
        
    // the fog table is indexed with the TSP's own 1/w (dreamcast-tests/pvr-tests/tspw)
    col = FogUnit<pp_Offset, pp_ColorClamp, pp_FogCtrl>(col, entry->ips.invW.Ip(x, y), offs.a);

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
bool PixelFlush_tsp(bool pp_AlphaTest, const FpuEntry* entry, float x, float y, u32 index, float invW, bool InVolume, ISP_BACKGND_T_type core_tag)
{
    u32 two_voume_index = InVolume & !FPU_SHAD_SCALE.intensity_shadow;

    RENDLOG("TSP: %d %f %f %d %f %d %08X %08X %08X %08X", index, x, y, InVolume, invW, pp_AlphaTest, entry->params.isp.full, entry->params.tsp[two_voume_index].full, entry->params.tcw[two_voume_index].full, core_tag.full);

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

    const bool rv = pixel(entry, x, y, invW, InVolume, index, fetch, filter, combiner, blending);

    const auto& tsp = entry->params.tsp[two_voume_index];
    if (tsp.SrcSelect && tsp.DstSelect && tsp.DstInstr == 0 && (tsp.SrcInstr == 1 || tsp.SrcInstr == 4))
        tspAlphaStuck = true;       // see tspAlphaStuck

    return rv;
}

}   // namespace refcore
