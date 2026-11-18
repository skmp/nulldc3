/*
	This file is part of libswirl
*/
//#include "license/bsd"

/*
    REFSW: Reference-style software rasterizer

    An attempt to model CLX2's CORE/SPG/RAMDAC at the lowest functional level

    Rasterizer structure
    ===

    Reads tile lists in CORE format, generated from a LLE TA implementation or software running from sh4,
    renders them in 32x32 tiles, reads out to VRAM and displays framebuffer from VRAM.

    CORE high level overview
    ===

    CORE Renders based on the REGION ARRAY, which is a flag-terminated list of tiles. Each RegionArrayEntry
    contains the TILE x/y position, control flags for Z clear/Write out/Presort and pointers to OBJECT LISTS.

    OBJECT LISTS are inline linked lists containing ObjectListEntries. Each ObjectListEntry has a small
    descriptor for the entry type and vertex size, and a pointer to the OBJECT DATA.

    OBJECT DATA contains the PARAMETERS for the OBJECT (ISP, TSP, TCW, optional TSP2 and TCW2) and vertixes.

    There are 3 OBJECT DATA TYPES
    - Triangle Strips (PARAMETERS, up to 8 VTXs) x 1
    - Triangle Arrays (PARAMETERS, 3 vtx) x Num_of_primitives
    - Quad Arrays (PARAMETERS, 4 vtx) x Num_of_primitives

    CORE renders the OBJECTS to its internal TILE BUFFERS, scales and filters the output (SCL)
    and writes out to VRAM.

    CORE Rendering details
    ===

    CORE has four main components, FPU (triangle setup) ISP (Rasterization, depth, stencil), TSP (Texutre + Shading)
    and SCL (tile writeout + scaling). There are three color rendering modes: DEPTH FIRST, DEPTH + COLOR and LAYER PEELING.

    OPAQUE OBJECTS are rendered using the DEPTH FIRST mode.
    PUNCH THROUGH OBJECTS are rendered using the DEPTH + COLOR mode.
    TRANSPARENT OBJECTS are rendered using either the DEPTH + COLOR mode or the LAYER PEELING mode.
    
    DEPTH FIRST mode
    ---
    OBJECTS are first rendered by ISP in the depth and tag buffers, 32 pixels (?) at a time. then the SPAN SORTER collects spans with the
    same tag and sends them to TSP for shading processing, one pixel at a time.

    DEPTH + COLOR mode
    ---
    OBJECTS are rendered by ISP and TSP at the same time, one pixel (?) at a time. ALPHA TEST feedback from TSP modifies the Z-write behavior.

    LAYER PEELING mode
    ---

    OBJECTS are first rendered by ISP in the depth and tag buffers, using a depth pass and a depth test buffer. SPAN SORTER collects spans with
    the same tag and sends them to TSP for shading processing. The process repeats itself until all layers have been indepedently rendered. On
    each pass, only the pixels with the lowest depth value that pass the depth pass buffer are rendered. In case of identical depth values, the
    tag buffer is used to sort the pixels by tag as well as depth in order to support co-planar polygons.
*/


#include "hw/pvr/pvr_mem.h"
#include "TexUtils.h"

#include <cmath>
#include <float.h>

#include <memory>
#include <cstdio>
#include <cstring>
#include <algorithm>

#include "hw/pvr/pvr_regs.h"
#include <vector>
#include <algorithm>
#include <cstring>

// #include <png.h>

#include "refsw_lists.h"

#include "refsw_tile.h"

