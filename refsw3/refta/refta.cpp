/*
    refta: the PowerVR2 tile accelerator, written from what a console does (dreamcast-tests/ta-tests: ta-tests and
    ta-probe, 2026-11). Each rule below names the case that shows it: "ta-tests x" or "ta-probe x".

    No float or double: the TA only ever takes floats apart (colours to 8 bits, intensities, coordinates to tiles),
    and that is done on their bits (refta_fp.h, the TA's own: the ISP and TSP units are not like it).

    State of the object lists lives where the hardware keeps it, in TA_OL_POINTERS (0x005f8600, one word per tile;
    there are 608, not the 600 the documents list: ta-probe grid_61x10). For each tile the word holds
        bit 31      the tile has an entry in the open list (cleared when the list ends)
        bit 30, 29  its last entry is a quad (sprite) array / a triangle array, still open for more
        bits 28-25  that array's count - 1        bit 24  its shadow bit
        bits 23-2   the array entry's address; after a strip entry, the next free word
        bits 1-0    the skip of the last entry (low two bits)
    (ta-probe olp_reset, olp_entries, stack_20, stack_20_sprites, sprite_bits_sh, two_vol_skip). An open array
    entry is written to memory only when the tile's next entry cannot join it, or when the list ends; the entry
    word itself (its parameter offset) is kept beside the register (pending[]). So a list is never read back.

    Order of writes: the console writes no object list word before the list ends (ta-probe olp_entries m0, ...),
    then entries and links before end words (ta-tests opb_none). Here strip entries and links are written at once
    and open arrays and end words when the list ends: the same memory at the end of a list.
*/
#include "license/bsd"
#include "refta.h"
#include "types.h"
#include "hw/pvr/pvr_regs.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/holly/holly_intc.h"
#ifndef REFSW_OFFLINE
#include "hw/sh4/sh4_if.h"          // EMUERROR's PC and PR
#endif
#include "refta_fp.h"

#include <utility>
#include <vector>