// refsw3's core (refcore): in a namespace of its own, beside rend/refsw2, which has the same names
namespace refcore {


extern u8* emu_vram;
FILE* rendlog;
// refsw3-offline's `log <file> x,y ...`: only these tiles' lines (x | y << 8); empty: every tile
std::vector<u32> rendlog_tiles;

/*
    Main renderer class
*/
// The punch-through ISP takes the tile's punch-through polygons in chunks, in list order, and peels each chunk on its
// own, in front of what the chunks before it resolved (the hardware manual, 3.4.3.1 "ISP Cache Size"). A primitive
// goes into the chunk if the vertices stored before it (a triangle 3, a quad 4, but the last vertex of a quad right
// before it not yet counted) and its own come to at most ISP_FEED_CFG's "Punch Through chunk size" less 2: 20
// triangles at the games' 0x40, 10 at 0x20, 11 from 0x23. The triangles of a strip count one by one, and a strip's
// entry is cut between chunks (dreamcast-tests/pvr-tests/ptchunk, ptchunkq: 210 configurations of triangles and
// sprites at 0x20-0x25 and 0x40; polly2-data shenmue_intro*, jgr_tree_shadows2). Whether a quad itself at the
// boundary counts 4 or 3 there is not measured (the readout cannot tell).
static bool ptChunking = false;
static int ptChunk, ptChunkSize, ptChunkAt, ptChunkFill, ptChunks;
static bool ptChunkPrevQuad = false;
static bool PtChunkTake(int nv) {       // the next nv vertices of the list: in the chunk being peeled?
    if (ptChunkFill > 0 && (ptChunkFill - (ptChunkPrevQuad ? 1 : 0)) + nv > ptChunkSize - 2) {
        ptChunkAt++;
        ptChunkFill = 0;
    }
    ptChunkFill += nv;
    ptChunkPrevQuad = nv == 4;
    ptChunks = ptChunkAt + 1;
    return ptChunkAt == ptChunk;
}

void RenderTriangle(RenderMode render_mode, DrawParameters* params, parameter_tag_t tag, const Vertex& v1, const Vertex& v2, const Vertex& v3, const Vertex* v4, taRECT* area)
{   
    if (ptChunking && (render_mode == RM_PUNCHTHROUGH_PASS0 || render_mode == RM_PUNCHTHROUGH_PASSN)) {
        if (!PtChunkTake(v4 ? 4 : 3))
            return;
    }
    RENDLOG("TRIA: %08X %08X %08X %08X %a %a %a %a %a  %a %a %a %a %a  %a %a %a %a %a", tag, params->isp.full, params->tsp[0].full, params->tcw[0].full,   // the corners exactly (dreamcast-tests: dumptri, uvmeas/zmeas game triangles)
        v1.x, v1.y, v1.z, v1.u, v1.v, v2.x, v2.y, v2.z, v2.u, v2.v, v3.x, v3.y, v3.z, v3.u, v3.v);
    RENDLOG("TRIC: %08X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X", tag,   // the corners' colours, ARGB (dumptri)
        v1.col[3], v1.col[2], v1.col[1], v1.col[0], v2.col[3], v2.col[2], v2.col[1], v2.col[0], v3.col[3], v3.col[2], v3.col[1], v3.col[0]);
    RasterizeTriangle_table[render_mode](params, tag, v1, v2, v3, v4, area);

    if (render_mode == RM_TRANSLUCENT_PRESORT) {
        RenderParamTags<RM_TRANSLUCENT_PRESORT>(area->left, area->top);
    }

    if (render_mode == RM_MODIFIER)
    {
        // 0 normal polygon, 1 inside last, 2 outside last
        if (params->isp.modvol.VolumeMode == 1 ) 
        {
            RENDLOG("STENCIL_SUM_OR");
            SummarizeStencilOr();
        }
        else if (params->isp.modvol.VolumeMode == 2) 
        {
            RENDLOG("STENCIL_SUM_AND");
            SummarizeStencilAnd();
        }
    }
}

u32 ReadRegionArrayEntry(u32 base, RegionArrayEntry* entry) 
{
    bool fmt_v1 = FPU_PARAM_CFG.region_header_type == 0;

    entry->control.full     = vri(emu_vram, base);
    entry->opaque.full      = vri(emu_vram, base + 4);
    entry->opaque_mod.full  = vri(emu_vram, base + 8);
    entry->trans.full       = vri(emu_vram, base + 12);
    entry->trans_mod.full   = vri(emu_vram, base + 16);


    u32 rv;
    if (fmt_v1)
    {
        entry->control.pre_sort = ISP_FEED_CFG.pre_sort;
        entry->puncht.full = 0x80000000;
        rv = 5 * 4;
    }
    else
    {
        entry->puncht.full = vri(emu_vram, base + 20);
        rv = 6 * 4;
    }

    return rv;
}

#define vert_packed_color_(to,src) \
	{ \
	u32 t=src; \
	to[0] = (u8)(t);t>>=8;\
	to[1] = (u8)(t);t>>=8;\
	to[2] = (u8)(t);t>>=8;\
	to[3] = (u8)(t);      \
	}

ISP_BACKGND_T_type CoreTagFromDesc(u32 cache_bypass, u32 shadow, u32 skip, u32 param_offs_in_words, u32 tag_offset) {
    ISP_BACKGND_T_type rv;
    rv.full = 0;
    rv.tag_offset = tag_offset;
    rv.param_offs_in_words = param_offs_in_words;
    rv.skip = skip;
    rv.shadow = shadow;
    rv.cache_bypass = cache_bypass;

    return rv;
}

// render a triangle strip object list entry
void RenderTriangleStrip(RenderMode render_mode, ObjectListEntry obj, taRECT* rect)
{
    Vertex vtx[8];
    DrawParameters params;

    u32 param_base = PARAM_BASE & 0xF00000;

    u32 tag_address = param_base + obj.tstrip.param_offs_in_words * 4;

    bool two_volumes = obj.tstrip.shadow & ~FPU_SHAD_SCALE.intensity_shadow;
    decode_pvr_vertices(&params, tag_address, obj.tstrip.skip, two_volumes, vtx, 8, 0);

    for (int i = 0; i < 6; i++)
    {
        if (obj.tstrip.mask & (1 << (5-i)))
        {
            parameter_tag_t tag = CoreTagFromDesc(params.isp.CacheBypass, obj.tstrip.shadow, obj.tstrip.skip, obj.tstrip.param_offs_in_words, i).full;
            
            int not_even = i&1;
            int even = not_even ^ 1;
            RENDLOG("STRIP: %08X %f %f %f %f %f %f %f %f %f %d", tag,
                vtx[i+not_even].x, vtx[i+not_even].y, vtx[i+not_even].z,
                vtx[i+even].x, vtx[i+even].y, vtx[i+even].z,
                vtx[i+2].x, vtx[i+2].y, vtx[i+2].z,
                i
            );
            RenderTriangle(render_mode, &params, tag, vtx[i+not_even], vtx[i+even], vtx[i+2], nullptr, rect);
        }
    }
}


// render a triangle array object list entry
void RenderTriangleArray(RenderMode render_mode, ObjectListEntry obj, taRECT* rect)
{
    auto triangles = obj.tarray.prims + 1;
    u32 param_base = PARAM_BASE & 0xF00000;


    u32 param_ptr = param_base + obj.tarray.param_offs_in_words * 4;
    bool two_volumes = obj.tstrip.shadow & ~FPU_SHAD_SCALE.intensity_shadow;

    for (int i = 0; i<triangles; i++)
    {
        DrawParameters params;
        Vertex vtx[3];

        u32 tag_address = param_ptr;
        param_ptr = decode_pvr_vertices(&params, tag_address, obj.tarray.skip, two_volumes, vtx, 3, 0);
            
        parameter_tag_t tag  = CoreTagFromDesc(params.isp.CacheBypass, obj.tstrip.shadow, obj.tstrip.skip, (tag_address - param_base)/4, 0).full;

        RENDLOG("TARR: %08X %f %f %f %f %f %f %f %f %f %d", tag,
            vtx[0].x, vtx[0].y, vtx[0].z,
            vtx[1].x, vtx[1].y, vtx[1].z,
            vtx[2].x, vtx[2].y, vtx[2].z,
            i
        );

        RenderTriangle(render_mode, &params, tag, vtx[0], vtx[1], vtx[2], nullptr, rect);
    }
}

// render a quad array object list entry
void RenderQuadArray(RenderMode render_mode, ObjectListEntry obj, taRECT* rect)
{
    auto quads = obj.qarray.prims + 1;
    u32 param_base = PARAM_BASE & 0xF00000;


    u32 param_ptr = param_base + obj.qarray.param_offs_in_words * 4;
    bool two_volumes = obj.tstrip.shadow & ~FPU_SHAD_SCALE.intensity_shadow;

    for (int i = 0; i<quads; i++)
    {
        DrawParameters params;
        Vertex vtx[4];

        u32 tag_address = param_ptr;
        param_ptr = decode_pvr_vertices(&params, tag_address, obj.qarray.skip, two_volumes, vtx, 4, 0);
            
        parameter_tag_t tag = CoreTagFromDesc(params.isp.CacheBypass, obj.qarray.shadow, obj.qarray.skip, (tag_address - param_base)/4, 0).full;

        RENDLOG("QARR: %08X %f %f %f %f %f %f %f %f %f %f %f %f %d", tag,
            vtx[0].x, vtx[0].y, vtx[0].z,
            vtx[1].x, vtx[1].y, vtx[1].z,
            vtx[2].x, vtx[2].y, vtx[2].z,
            vtx[3].x, vtx[3].y, vtx[3].z,
            i
        );

        RenderTriangle(render_mode, &params, tag, vtx[0], vtx[1], vtx[2], &vtx[3], rect);
    }
}

// Render an object list
void RenderObjectList(RenderMode render_mode, pvr32addr_t base, taRECT* rect)
{
    ObjectListEntry obj;

    for (;;) {
        obj.full = vri(emu_vram, base);
        RENDLOG("OBJECT: %08X %08X", base, obj.full);
        base += 4;

        if (!obj.is_not_triangle_strip) {
            RenderTriangleStrip(render_mode, obj, rect);
        } else {
            switch(obj.type) {
                case 0b111: // link
                    if (obj.link.end_of_list)
                        return;

                        base = obj.link.next_block_ptr_in_words * 4;
                    break;

                case 0b100: // triangle array
                    RenderTriangleArray(render_mode, obj, rect);
                    break;
                    
                case 0b101: // quad array
                    RenderQuadArray(render_mode, obj, rect);
                    break;

                default:
                    printf("RenderObjectList: Not handled object type: %d\n", obj.type);
            }
        }
    }
}

// Render a frame
// Called on START_RENDER write
void RenderCORE() {
    u32 base = REGION_BASE;

    RegionArrayEntry entry;
    
    RENDLOG("REFSW2LOG: 0");
    RENDLOG("BGTAG: %08X", ISP_BACKGND_T.full);

    // ---- writing the picture to the framebuffer ----
    // What the hardware adds to a channel before cutting it to 4 bits, by pixel position; half of it for 5
    // bits and a quarter for 6. Measured on a console, every entry exactly (dreamcast-tests/pvr-tests/fbpack; fb555,
    // fb565).
    static const int ditherAdd[4][4] = {
        {  5, 13,  7, 15 },
        {  9,  1, 11,  3 },
        {  6, 14,  4, 12 },
        { 10,  2,  8,  0 }
    };
    // Always FB_W_SOF1: on a console nothing reaches FB_W_SOF2, whatever the interlace and field bits of
    // SCALER_CTL and the display's mode and field are (dreamcast-tests/pvr-tests/sof2probe, sof2field, scalermeas);
    // those bits only choose which lines the scaler takes. FB_W_SOF2 is the second strip buffer: with FB_R_CTRL
    // bit 22 the strips of 16 * size lines go to FB_W_SOF1 and FB_W_SOF2 in turn, each from the buffer's
    // first line (dreamcast-tests/pvr-tests/sof2sweep). That mode is not implemented here.
    const u32 fbBase = FB_W_SOF1;
    const bool rtt = fbBase & 0x1000000;
    const auto fb_packmode = FB_W_CTRL.fb_packmode;
    verify(fb_packmode != 0x7);
    const bool dither = FB_W_CTRL.fb_dither;
    const int bpp = fb_packmode <= 0x3 ? 2 : fb_packmode == 0x4 ? 3 : 4;   // 16 bit formats: 2, 888 packed: 3, 0888 and 8888: 4
    // One pixel of the framebuffer, if the pixel clip (FB_X_CLIP, FB_Y_CLIP: framebuffer coordinates, both ends
    // included) lets it through.
    auto put = [&](int X, int Y, int b8, int g8, int r8, int a8) {
        if (X < (int)FB_X_CLIP.min || X > (int)FB_X_CLIP.max || Y < (int)FB_Y_CLIP.min || Y > (int)FB_Y_CLIP.max)
            return;
        const u32 dst = fbBase + (u32)Y * FB_W_LINESTRIDE.stride * 8 + (u32)X * bpp;
        const int add = dither ? ditherAdd[Y & 3][X & 3] : 0;
        // a channel cut to `bits` bits: add the dither value, drop the low bits, saturate
        auto cut = [add](int v, int bits) {
            int q = (v + (add >> (bits - 4))) >> (8 - bits);
            int top = (1 << bits) - 1;
            return q > top ? top : q;
        };
        if (bpp == 2) {
            u32 pixel;
            if (fb_packmode == 0x0)         // 0555 KRGB: the top bit is the top bit of fb_kval
                pixel = cut(b8, 5) | (cut(g8, 5) << 5) | (cut(r8, 5) << 10) | ((FB_W_CTRL.fb_kval & 0x80) << 8);
            else if (fb_packmode == 0x1)    // 565 RGB
                pixel = cut(b8, 5) | (cut(g8, 6) << 5) | (cut(r8, 5) << 11);
            else if (fb_packmode == 0x2)    // 4444 ARGB: alpha is dithered like the colours
                pixel = cut(b8, 4) | (cut(g8, 4) << 4) | (cut(r8, 4) << 8) | (cut(a8, 4) << 12);
            else                            // 1555 ARGB: the top bit is "alpha is at least the threshold"
                pixel = cut(b8, 5) | (cut(g8, 5) << 5) | (cut(r8, 5) << 10) | ((a8 >= (int)FB_W_CTRL.fb_alpha_threshold) << 15);
            if (rtt)
                (u16&)emu_vram[dst & VRAM_MASK] = pixel;
            else
                pvr_write_area1_16(emu_vram, dst, pixel);
        }
        else if (bpp == 3) {                // 888 RGB, packed
            if (rtt) {
                emu_vram[(dst + 0) & VRAM_MASK] = b8;
                emu_vram[(dst + 1) & VRAM_MASK] = g8;
                emu_vram[(dst + 2) & VRAM_MASK] = r8;
            }
            else {
                emu_vram[pvr_map32(dst + 0)] = b8;
                emu_vram[pvr_map32(dst + 1)] = g8;
                emu_vram[pvr_map32(dst + 2)] = r8;
            }
        }
        else {                              // 0888 KRGB: the top byte is fb_kval; 8888 ARGB: alpha
            u32 top = fb_packmode == 0x5 ? (u32)FB_W_CTRL.fb_kval : (u32)a8;
            u32 pixel = b8 + g8 * 256U + r8 * 256U * 256U + top * 256U * 256U * 256U;
            if (rtt)
                (u32&)emu_vram[dst & VRAM_MASK] = pixel;
            else
                pvr_write_area1_32(emu_vram, dst, pixel);
        }
    };
    // ---- the scaler (SCALER_CTL) ----
    // Measured on a console, every pixel of 77 pictures (dreamcast-tests/pvr-tests/scalermeas: 43 settings; scalertile:
    // the tiles of the region array in six orders, two frames in a row, 1024 lines). The scaler is not a pass
    // over the finished picture: the tiles go through it one after the other, in the order of the region
    // array, and what it keeps between them does not know where a tile is.
    //
    //   factor 0x400 (the low 16 bits): no vertical scaling; a tile's lines go to the tile's own place.
    //
    //   Any other factor: the lines of the tiles are one stream. A tile with y = 0 starts a new stream (the
    //   stream before it is ended first), and so does a frame. Output row j of a stream is taken at
    //   pos = j * factor (+ 1024 in a stream that a tile with y = 0 started, when the picture shrinks and both
    //   the interlace and the field bit are set), in units of 1/1024 line: line k = pos >> 10 and the next one,
    //   weighted with the top 4 bits of the fraction, (a * (16 - f) + b * f) >> 4. When the picture shrinks
    //   (factor above 0x400) the lines are filtered first, (above + 2 * line + below) >> 2 with the stream's
    //   first line repeated above it.
    //   A row is written when the stream has reached line k + 1 (k + 2 with the filter), at the x of the tile
    //   being worked on then and at row j & 1023 -- the row counter has 10 bits and belongs to the stream,
    //   not to the tile. In the order KallistiOS uses (column by column, top to bottom) that is the picture
    //   one expects; row by row, every tile lands somewhere else.
    //   When a stream ends, at the x of its last tile: with the filter, the last line counts once more as the
    //   line below it; then the rows that need nothing after the last line are written (a row at the last
    //   line itself only when the fraction of pos is 0, all 10 bits).
    //   At the end of the frame the filtered last line is stuck at the value of the tile's first pixel for
    //   the whole width of the tile (a row that uses it shows that pixel 31 times over).
    //
    //   Bit 16: pairs of pixels are averaged, (a + b) >> 1, after the above, and x is halved.
    //   FB_X_CLIP and FB_Y_CLIP are applied to what comes out, in framebuffer coordinates.
    //   Not measured: factor 0 (nothing is written here).
    const int vfactor = SCALER_CTL.vscalefactor;
    const bool vfilter = vfactor > 0x400, halve = SCALER_CTL.hscale;
    const int fieldStart = (vfilter && SCALER_CTL.interlace && SCALER_CTL.fieldselect) ? 1024 : 0;
    struct ScalerLine { u8 p[32][4]; };          // blue, green, red, alpha
    ScalerLine ring[8];                          // the stream's last lines; line i is ring[i & 7]
    s64 streamLines = 0, streamRow = 0;          // lines taken in, rows written
    int streamStart = 0, streamX = -1;           // pos of row 0; x of the last tile (-1: no tile yet)
    auto putRow = [&](int X, int Y, const int row[32][4]) {
        if (halve) {
            for (int x = 0; x < 16; x++)
                put(X / 2 + x, Y, (row[2 * x][0] + row[2 * x + 1][0]) >> 1, (row[2 * x][1] + row[2 * x + 1][1]) >> 1,
                    (row[2 * x][2] + row[2 * x + 1][2]) >> 1, (row[2 * x][3] + row[2 * x + 1][3]) >> 1);
        } else {
            for (int x = 0; x < 32; x++)
                put(X + x, Y, row[x][0], row[x][1], row[x][2], row[x][3]);
        }
    };
    auto filtered = [&](s64 i, int x, int c) -> int {
        const int v = ring[i & 7].p[x][c];
        if (!vfilter)
            return v;
        return (ring[(i > 0 ? i - 1 : 0) & 7].p[x][c] + 2 * v + ring[(i + 1) & 7].p[x][c]) >> 2;
    };
    // write the rows that can be written. end: the stream is over (with the filter the last line has been
    // taken in once more); stuck: the end of the frame
    auto emitRows = [&](int X, bool end, bool stuck) {
        const s64 last = (end && vfilter ? streamLines - 1 : streamLines) - 1;      // the last line of the tiles
        for (;;) {
            const s64 pos = streamRow * vfactor + streamStart, k = pos >> 10;
            const int f = (int)(pos >> 6) & 15;
            if (end) {
                if (!(k + 1 <= last || (k == last && (pos & 1023) == 0)))
                    return;
            } else if (k + (vfilter ? 2 : 1) > streamLines - 1)
                return;
            int row[32][4];
            for (int x = 0; x < 32; x++)
                for (int c = 0; c < 4; c++) {
                    int a = filtered(k, stuck && vfilter && k == last ? 0 : x, c);
                    if (f)
                        a = (a * (16 - f) + filtered(k + 1, stuck && vfilter && k + 1 == last ? 0 : x, c) * f) >> 4;
                    row[x][c] = a;
                }
            putRow(X, (int)(streamRow & 1023), row);
            streamRow++;
        }
    };
    auto endStream = [&](bool stuck) {
        if (streamX < 0 || streamLines == 0)
            return;
        if (vfilter) {
            ring[streamLines & 7] = ring[(streamLines - 1) & 7];
            streamLines++;
        }
        emitRows(streamX, true, stuck);
    };

    // Parse region array
    FILE* const logAll = rendlog;       // the log; with rendlog_tiles, rendlog is it only for those tiles
    bool prevWrittenOut = false;        // the entry before this one was written out to the framebuffer
    bool havePrev = false;              // there was an entry before this one
    int prevTileX = 0, prevTileY = 0;   // its tile
    do {
        auto step = ReadRegionArrayEntry(base, &entry);
        if (logAll && !rendlog_tiles.empty()) {
            const u32 t = entry.control.tilex | entry.control.tiley << 8;
            rendlog = std::find(rendlog_tiles.begin(), rendlog_tiles.end(), t) != rendlog_tiles.end() ? logAll : nullptr;
        }
        
        RENDLOG("TILE: %08X %08X %08X %08X %08X %08X %08X", base, entry.control.full, entry.opaque.full, entry.opaque_mod.full, entry.trans.full, entry.trans_mod.full, entry.puncht.full);

        base += step;

        taRECT rect;
        rect.top = entry.control.tiley * 32;
        rect.left = entry.control.tilex * 32;

        rect.bottom = rect.top + 32;
        rect.right = rect.left + 32;

        parameter_tag_t bgTag;

        ClearFpuCache();
        // register BGPOLY to fpu
        {
            bgTag = ISP_BACKGND_T.full;
        }

        tilePixelsShaded = 0;

        // Tile needs clear?
        if (!entry.control.z_keep)
        {
            RENDLOG("ZCLEAR");
            // Clear Param + Z + stencil buffers
            // The background's depth is stored as a float of 19 significant bits whose smallest exponent is that of 0.25:
            // below it the leading bit is 0 (a denormal), so the smallest step is 2^-20 and anything below 2^-20 is zero
            // (dreamcast-tests/pvr-tests/bgdepth: 32 depths from 2^-20 to 1e6; bgtie: with the background's tag below,
            // a polygon at 0.5 ties with a background of 0.5 + 2^-20, as the 19 bit float has it and a fixed point would
            // not; trtie pages 12-15, polly2-data rez_ingame: a polygon at exactly a background of about 1e-5 is in front
            // of it; dumprules pages 4-7, polly2-data xtreme_intro: nothing is behind a background below 2^-20).
            float bgDepth = ISP_BACKGND_D.f;
            if (std::isfinite(bgDepth) && bgDepth >= 0) {
                if (bgDepth < 0.25f)
                    bgDepth = (float)(floor((double)bgDepth * 1048576.0) / 1048576.0);
                else {
                    int e;
                    const double m = frexp((double)bgDepth, &e);
                    bgDepth = (float)ldexp(floor(m * 524288.0) / 524288.0, e);
                }
            } else if (((ISP_BACKGND_D.i >> 23) & 0xff) < 107)
                bgDepth = 0.0f;
            ClearBuffers(bgTag, bgDepth, 0);
        } else {
            RENDLOG("ZKEEP");
            ClearParamStatusBuffer();
        }

        // Render OPAQ to TAGS
        if (!entry.opaque.empty)
        {
            RENDLOG("OPAQ");
            RenderObjectList(RM_OPAQUE, entry.opaque.ptr_in_words * 4, &rect);
        
            if (!entry.opaque_mod.empty)
            {
                RENDLOG("OPAQ_MOD");
                RenderObjectList(RM_MODIFIER, entry.opaque_mod.ptr_in_words * 4, &rect);
            }
        }

        // Does the first punch-through pass draw anything in this tile? Then the opaque runs do not end at volume
        // edges, and the opaque pixels inside a volume are shaded again with the punch-through ones (refsw_tile.cpp,
        // OpInPt; dreamcast-tests/pvr-tests/optrun).
        extern bool OpInPt;
        {
            OpInPt = !entry.puncht.empty;
            if (OpInPt) {
                extern void SnapshotTile(bool save);
                extern void PeelBuffersPTInitial();
                extern int PtPass;
                SnapshotTile(true);
                PeelBuffersPTInitial();
                PtPass = 0;
                ClearMoreToDraw();
                RenderObjectList(RM_PUNCHTHROUGH_PASS0, entry.puncht.ptr_in_words * 4, &rect);
                OpInPt = GetMoreToDraw();
                SnapshotTile(false);
            }
        }
        RENDLOG("OP_PARAMS");
        // Render TAGS to ACCUM
        RenderParamTags<RM_OPAQUE>(rect.left, rect.top);

        // render PT to TAGS
        if (!entry.puncht.empty)
        {
            RENDLOG("PT");

            PeelBuffersPTInitial();
            ptChunking = true;
            ptChunkSize = ISP_FEED_CFG.full >> 4 & 0x3ff;
            ptChunks = 1;
            for (ptChunk = 0; ptChunk < ptChunks; ptChunk++) {
                if (ptChunk) {
                    RENDLOG("PT_CHUNK");
                    extern void PeelBuffersPTChunk();
                    PeelBuffersPTChunk();
                }
                // one pass after the other until no pixel failed the alpha test (or nothing is left to try)
                for (int pass = 0; ; pass++) {
                    if (pass)
                        RENDLOG("PT_N");
                    ClearMoreToDraw();
                    { extern int PtPass; PtPass = pass; }
                    ptChunkAt = 0;
                    ptChunkFill = 0;
                    ptChunkPrevQuad = false;
                    RenderObjectList(pass ? RM_PUNCHTHROUGH_PASSN : RM_PUNCHTHROUGH_PASS0, entry.puncht.ptr_in_words * 4, &rect);
                    if (!GetMoreToDraw())
                        break;
                    if (pass)
                        RENDLOG("PT_N_PARAMS");
                    else
                        RENDLOG("PT_PARAMS");
                    ClearMoreToDraw();
                    RenderParamTags<RM_PUNCHTHROUGH_PASS0>(rect.left, rect.top);
                    if (!GetMoreToDraw())
                        break;
                }
            }
            ptChunking = false;
            PeelBuffersPTFinal();
            if (!entry.opaque_mod.empty)
            {
                RENDLOG("PT_MOD");
                RenderObjectList(RM_MODIFIER, entry.opaque_mod.ptr_in_words * 4, &rect);
                RENDLOG("PT_MOD_PARAMS");
                RenderParamTags<RM_PUNCHTHROUGH_MV>(rect.left, rect.top);
            }
        }

        // layer peeling rendering
        if (!entry.trans.empty)
        {
            if (entry.control.pre_sort) {
                RENDLOG("TR_PS");
                 // clear the param buffer
                 ClearParamStatusBuffer();

                 // render to TAGS
                 {
                     RenderObjectList(RM_TRANSLUCENT_PRESORT, entry.trans.ptr_in_words * 4, &rect);
                 }

                // what happens with modvols here?
                //  if (!entry.trans_mod.empty)
                //  {
                //      RenderObjectList(RM_MODIFIER, entry.trans_mod.ptr_in_words * 4, &rect);
                //  }
            } else {
                RENDLOG("TR_AS");
                SaveDepth();
                // the first reference is the key of what is there (opaque, punch-through or background: its depth and its
                // tag), not a tag below every other: a translucent polygon at exactly that depth is drawn only with a tag
                // above it (dreamcast-tests/pvr-tests/trtie; polly2-data shenmue_intro2 (522,170))
                do
                {
                    RENDLOG("TR_AS_N");
                    // prepare for a new pass
                    ClearMoreToDraw();

                    // copy depth test to depth reference buffer, clear depth test buffer, clear stencil
                    PeelBuffers(FLT_MAX, 0);

                    // render to TAGS
                    {
                        RenderObjectList(RM_TRANSLUCENT_AUTOSORT, entry.trans.ptr_in_words * 4, &rect);
                    }

                    if (!entry.trans_mod.empty)
                    {
                        RenderObjectList(RM_MODIFIER, entry.trans_mod.ptr_in_words * 4, &rect);
                    }

                    RENDLOG("TR_PARAMS");
                    // render TAGS to ACCUM
                    RenderParamTags<RM_TRANSLUCENT_AUTOSORT>(rect.left, rect.top);
                } while (GetMoreToDraw() != 0);
                RestoreDepth();
            }
        }

        {
            auto copy = (u32*)GetColorOutputBuffer();
            RENDLOG("PIXELS");
            for (unsigned i = 0; i < MAX_RENDER_PIXELS; i++)
            {
                RENDLOG("%08X", copy[i]);
            }
        }
        
        // Copy to vram. A keep Z tile that shades nothing ends at once, while the tile before it is still being
        // written out, and is itself never written to the framebuffer; the buffers still change places
        // (dreamcast-tests/pvr-tests/accbuf, pages 0 and 3: the tiles after it alternate as if it had been written
        // out). What else that collision does depends on its timing (README, accbuf) and is not modelled here.
        // Only after a tile that is written out. After one that is not, the keep Z entry writes the tile buffer
        // out at the place of the entry before it, with what that entry drew (dreamcast-tests/pvr-tests/keepz:
        // page 1, an entry not written out at one cell and an empty keep Z entry at the next: the first cell
        // gets its picture, the second nothing). Games give both entries the same tile (polly2-data logo, mem,
        // menu2, menu3, thps2_menu): it looks like an ordinary write-out (keepz page 0).
        const bool emptyKeepZ = entry.control.z_keep && tilePixelsShaded == 0 && prevWrittenOut;
        const bool emptyAfterNoWrite = entry.control.z_keep && tilePixelsShaded == 0 && !prevWrittenOut && havePrev;
        const int tileX = emptyAfterNoWrite ? prevTileX : entry.control.tilex;
        const int tileY = emptyAfterNoWrite ? prevTileY : entry.control.tiley;
        prevWrittenOut = !entry.control.no_writeout && !emptyKeepZ;
        prevTileX = entry.control.tilex; prevTileY = entry.control.tiley; havePrev = true;
        if (!entry.control.no_writeout && emptyKeepZ)
            NextTileAccumulationBuffers();
        else if (!entry.control.no_writeout)
        {
            auto src = GetColorOutputBuffer();      // blue, green, red, alpha
            const int X0 = tileX * 32, Y0 = tileY * 32;
            if (vfactor == 0x400) {
                for (int y = 0; y < 32; y++) {
                    int row[32][4];
                    for (int x = 0; x < 32; x++, src += 4)
                        for (int c = 0; c < 4; c++)
                            row[x][c] = src[c];
                    putRow(X0, Y0 + y, row);
                }
            } else if (vfactor != 0) {
                if (tileY == 0) {
                    endStream(false);
                    streamLines = streamRow = 0;
                    streamStart = fieldStart;
                }
                for (int y = 0; y < 32; y++, src += 32 * 4) {
                    memcpy(ring[streamLines & 7].p, src, 32 * 4);
                    streamLines++;
                    emitRows(X0, false, false);
                }
                streamX = X0;
            }
            // the buffer that is written out stays behind as the next tile's secondary buffer; an entry
            // that is not written out (KallistiOS's first one) leaves the buffers where they are
            NextTileAccumulationBuffers();
        }
    } while (!entry.control.last_region);
    rendlog = logAll;

    if (vfactor != 0x400 && vfactor != 0)
        endStream(true);
}


#if 0
void Hackpresent()
{
    if (FB_R_SIZE.fb_x_size == 0 || FB_R_SIZE.fb_y_size == 0)
        return;

    int width = (FB_R_SIZE.fb_x_size + 1) << 1; // in 16-bit words
    int height = FB_R_SIZE.fb_y_size + 1;
    int modulus = (FB_R_SIZE.fb_modulus - 1) << 1;

    int bpp;
    switch (FB_R_CTRL.fb_depth)
    {
    case fbde_0555:
    case fbde_565:
        bpp = 2;
        break;
    case fbde_888:
        bpp = 3;
        //width = (width * 2) / 3;     // in pixels
        modulus = (modulus * 2) / 3; // in pixels
        break;
    case fbde_C888:
        bpp = 4;
        //width /= 2;   // in pixels
        modulus /= 2; // in pixels
        break;
    default:
        die("Invalid framebuffer format\n");
        bpp = 4;
        break;
    }

    u32 addr = SCALER_CTL.interlace && SCALER_CTL.fieldselect ? FB_R_SOF2 : FB_R_SOF1;

    // printf("Presenting: %X\n", addr);
    addr &= ~3;

    size_t pb_len = width * (SPG_CONTROL.interlace ? (height * 2 + 1) : height) * 4;
    void* pb = calloc(1, pb_len);

    u8 *dst = (u8 *)pb;

    if (SPG_CONTROL.interlace & SPG_STATUS.fieldnum) {
        dst += width * 4;
    }

#define RED_5 10
#define BLUE_5 0
#define RED_6 11
#define BLUE_6 0
#define RED_8 16
#define BLUE_8 0

    switch (FB_R_CTRL.fb_depth)
    {
    case fbde_0555: // 555 RGB
        for (int y = 0; y < height; y++)
        {
            for (int i = 0; i < width; i++)
            {
                u16 src = pvr_read_area1_16(emu_vram, addr);
                *dst++ = (((src >> BLUE_5) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                *dst++ = (((src >> 5) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                *dst++ = (((src >> RED_5) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                *dst++ = 0xFF;
                addr += bpp;
            }
            addr += modulus * bpp;
            if (SPG_CONTROL.interlace) {
                dst += width * 4;
            }
        }
        break;

    case fbde_565: // 565 RGB
        for (int y = 0; y < height; y++)
        {
            for (int i = 0; i < width; i++)
            {
                u16 src = pvr_read_area1_16(emu_vram, addr);
                *dst++ = (((src >> BLUE_6) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                *dst++ = (((src >> 5) & 0x3F) << 2) + (FB_R_CTRL.fb_concat >> 1);
                *dst++ = (((src >> RED_6) & 0x1F) << 3) + FB_R_CTRL.fb_concat;
                *dst++ = 0xFF;
                addr += bpp;
            }
            addr += modulus * bpp;

            if (SPG_CONTROL.interlace) {
                dst += width * 4;
            }
        }
        break;
    case fbde_888: // 888 RGB
        for (int y = 0; y < height; y++)
        {
            for (int i = 0; i < width; i++)
            {
                if (addr & 1)
                {
                    u32 src = pvr_read_area1_32(emu_vram, addr - 1);
                    *dst++ = src >> RED_8;
                    *dst++ = src >> 8;
                    *dst++ = src >> BLUE_8;
                }
                else
                {
                    u32 src = pvr_read_area1_32(emu_vram, addr);
                    *dst++ = src >> (RED_8 + 8);
                    *dst++ = src >> 16;
                    *dst++ = src >> (BLUE_8 + 8);
                }
                *dst++ = 0xFF;
                addr += bpp;
            }
            addr += modulus * bpp;

            if (SPG_CONTROL.interlace) {
                dst += width * 4;
            }
        }
        break;
    case fbde_C888: // 0888 RGB
        for (int y = 0; y < height; y++)
        {
            for (int i = 0; i < width; i++)
            {
                u32 src = pvr_read_area1_32(emu_vram, addr);
                *dst++ = src >> RED_8;
                *dst++ = src >> 8;
                *dst++ = src >> BLUE_8;
                *dst++ = 0xFF;
                addr += bpp;
            }
            addr += modulus * bpp;
                
            if (SPG_CONTROL.interlace) {
                dst += width * 4;
            }
        }
        break;
    }
    {
        extern Window x11_win;
        extern Display* x11_disp;
        extern Visual* x11_vis;

        extern int x11_width;
        extern int x11_height;
        XImage* ximage = XCreateImage(x11_disp, x11_vis, 24, ZPixmap, 0, (char*)pb, width, SPG_CONTROL.interlace ? height * 2 : height, 32, width * 4);

        GC gc = XCreateGC(x11_disp, x11_win, 0, 0);
        XPutImage(x11_disp, x11_win, gc, ximage, 0, 0, (x11_width - width) / 2, (x11_height - (SPG_CONTROL.interlace ? height * 2 : height)) / 2, width, SPG_CONTROL.interlace ? height * 2 : height);
        XFree(ximage);
        XFreeGC(x11_disp, gc);
    }
    free(pb);
}
#endif

}   // namespace refcore