namespace refta {

namespace {

using refta_fp::F32;

const int kTileRegs = 608;          // tiles with a register; later tiles get no entries (ta-probe grid_41x15)
const u32 kActive = 0x80000000u, kQuads = 0x40000000u, kTris = 0x20000000u;
const u32 kCount = 0x1E000000u, kShadow = 0x01000000u, kAddr = 0x00FFFFFCu;
const int kFar = 0x3FFFFFF;         // the tile of a coordinate of 2^31 or more, +inf or +NaN
const u32 kEnd = 0xF0000000u, kLink = 0xE0000000u;
const u32 kStripLen[4] = { 3, 4, 6, 8 };
const HollyInterruptID kListDone[5] = { holly_OPAQUE, holly_OPAQUEMOD, holly_TRANS, holly_TRANSMOD, holly_PUNCHTHRU };

enum Kind { kPoly, kSprite, kModvol };

struct Bounds { int x1, y1, x2, y2; };

Bounds Union(const Bounds& a, const Bounds& b)
{
    return { a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1, a.x2 > b.x2 ? a.x2 : b.x2, a.y2 > b.y2 ? a.y2 : b.y2 };
}

// ---- memory and registers ----

u8* vram;
u32 Rd(u32 a) { return *(u32*)(vram + pvr_map32(a)); }
void Wr(u32 a, u32 v) { *(u32*)(vram + pvr_map32(a)) = v; }
u32& Reg(u32 addr) { return PvrReg(addr, u32); }
u32& TileReg(int t) { return PvrReg(TA_OL_POINTERS_START_addr + 4u * (u32)t, u32); }

// ---- the TA's state ----

bool inited;                        // TA_LIST_INIT since SOFTRESET: data before it is dropped
bool stopped;                       // parameter types 2, 3, 6: no more input until SOFTRESET (ta-probe err_para_types)

// latched by TA_LIST_INIT and TA_LIST_CONT
u32 matrixBase[5], ispBase;
u32 nextUp, nextDown;               // the next block growing up / growing down (bytes): two pointers (ta-probe mode_change)
u32 downUnit;                       // the opaque list's block size in bytes: the down pointer is that far below a block

// the open list
int list = -1;
u32 listBase, blockWords;
bool down, noFlush;
int gw, gh;
u32 pending[kTileRegs];             // the open array entry of each tile
u32 pendingEnd[kTileRegs];          // the parameter offset just after its last polygon
u32 pendingSize[kTileRegs];         // the parameter words of each of its polygons
std::vector<std::pair<u32, u32>> slotQueue, blockQueue;   // object list words not yet in memory, oldest first:
                                                          // those in the list's matrix, those in blocks

// kept by TA_LIST_INIT; SOFTRESET says for each
u32 maxVertex = 3;                  // Strip_Len: TA_LIST_INIT keeps it, SOFTRESET makes it 3 (ta-probe strip_len_*)
u32 face[2];                        // intensity face colours: both keep them (ta-probe int2_*)
Bounds clipRect;                    // user clip: TA_LIST_INIT keeps it, SOFTRESET makes it tile (0, 0) (ta-probe uclip_*)
Bounds volAcc;                      // the bounds of a modifier volume so far: TA_LIST_INIT keeps them,
bool volFresh = true;               // SOFTRESET drops them (ta-probe mvleak_init, mvleak_reset)

// the current header
struct Header {
    Kind kind;
    int clipMode;
    bool tex, offs, uv16, twoVol, shadow, volume;
    int colType;                    // 0 packed, 1 float, 2 intensity, 3 intensity with the last face colours
    u32 w[5];                       // ISP, TSP, TCW (, TSP1, TCW1)
    int words;
    u32 skip;                       // of the entries: one volume's words after x, y, z
    int vtxWords;                   // words after x, y, z of a vertex, all volumes
    bool vtx64;                     // vertices of two blocks
    u32 base, offsCol;              // sprites
    bool inModvolList;              // a sprite in a modifier volume list (ta-probe sprite_in_mv)
    bool listless;                  // vertices with no header (Listless)
} hdr, openHdr;

// What the last headers leave for vertices that come with no header (no list open, or none since it opened):
// kept through the end of a list and SOFTRESET (ta-probe err_vertex_first after the prelude's header).
u32 lastIsp;                        // the ISP word of the last header of any kind
bool lastShadow;                    // the shadow bit and two volumes of the last polygon header (modifier volume
bool lastTwoVol;                    // headers leave them: listless_modvol)
bool haveHeader;                    // a header since the list began (or since TA_LIST_INIT: ta-probe i8_zero, i8_done)

// vertices of the strip being built
struct Vtx {
    u32 x, y, z;
    u32 a[8];
    u32 bx, by;                     // what its tiles are found from (mostly x, y)
    bool xyz;                       // only x, y, z among the parameters (three of the nine words of the listless way)
};
Vtx strip[12];
int nv, parity;
enum { eNone, eHdr2, eVtx2, eListless2 } expect;
u32 firstHalf[8];
// a sprite header in the middle of a strip (ta-probe sprite_hdr_mid, sprite_mid_*): 1 waiting for the vertex that
// ends the strip, 2 dropping the block after it; then the header applies
int spriteMid;
u32 spriteHdr[8];
// TA_LIST_INIT in the middle of a strip (ta-probe i8_*): the polygon in progress counts only the vertices after it
// (initCount) for its size; a header after it is taken in the middle of the strip, and the polygon in progress keeps
// the header it began with (openHdr)
bool initMid;
int initCount;
bool openHdrValid;

void Raise(HollyInterruptID id) { asic_RaiseInterrupt(id); }

// Input that stops a console's TA: it takes no more data, its FIFO fills, and the CPU's next write to it never ends
// (ta-probe lock_type3, lock_type6: pvrio's guard reset it; before the guard a console had to be restarted): an error.
// Input whose outcome refta does not know or knows it gets wrong (see dreamcast-tests README, "Not in refta"): a
// warning. Said the way the emulator's EMUERROR and EMUWARN say things, with the CPU's PC and PR; the offline tools have no
// CPU and gives the place in its script instead.
#ifdef REFSW_OFFLINE
}   // namespace
}   // namespace refta
const char* refta_offline_where();
namespace refta {
namespace {
#define TA_LOCKUP(format, ...)                                                                                    \
    printf("Error in %20s:%s:%d PC: %s PR: -: " format "\n", __FILE__, __FUNCTION__, __LINE__, refta_offline_where(), \
           ##__VA_ARGS__)
#define TA_UNMODELLED(format, ...)                                                                                \
    printf("Warning in %20s:%s:%d PC: %s PR: -: " format "\n", __FILE__, __FUNCTION__, __LINE__,                    \
           refta_offline_where(), ##__VA_ARGS__)
#else
#define TA_LOCKUP(format, ...) EMUERROR(format, ##__VA_ARGS__)
#define TA_UNMODELLED(format, ...) EMUWARN(format, ##__VA_ARGS__)
#endif

// ---- floats, taken apart ----

// a float colour channel: floor(256 c) - 1 in 0..255; negative (-0, -inf, -NaN) 0; 1 or more, +inf, +NaN 255
// (ta-tests float_sweep, float_bounds)
u32 Channel8(u32 bits)
{
    const F32 f = F32::Of(bits);
    if (f.neg)
        return 0;
    if (f.special || f.atLeastPow2(0))
        return 255;
    int64_t v = (int64_t)f.floorScaled(8) - 1;
    return v < 0 ? 0 : (u32)v;
}
u32 FloatColour(u32 a, u32 r, u32 g, u32 b)
{
    return Channel8(a) << 24 | Channel8(r) << 16 | Channel8(g) << 8 | Channel8(b);
}
// an intensity as 0..256: floor(256 i) clamped; +NaN is 1 (ta-tests int1_sweep_a/b, int_offset; int_table)
u32 Intensity256(u32 bits)
{
    const F32 f = F32::Of(bits);
    if (f.neg)
        return 0;
    if (f.special || f.atLeastPow2(0))
        return 256;
    return f.floorScaled(8);
}
// the face colour scaled: each of R, G, B times the intensity / 256, alpha kept (ta-tests int_table)
u32 Scale(u32 faceCol, u32 intensityBits)
{
    u32 i = Intensity256(intensityBits);
    return (faceCol & 0xFF000000u) | (((faceCol >> 16 & 255) * i >> 8) << 16) | (((faceCol >> 8 & 255) * i >> 8) << 8) |
           ((faceCol & 255) * i >> 8);
}
// the tile of a coordinate: floor(x / 32); with the sign bit -1, past 2^31 far (ta-tests bin_*)
int TileOf(u32 bits)
{
    const F32 f = F32::Of(bits);
    if (f.special)
        return f.neg ? -1 : kFar;
    if (f.neg)
        return -1;                  // -0 too: it counts only where nothing else is in the grid (ta-tests fuzz_110, fuzz_154)
    if (f.atLeastPow2(31))
        return kFar;
    return (int)f.floorScaled(-5);
}

// ---- object lists ----

u32 Slot(int t) { return listBase + (u32)t * blockWords * 4; }

// Growing up nothing at or past TA_OL_LIMIT is written there, growing down nothing below it: the word goes to
// TA_OL_LIMIT itself (ta-tests ol_limit_at_base, ol_limit_below; ta-probe ol_limit_mid_matrix, init_beyond_limit,
// down_limit_above, mode_change).
bool PastLimit(u32 a)
{
    const u32 lim = Reg(TA_OL_LIMIT_addr) & kAddr;
    return down ? a < lim : a >= lim;
}
// An entry or a link given such an address is "object lists full", at once (ta-probe ol_limit_mid_matrix,
// ol_limit_base_mid: before the end of the list; end words sent there raise nothing).
u32 Placed(u32 a)
{
    if (PastLimit(a))
        Raise(holly_MATR_NOMEM);
    return a;
}
// The next n waiting object list words to memory: the matrix's first, oldest first in each queue.
void Drain(size_t n)
{
    for (; n && (slotQueue.size() || blockQueue.size()); n--) {
        auto& q = slotQueue.size() ? slotQueue : blockQueue;
        Wr(q.front().first, q.front().second);
        q.erase(q.begin());
    }
}
// A write to the object lists. The words wait in a queue, and go to memory after the polygon that made them: when
// 31 or more wait then, all but 16 go, words in the list's matrix before words in blocks, oldest first; the rest when
// the list ends (ta-probe cache_tiles_steps: nothing until 31, then 15 at a time from polygons of one tile; q48_up,
// q16_up, q48_then_small: polygons of many tiles leave 16 each time; cache_chain: counted in words, not lines;
// olp_down: tile 6's slot before tile 5's older blocks). Where a matrix or a block lies on parameters this decides
// which stays (ta-tests lfuzz_01240, lfuzz_01913).
void OlWrite(u32 a, u32 v)
{
    const bool inMatrix = a >= listBase && a < listBase + (u32)(gw * gh) * blockWords * 4;
    if (PastLimit(a))
        a = Reg(TA_OL_LIMIT_addr) & kAddr;
    (inMatrix ? slotQueue : blockQueue).push_back({ a, v });
}
// after a polygon (or an object list set)
void DrainAfterPolygon()
{
    const size_t n = slotQueue.size() + blockQueue.size();
    if (n >= 31)
        Drain(n - 16);
}

// The next block. Growing up: the pointer, which moves on unless the block after it would reach TA_OL_LIMIT;
// moving onto the last block that fits is "object lists full" (ta-tests ol_limit; ta-probe up_limit_two); a block
// that cannot move on is handed out again (ta-tests ol_limit: a block linked to itself). Growing down the same
// mirrored, with the pointer one opaque block below the top (TA_NEXT_OPB_INIT less the opaque block size after
// TA_LIST_INIT): a block of any size ends where the last one began (ta-probe down_noalloc, down_sizes, down_mid;
// ta-tests ol_limit_down; the opaque block size: fuzz cases with other sizes).
u32 Allocate()
{
    const int64_t lim = Reg(TA_OL_LIMIT_addr) & kAddr, size = blockWords * 4;
    u32 b;
    if (!down) {
        b = nextUp;
        int64_t n = (int64_t)b + size;
        if (n < lim) {
            if (n + size >= lim)
                Raise(holly_MATR_NOMEM);
            nextUp = (u32)n;
        }
        Reg(TA_NEXT_OPB_addr) = nextUp >> 2;
    } else {
        b = nextDown + downUnit - (u32)size;
        int64_t nb = (int64_t)b - size;
        if (nb > lim) {
            if (nb - size <= lim)
                Raise(holly_MATR_NOMEM);
            nextDown = b - downUnit;
        }
        Reg(TA_NEXT_OPB_addr) = nextDown >> 2;
    }
    return b;
}

// Where the tile's next word goes: its slot when it has no entry yet, after its open array, or the register's
// address after a strip entry. An open array is written out here.
u32 CloseTile(int t)
{
    u32 r = TileReg(t);
    if (!(r & kActive))
        return Slot(t);
    if (r & (kTris | kQuads)) {
        OlWrite(r & kAddr, pending[t]);
        return (r & kAddr) + 4;
    }
    return r & kAddr;
}

// An entry for tile t; sizeWords: the parameters of one polygon of the entry. raw: a word an object list set
// gives, written as it is after what the tile has (never an open array).
void Append(int t, u32 e, u32 sizeWords, bool raw = false)
{
    // A tile with no register (from 608 on): strip entries go to its slot, each on the one before; arrays, which
    // only a register holds open, are lost (ta-tests grid_64x16; ta-probe grid_41x15, where the end word covers them)
    if (t >= kTileRegs) {
        if (raw || !(e >> 31))
            OlWrite(Placed(Slot(t)), e);
        return;
    }
    u32& r = TileReg(t);
    const bool array = !raw && (e >> 31) != 0;
    const u32 kind = (e >> 29) == 5 ? kQuads : kTris;
    // Joining the open array: same type, shadow and skip, fewer than 16, this polygon's parameters right after
    // those of the array's last polygon (a polygon between them that wrote parameters elsewhere breaks it; one that
    // wrote none does not: ta-probe stack_20, stack_culled, uclip_mode1_stack, uclip_outside_stack, stack_strip),
    // and as many parameter words as its polygons (two volumes and Shadow alone have the same shadow bit and skip,
    // and do not join: ta-tests lfuzz_00273, lfuzz_00551, lfuzz_00893, lfuzz_00899; other header words may differ:
    // ta-tests modvol_volume_bit)
    if ((r & kActive) && array && (r & (kTris | kQuads)) == kind) {
        const u32 p = pending[t], count = ((p >> 25) & 15) + 1;
        if ((p & 0xE1E00000u) == (e & 0xE1E00000u) && count < 16 && pendingEnd[t] == (e & 0x1FFFFF) &&
            pendingSize[t] == sizeWords) {
            pending[t] = p + (1u << 25);
            pendingEnd[t] += sizeWords;
            r += 1u << 25;
            return;
        }
    }
    u32 a = Placed(CloseTile(t));
    // The last word of a block (by the address: a block that does not start on a multiple of its size is cut
    // short, ta-probe opb_unaligned_init, opb_unaligned_base) takes the link to a new block. A full block with no
    // more entries takes the end word there (ta-probe opb_exact_7).
    if (((a >> 2) & (blockWords - 1)) == blockWords - 1) {
        u32 b = Allocate();
        OlWrite(a, kLink | b);
        a = Placed(b);
    }
    // (an object list set keeps the skip and shadow bits the register had: olset_a, lock_type2_zero)
    const u32 keep = kActive | (raw ? r & (kShadow | 3) : (e & kShadow) | ((e >> 21) & 3));
    if (array) {
        pending[t] = e;
        pendingEnd[t] = (e & 0x1FFFFF) + sizeWords;
        pendingSize[t] = sizeWords;
        r = keep | kind | (e & kCount) | a;
    } else {
        OlWrite(a, e);
        r = keep | (a + 4);
    }
}

// ---- parameters ----

// The TA has two parameter pointers (ta-probe s6_*, s7_*). TA_ISP_CURRENT is where it puts each polygon (the offset
// of its entries) and moves on by the polygon's size; paramAt is where the words go, and they go to memory only below
// TA_ISP_CURRENT: the rest wait, in order, for a later polygon to move it on, of whatever list (s7_isp_after: the
// register after the polygon; s7_cont_after: TA_LIST_CONT keeps them; s7_reset_after: SOFTRESET and TA_LIST_INIT drop
// them). The two differ only after a polygon whose size is not its number of words: a sprite in a modifier list.
u32 paramAt;
std::vector<u32> paramQueue;        // the words for paramAt on
u32 paramLast;                      // the last word queued

// Words to memory below gate. Up to and with the one at TA_ISP_LIMIT; then "parameters full" and nothing more of
// what waits (ta-tests isp_limit; ta-probe isp_limit_odd: the limit's low bits do not count).
void ParamDrain(u32 gate)
{
    const u32 limit = Reg(TA_ISP_LIMIT_addr) & ~3u;
    size_t i = 0;
    for (; i < paramQueue.size() && paramAt < gate; i++) {
        Wr(paramAt, paramQueue[i]);
        if (paramAt == limit) {
            Raise(holly_PRIM_NOMEM);
            paramQueue.clear();
            return;
        }
        paramAt += 4;
    }
    paramQueue.erase(paramQueue.begin(), paramQueue.begin() + (ptrdiff_t)i);
}

// The skip an ISP word gives: Texture (bit 25), Offset (24), 16-bit UV (22) (ta-probe s6_dx*, s7_dx160, s7_dx200,
// s7_dxneg).
u32 SkipOf(u32 isp)
{
    return (isp >> 25) & 1 ? ((isp >> 22) & 1 ? 1u : 2u) + 1 + ((isp >> 24) & 1) : 1;
}

void ListStart(int type)
{
    const u32 alloc = Reg(TA_ALLOC_CTRL_addr), clip = Reg(TA_GLOB_TILE_CLIP_addr);
    const u32 code = (alloc >> (4 * type)) & 3;
    list = type;
    listBase = matrixBase[type];
    // read when the list begins: its block size (size 0: 8 word blocks, ta-tests opb_none) and direction
    // (ta-probe alloc_change, mode_change), and the grid (ta-probe clip_change)
    blockWords = code ? 8u << (code - 1) : 8;
    down = (alloc >> 20) & 1;
    gw = (int)(clip & 0x3F) + 1;
    gh = (int)((clip >> 16) & 0xF) + 1;
    // with either count at its largest (64 wide, 16 high) the end of the list writes nothing: no open arrays, no
    // end words (ta-probe grid_*x16, grid_64x*, grid_max_y_stack)
    noFlush = gw == 64 || gh == 16;
    for (int t = 0; t < gw * gh && t < kTileRegs; t++)
        TileReg(t) &= ~kActive;
    Reg(TA_NEXT_OPB_addr) = (down ? nextDown : nextUp) >> 2;
}

void EndList()
{
    const int n = gw * gh;
    if (!noFlush)
        for (int t = 0; t < n; t++)
            OlWrite(t < kTileRegs ? CloseTile(t) : Slot(t), kEnd);
    Drain(slotQueue.size() + blockQueue.size());   // what still waits, with each tile's open array and end word
    for (int t = 0; t < n && t < kTileRegs; t++)
        TileReg(t) &= ~kActive;
    Raise(kListDone[list]);
    list = -1;
    haveHeader = false;
}

// ---- polygons ----

Bounds ClipRectOnGrid()
{
    Bounds c = clipRect;
    if (c.x2 > gw - 1) c.x2 = gw - 1;
    if (c.y2 > gh - 1) c.y2 = gh - 1;
    return c;
}

bool InRect(const Bounds& c, int x, int y) { return x >= c.x1 && x <= c.x2 && y >= c.y1 && y <= c.y2; }

// A polygon: n vertices of the current header; ends: the last polygon of its strip.
void Commit(const Vtx* vs, int n, bool ends = true)
{
    int tx[8], ty[8];
    for (int i = 0; i < n; i++) {
        tx[i] = TileOf(vs[i].bx);
        ty[i] = TileOf(vs[i].by);
    }
    // the bounds of each triangle (a sprite: A B C and B C D), and of all
    const int ntri = hdr.kind == kSprite ? 2 : n - 2;
    Bounds tb[6], pb;
    for (int i = 0; i < ntri; i++) {
        tb[i] = { tx[i], ty[i], tx[i], ty[i] };
        for (int k = 1; k < 3; k++)
            tb[i] = Union(tb[i], { tx[i + k], ty[i + k], tx[i + k], ty[i + k] });
        pb = i ? Union(pb, tb[i]) : tb[i];
    }
    if (pb.x1 < 0) pb.x1 = 0;
    if (pb.x2 > gw - 1) pb.x2 = gw - 1;
    if (pb.y1 < 0) pb.y1 = 0;
    if (pb.y2 > gh - 1) pb.y2 = gh - 1;
    // One box of bounds for modifier volumes: each triangle adds its own; a triangle whose header has the Volume
    // bit (the last of a volume) goes to the tiles of the whole box, and the triangle after it starts a new box
    // (ta-tests modvol_volume_bit; the volume instruction in the ISP word does not count, modvol_bounds and
    // ta-probe mvleak_volumes). The first other polygon after modifier volumes, of whatever list, takes an open box
    // as its own too, before culling and clipping, and closes it (ta-tests all_lists; ta-probe mvleak_*).
    if (hdr.kind == kModvol || hdr.inModvolList) {      // a sprite in a modifier list adds to it too (sprite_in_mv)
        volAcc = volFresh ? pb : Union(volAcc, pb);
        volFresh = hdr.volume;
        if (hdr.volume)
            pb = volAcc;
    } else if (!volFresh) {
        pb = Union(pb, volAcc);
        volFresh = true;
    }
    const bool single = pb.x1 == pb.x2 && pb.y1 == pb.y2;   // before the user clip (ta-probe uclip_inside_single)
    const Bounds cr = ClipRectOnGrid();
    // off the grid: nothing, whatever the clip mode (ta-probe uclip_mode1; ta-tests fuzz, outside mode)
    if (pb.x2 < 0 || pb.x1 >= gw || pb.y2 < 0 || pb.y1 >= gh)
        return;
    switch (hdr.clipMode) {
    case 2:                         // inside (ta-tests uclip_inside)
        if (pb.x2 < cr.x1 || pb.x1 > cr.x2 || pb.y2 < cr.y1 || pb.y1 > cr.y2)
            return;
        if (pb.x1 < cr.x1) pb.x1 = cr.x1;
        if (pb.x2 > cr.x2) pb.x2 = cr.x2;
        if (pb.y1 < cr.y1) pb.y1 = cr.y1;
        if (pb.y2 > cr.y2) pb.y2 = cr.y2;
        break;
    case 3:                         // outside (ta-tests uclip_outside)
        if (pb.x1 >= cr.x1 && pb.x2 <= cr.x2 && pb.y1 >= cr.y1 && pb.y2 <= cr.y2)
            return;
        break;
    }

    // parameters: a polygon that finds TA_ISP_CURRENT at TA_ISP_LIMIT is dropped (ta-probe isp_limit_at, isp_limit_exact)
    const u32 cur = Reg(TA_ISP_CURRENT_addr), limit = Reg(TA_ISP_LIMIT_addr) & ~3u;
    if (cur == limit) {
        Raise(holly_PRIM_NOMEM);
        return;
    }
    const u32 offset = ((cur - (ispBase & 0x00F00000u)) >> 2) & 0x1FFFFF;
    const size_t at = (cur - paramAt) >> 2;     // the word at TA_ISP_CURRENT among those waiting and these
    if (hdr.kind == kSprite) {
        // x, y, z, the UV (textured), the base colour on the third corner only, the offset colour (textured with
        // offset) likewise; the fourth corner has z 0 and no UV (ta-probe sprite_bits, sprite_bits_sh)
        paramQueue.push_back(hdr.w[0]);
        paramQueue.push_back(hdr.w[1]);
        paramQueue.push_back(hdr.w[2]);
        for (int i = 0; i < 4; i++) {
            paramQueue.push_back(vs[i].x);
            paramQueue.push_back(vs[i].y);
            paramQueue.push_back(i < 3 ? vs[i].z : 0);
            if (hdr.tex)
                paramQueue.push_back(i < 3 ? vs[i].a[0] : 0);
            paramQueue.push_back(i == 2 ? hdr.base : 0);
            if (hdr.offs)
                paramQueue.push_back(i == 2 ? hdr.offsCol : 0);
        }
    } else {
        for (int k = 0; k < hdr.words; k++)
            paramQueue.push_back(hdr.w[k]);
        for (int i = 0; i < n; i++) {
            paramQueue.push_back(vs[i].x);
            paramQueue.push_back(vs[i].y);
            paramQueue.push_back(vs[i].z);
            for (int k = 0; k < (vs[i].xyz ? 0 : hdr.vtxWords); k++)
                paramQueue.push_back(vs[i].a[k]);
        }
    }
    paramLast = paramQueue.back();
    if (at >= paramQueue.size())
        TA_UNMODELLED("TA: TA_ISP_CURRENT %08X is %u words past where this polygon's parameters end; its size is "
                      "taken from the word in memory there, which no console run has shown", cur,
                      (unsigned)(at - paramQueue.size()));
    u32& atWord = at < paramQueue.size() ? paramQueue[at] : *(u32*)(vram + pvr_map32(cur));
    // Its size and skip. In a modifier volume list a skip of 0: a triangle 12 words, a sprite 15 whatever it has
    // (ta-probe s6_*, spr_mv_*). Else the skip the word at TA_ISP_CURRENT gives as an ISP word: the polygon's own ISP
    // word, but once the pointers differ whatever word is there (s6_dx*: a sprite's D.x; s7_strip4: the TCW of the
    // second triangle); with two volumes each vertex has two (s7_two_vol); sprites have four vertices (s7_sprite).
    // Vertices with no header are a triangle, two volumes if the last polygon header had them (Listless; ta-probe
    // i8_one: skip 3 from the word there). A polygon in progress at TA_LIST_INIT counts the vertices after it only
    // (i8_newhdr: 3 + 4 words for one; i8_one: 3 + 12 for the three of the listless way).
    u32 skip = 0;
    if (hdr.kind != kModvol && !hdr.inModvolList)
        skip = SkipOf(atWord);
    const int counted = initMid ? initCount : n;
    initMid = false;
    const u32 size = (u32)hdr.words + (u32)counted * (3 + skip * (hdr.twoVol ? 2 : 1));
    // the single-tile bit goes into the word at TA_ISP_CURRENT (s7_two_tris: a colour of the polygon before it;
    // s7_sprite, s6_dx40: the D.x of a sprite before it; s7_strip4: a TCW)
    atWord = (atWord & ~0x00200000u) | (single ? 0x00200000u : 0);
    const u32 end = cur + 4 * size;
    if (paramAt != cur && cur < limit && end > limit)
        TA_UNMODELLED("TA: a polygon at TA_ISP_CURRENT %08X reaches TA_ISP_LIMIT %08X while its words go to %08X; "
                      "what a console then writes and where TA_ISP_CURRENT stops is not known", cur, limit, paramAt);
    // At the end of a strip, a sprite or a triangle of the listless way, parameters short of the polygon's end: the
    // last word again until they reach it (ta-probe s6_dx40, s7_dx160, s7_sprite, listless_*; i8_newhdr m6: before the
    // end of the list); not where a strip goes on in another polygon (s7_strip4)
    if (ends)
        while (paramAt + 4 * paramQueue.size() < end)
            paramQueue.push_back(paramLast);
    ParamDrain(end);
    // a polygon over TA_ISP_LIMIT ends there (ta-probe isp_limit_odd); one that begins past it goes on (isp_limit_below)
    Reg(TA_ISP_CURRENT_addr) = cur < limit && end > limit ? limit : end;

    // entries: none in mode 1 (ta-tests uclip_reserved), none in the rectangle when outside
    if (hdr.clipMode == 1)
        return;
    u32 e = offset | (hdr.inModvolList ? 0 : skip << 21 | (hdr.shadow ? kShadow : 0));
    if (hdr.kind == kSprite)
        e |= 0xA0000000u;
    else if (hdr.kind == kModvol || n == 3)
        e |= 0x80000000u;
    for (int y = pb.y1; y <= pb.y2; y++)
        for (int x = pb.x1; x <= pb.x2; x++) {
            if (hdr.clipMode == 3 && InRect(cr, x, y))
                continue;
            u32 te = e;
            if (!(e >> 31))         // a strip: a bit for each triangle whose bounds hold the tile (none: a 0 mask,
                for (int i = 0; i < ntri; i++)   // ta-probe mvleak_strip)
                    if (x >= tb[i].x1 && x <= tb[i].x2 && y >= tb[i].y1 && y <= tb[i].y2)
                        te |= 0x40000000u >> i;
            Append(y * gw + x, te, size);
        }
    DrainAfterPolygon();
}

// ---- headers ----

void TakeHeader(const u32* b, bool sprite)
{
    const u32 pcw = b[0];
    const int type = (int)((pcw >> 24) & 7);
    if (type > 4)
        return;                     // list types 5-7: the header is not taken, list open or not, and its vertices
                                    // go the listless way (ta-probe list5, list6, list7, err_list_types)
    if (list < 0)
        ListStart(type);            // else the list type is not looked at (ta-probe err_list_switch)
    const bool uv16 = pcw & 1, gour = (pcw >> 1) & 1, offsBit = (pcw >> 2) & 1, tex = (pcw >> 3) & 1;
    const bool volume = (pcw >> 6) & 1, shadow = (pcw >> 7) & 1;
    Header h = {};
    h.clipMode = (int)((pcw >> 16) & 3);
    h.tex = tex;
    h.offs = tex && offsBit;
    h.shadow = shadow;
    if (pcw & 0x00800000u)          // Group_En: Strip_Len, kept until the next (ta-tests group_en, strip_len)
        maxVertex = kStripLen[(pcw >> 18) & 3];
    if (sprite) {
        // ISP: bit 22 always, texture, offset, no Gouraud; TSP, TCW as sent; skip 1, 2 or 3; the shadow bit in
        // the entry (ta-probe sprite_bits, sprite_bits_sh; ta-tests sprites)
        h.kind = kSprite;
        h.w[0] = (b[1] & 0xFC1FFFFFu) | 0x00400000u | (u32)tex << 25 | (u32)h.offs << 24;
        h.w[1] = b[2];
        h.w[2] = b[3];
        h.words = 3;
        h.base = b[4];
        h.offsCol = b[5];
        face[0] = b[4];             // the sprite's colours are the face colours of intensity mode 2 after it
        face[1] = b[5];             // (ta-tests fuzz: a sprite, then an intensity 2 polygon)
        h.skip = tex ? (h.offs ? 3 : 2) : 1;
        // in a modifier volume list: a quad entry with skip 0 and no shadow bit, and its bounds in the volume box
        // (ta-probe sprite_in_mv)
        h.inModvolList = list == 1 || list == 3;
        hdr = h;
        lastIsp = h.w[0];
        haveHeader = true;
        return;
    }
    if (list == 1 || list == 3) {
        // a modifier volume triangle: ISP with UV16, Gouraud and Texture as polygons have them, TSP and TCW 0,
        // entries with skip 0 and no shadow bit (ta-tests modvol_isp, modvol_volume_bit)
        h.kind = kModvol;
        h.w[0] = (b[1] & 0xFC1FFFFFu) | (pcw & 0xB) << 22 | (u32)h.offs << 24;
        h.w[1] = h.w[2] = 0;
        h.words = 3;
        h.skip = 0;
        h.shadow = false;
        h.volume = volume;
        hdr = h;
        lastIsp = h.w[0];
        haveHeader = true;
        return;
    }
    // a polygon: two volumes with Volume alone, the entry's shadow bit from Shadow alone (ta-tests two_volume_hdrs)
    h.kind = kPoly;
    h.uv16 = tex && uv16;
    h.twoVol = volume;
    h.colType = (int)((pcw >> 4) & 3);
    if (h.twoVol && h.colType == 1)
        h.colType = 3;              // two volumes of float colours: the intensities of the last face colours (ta-tests two_vol_types)
    h.w[0] = (b[1] & 0xFC1FFFFFu) | (u32)uv16 << 22 | (u32)gour << 23 | (u32)tex << 25 | (u32)h.offs << 24;   // ta-tests obj_bits
    h.w[1] = b[2];
    h.w[2] = tex ? b[3] : 0;        // ta-tests tsp_tcw
    h.words = 3;
    if (h.twoVol) {
        h.w[3] = b[4];
        h.w[4] = tex ? b[5] : 0;
        h.words = 5;
    }
    h.skip = tex ? (h.uv16 ? 1u : 2u) + 1 + h.offs : 1;
    h.vtxWords = (int)h.skip * (h.twoVol ? 2 : 1);
    h.vtx64 = tex && (h.twoVol || h.colType == 1);
    hdr = h;
    lastIsp = h.w[0];
    lastTwoVol = h.twoVol;
    lastShadow = h.shadow;
    haveHeader = true;
    if (h.colType == 2) {           // intensity: face colours here (ta-tests col_type_hdrs, int_offset)
        if (h.twoVol || h.offs)
            expect = eHdr2;         // two of them in a second block
        else
            face[0] = FloatColour(b[4], b[5], b[6], b[7]);
    }
}

// ---- vertices ----

// a vertex's words after x, y, z, in the order of the parameters (ta-tests packed_colours, float_sweep_tex,
// tex_packed, int1_sweep_*, int_offset, two_vol_types, combo)
void PolyVertex(const u32* b1, const u32* b2, Vtx& v)
{
    v.x = v.bx = b1[1];
    v.y = v.by = b1[2];
    v.z = b1[3];
    int k = 0;
    const bool intensity = hdr.colType >= 2;
    if (!hdr.twoVol) {
        if (!hdr.tex) {
            v.a[k++] = hdr.colType == 0 ? b1[6] : hdr.colType == 1 ? FloatColour(b1[4], b1[5], b1[6], b1[7]) : Scale(face[0], b1[6]);
            return;
        }
        v.a[k++] = b1[4];
        if (!hdr.uv16)
            v.a[k++] = b1[5];
        if (hdr.colType == 1) {
            v.a[k++] = FloatColour(b2[0], b2[1], b2[2], b2[3]);
            if (hdr.offs)
                v.a[k++] = FloatColour(b2[4], b2[5], b2[6], b2[7]);
        } else {
            v.a[k++] = intensity ? Scale(face[0], b1[6]) : b1[6];
            if (hdr.offs)
                v.a[k++] = intensity ? Scale(face[1], b1[7]) : b1[7];
        }
        return;
    }
    if (!hdr.tex) {
        v.a[k++] = intensity ? Scale(face[0], b1[4]) : b1[4];
        v.a[k++] = intensity ? Scale(face[1], b1[5]) : b1[5];
        return;
    }
    // two volumes, textured: volume 0 in the first block, volume 1 in the second; each volume's colours from its
    // own face colour
    for (int vol = 0; vol < 2; vol++) {
        const u32* s = vol ? b2 - 4 : b1;           // volume 1's words are b2[0..3], read as s[4..7]
        const u32 f = face[vol];
        v.a[k++] = s[4];
        if (!hdr.uv16)
            v.a[k++] = s[5];
        v.a[k++] = intensity ? Scale(f, s[6]) : s[6];
        if (hdr.offs)
            v.a[k++] = intensity ? Scale(f, s[7]) : s[7];
    }
}

// a strip vertex: a polygon at the end of the strip, or each time Strip_Len vertices are there (ta-tests
// strip_len, strip_lengths, strip_parity)
void CommitStrip(int n, bool ends)
{
    if (openHdrValid) {             // begun before TA_LIST_INIT and a header after it (ta-probe i8_newhdr)
        const Header next = hdr;
        hdr = openHdr;
        Commit(strip, n, ends);
        hdr = next;
        openHdrValid = false;
    } else {
        Commit(strip, n, ends);
    }
}

void StripVertex(const Vtx& v, bool eos)
{
    strip[nv++] = v;
    initCount++;
    if (eos) {
        if (nv < 3) {
            Raise(holly_ILLEGAL_PARAM);     // and the strip is dropped (ta-probe err_short_strip)
            initMid = openHdrValid = false;
        } else {
            CommitStrip(nv, true);
        }
        nv = 0;
        parity = 0;
    } else if (nv == (int)maxVertex) {
        CommitStrip(nv, false);
        if (nv == 3) {
            if (parity == 0)
                strip[0] = strip[2];
            else
                strip[1] = strip[2];
            parity ^= 1;
        } else {
            strip[0] = strip[nv - 2];
            strip[1] = strip[nv - 1];
            parity = 0;
        }
        nv = 2;
    }
}

void Vertex64(const u32* b1, const u32* b2)
{
    if (hdr.kind == kSprite) {      // committed with or without end of strip (ta-tests sprite_no_eos)
        Vtx c[4] = {};
        const u32 xyz[12] = { b1[1], b1[2], b1[3], b1[4], b1[5], b1[6], b1[7], b2[0], b2[1], b2[2], b2[3], 0 };
        for (int i = 0; i < 4; i++) {
            c[i].x = c[i].bx = xyz[3 * i];
            c[i].y = c[i].by = xyz[3 * i + 1];
            c[i].z = xyz[3 * i + 2];
            c[i].a[0] = i < 3 ? b2[5 + i] : 0;   // UVs of A, B, C
        }
        Commit(c, 4);
        return;
    }
    if (hdr.kind == kModvol) {
        Vtx c[3] = {};
        const u32 xyz[9] = { b1[1], b1[2], b1[3], b1[4], b1[5], b1[6], b1[7], b2[0], b2[1] };
        for (int i = 0; i < 3; i++) {
            c[i].x = c[i].bx = xyz[3 * i];
            c[i].y = c[i].by = xyz[3 * i + 1];
            c[i].z = xyz[3 * i + 2];
        }
        Commit(c, 3);
        return;
    }
    Vtx v = {};
    PolyVertex(b1, b2, v);
    StripVertex(v, (b1[0] >> 28) & 1);
}

// A vertex with no header since the list began (or since TA_LIST_INIT), or with no list open (which then opens the
// opaque list, whatever the list before: listless_modvol): taken as a modifier volume vertex is, two blocks (so that a
// header or an end of list in the second is not seen as one), nine words: three x, y, z. Written as a triangle: the
// last header's ISP word, TSP and TCW 0, the nine words, and the last of them again to its end; its size and skip as
// any polygon's from the word where it goes, two volumes as the last polygon header had (ta-probe i8_one: skip 3); its
// entries a triangle array with the last polygon header's shadow bit (ta-probe err_vertex_first, err_vertex_after_eol,
// listless_*, list5-7, i8_zero, i8_done).
// After TA_LIST_INIT in the middle of a strip, the three go on the end of the strip, which then ends: one polygon of
// all its triangles, its own header and vertices, then the nine words, its size counting the three (ta-probe i8_one,
// i8_after1, init_mid_one). Where the first block did not end the strip a console goes on in a way not modelled
// (i8_two, i8_after1: the next polygons repeat the nine words and the vertex before them).
void Listless(const u32* b1, const u32* b2)
{
    if (list < 0)
        ListStart(0);
    const u32 nine[9] = { b1[1], b1[2], b1[3], b1[4], b1[5], b1[6], b1[7], b2[0], b2[1] };
    Vtx out[3] = {};
    for (int i = 0; i < 3; i++) {
        out[i].x = out[i].bx = nine[3 * i];
        out[i].y = out[i].by = nine[3 * i + 1];
        out[i].z = nine[3 * i + 2];
        out[i].xyz = true;
    }
    if (nv > 0) {
        if (!((b1[0] >> 28) & 1))
            TA_UNMODELLED("TA: after TA_LIST_INIT in the middle of a strip, a vertex (control word %08X) that does "
                          "not end the strip: a console then writes more polygons, repeating these parameters and "
                          "binned on earlier triangles, which refta does not do", b1[0]);
        for (int i = 0; i < 3; i++)
            strip[nv++] = out[i];
        initCount += 3;
        CommitStrip(nv, true);
        nv = 0;
        parity = 0;
        return;
    }
    const Header saved = hdr;
    Header h = {};
    h.kind = kPoly;
    h.listless = true;
    h.w[0] = lastIsp;
    h.words = 3;
    h.shadow = lastShadow;
    h.twoVol = lastTwoVol;
    hdr = h;
    Commit(out, 3);
    hdr = saved;
}

// Object list set (parameter type 2): word 1 as an entry in each tile of the rectangle in words 4-7 (read as a user
// clip's), after what the tile has (ta-probe olset_a, olset_b; all zero, lock_type2_zero: 0 into tile (0, 0)).
void ObjectListSet(const u32* b)
{
    if (list < 0)
        ListStart(0);
    int x1 = (int)(b[4] & 0x3F), y1 = (int)(b[5] & 0xF), x2 = (int)(b[6] & 0x3F), y2 = (int)(b[7] & 0xF);
    if (x2 > gw - 1) x2 = gw - 1;
    if (y2 > gh - 1) y2 = gh - 1;
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            Append(y * gw + x, b[1], 0, true);
    DrainAfterPolygon();
}

// ---- input ----

void Block(const u32* b)
{
    if (!inited || stopped)
        return;
    if (expect == eHdr2) {
        expect = eNone;
        face[0] = FloatColour(b[0], b[1], b[2], b[3]);
        face[1] = FloatColour(b[4], b[5], b[6], b[7]);
        return;
    }
    if (expect == eVtx2) {
        expect = eNone;
        Vertex64(firstHalf, b);
        return;
    }
    if (expect == eListless2) {
        expect = eNone;
        Listless(firstHalf, b);
        return;
    }
    if (spriteMid == 2) {           // the block after the vertex that ended the strip is dropped; the sprite header
        spriteMid = 0;              // then applies (ta-probe sprite_hdr_mid: the next vertices are sprites)
        TakeHeader(spriteHdr, true);
        return;
    }
    switch (b[0] >> 29) {
    case 7:
        if (list < 0 || !haveHeader) {
            for (int i = 0; i < 8; i++)
                firstHalf[i] = b[i];
            expect = eListless2;
            break;
        }
        if (spriteMid == 1) {       // after a sprite header in the strip: the strip's vertex is this block's first
            Vtx v = {};             // word in every place (ta-probe sprite_hdr_mid, sprite_mid_clip, sprite_mid_neg)
            v.x = v.y = v.z = b[1];
            v.bx = b[1];            // (but its tiles from its x and y: sprite_mid_clip)
            v.by = b[2];
            for (int k = 0; k < 8; k++)
                v.a[k] = b[1];
            StripVertex(v, (b[0] >> 28) & 1);
            spriteMid = 2;
            break;
        }
        if (hdr.kind != kPoly || hdr.vtx64) {
            for (int i = 0; i < 8; i++)
                firstHalf[i] = b[i];
            expect = eVtx2;
        } else {
            Vtx v = {};
            PolyVertex(b, b, v);
            StripVertex(v, (b[0] >> 28) & 1);
        }
        break;
    case 0:                         // end of list; in the middle of a strip not looked at (ta-probe err_eol_in_strip).
        if (nv == 0) {              // With no list open the opaque list, ended at once: an end word at the start of
            if (list < 0)           // each tile (ta-probe init_one, init_hdr: their second end of list)
                ListStart(0);
            EndList();
        }
        break;
    case 1:                         // user clip, also in the middle of a strip (ta-probe err_clip_in_strip; ta-tests uclip_bounds)
        clipRect = { (int)(b[4] & 0x3F), (int)(b[5] & 0xF), (int)(b[6] & 0x3F), (int)(b[7] & 0xF) };
        break;
    case 4:                         // in the middle of a strip not looked at (ta-probe err_hdr_in_strip), but after
        if (nv == 0) {              // TA_LIST_INIT taken, for the polygons after the one in progress (i8_newhdr)
            TakeHeader(b, false);
        } else if (!haveHeader) {
            if (!openHdrValid)
                openHdr = hdr;
            openHdrValid = true;
            TakeHeader(b, false);
        }
        break;
    case 5:                         // a sprite header in the middle of a strip waits for the strip's end
        if (nv == 0) {
            TakeHeader(b, true);
        } else {
            if (initMid)
                TA_UNMODELLED("TA: a sprite header in the middle of a strip after TA_LIST_INIT, not tried on a "
                              "console: taken as a sprite header in a strip is");
            for (int i = 0; i < 8; i++)
                spriteHdr[i] = b[i];
            spriteMid = 1;
        }
        break;
    case 2:
        if (nv == 0)
            ObjectListSet(b);
        break;
    default:                        // 3, 6: the TA takes nothing more (ta-probe lock_type3, lock_type6)
        TA_LOCKUP("TA: parameter type %u (control word %08X) stops a console's tile accelerator: it takes no more "
                  "data until SOFTRESET, and the CPU's next write to it does not end", b[0] >> 29, b[0]);
        stopped = true;
        break;
    }
}

void LatchBases()
{
    // TA_OL_BASE and TA_NEXT_OPB_INIT in 32 bytes (ta-probe ol_base_odd); the matrix of each list from
    // TA_ALLOC_CTRL and TA_GLOB_TILE_CLIP as they are now (ta-probe clip_change, alloc_change_op)
    const u32 alloc = Reg(TA_ALLOC_CTRL_addr), clip = Reg(TA_GLOB_TILE_CLIP_addr);
    const u32 tiles = ((clip & 0x3F) + 1) * (((clip >> 16) & 0xF) + 1);
    u32 b = Reg(TA_OL_BASE_addr) & 0x00FFFFE0u;
    for (int l = 0; l < 5; l++) {
        const u32 code = (alloc >> (4 * l)) & 3;
        matrixBase[l] = b;
        if (code)
            b += tiles * (8u << (code - 1)) * 4;
    }
}

}   // namespace

// SOFTRESET bit 0: no list, the user clip rectangle tile (0, 0), Strip_Len 3, no modifier volume bounds, input
// taken again, TA_ISP_CURRENT 0; face colours and TA_OL_POINTERS are kept (ta-probe uclip_after_reset, strip_len_reset,
// mvleak_reset, int2_reset, olp_reset)
void Reset()
{
    inited = false;
    stopped = false;
    list = -1;
    haveHeader = false;
    spriteMid = 0;
    nv = parity = 0;
    initMid = openHdrValid = false;
    expect = eNone;
    maxVertex = 3;
    clipRect = { 0, 0, 0, 0 };
    volFresh = true;
    slotQueue.clear();
    blockQueue.clear();
    paramQueue.clear();
    paramAt = 0;
    Reg(TA_ISP_CURRENT_addr) = 0;   // (ta-probe olp_reset m0)
}

// TA_LIST_INIT: the matrices and both block pointers anew, TA_ISP_CURRENT from TA_ISP_BASE. A strip or a list in
// progress goes on (ta-probe err_init_in_strip).
void ListInit(uint8_t* v)
{
    vram = v;
    inited = true;
    LatchBases();
    ispBase = Reg(TA_ISP_BASE_addr);
    Reg(TA_ISP_CURRENT_addr) = ispBase;
    paramQueue.clear();
    paramAt = ispBase;
    // vertices after it go the listless way until a header (ta-probe i8_zero, i8_done); a strip in progress counts
    // from here (i8_*)
    haveHeader = false;
    initMid = nv > 0;
    initCount = 0;
    const u32 opCode = Reg(TA_ALLOC_CTRL_addr) & 3;
    downUnit = (opCode ? 8u << (opCode - 1) : 8) * 4;
    nextUp = Reg(TA_NEXT_OPB_INIT_addr) & 0x00FFFFE0u;
    nextDown = nextUp - downUnit;
    Reg(TA_NEXT_OPB_addr) = ((Reg(TA_ALLOC_CTRL_addr) >> 20) & 1 ? nextDown : nextUp) >> 2;   // ta-probe down_noalloc
}

// TA_LIST_CONT: TA_OL_BASE again; TA_ISP_CURRENT and the block pointers go on (ta-probe list_cont_base,
// list_cont_alloc). A list sent again begins anew (ta-tests list_cont, ta-probe list_cont_same).
void ListCont()
{
    LatchBases();
}

void Write(const uint32_t* words, uint32_t count)
{
    for (uint32_t i = 0; i + 8 <= count; i += 8)
        Block(words + i);
}

}   // namespace refta
