/*
    refsw3-offline: refsw3 without the rest of the emulator: refsw3-core (refsw3/refcore, the ISP and TSP) and
    refsw3-ta (refsw3/refta, the tile accelerator).

    It starts with all registers and all of video memory at zero and does what a stream of commands tells it:
    register writes, video memory writes, data for the tile accelerator, "render", "write what is in the
    framebuffer to a file". The same commands can be played on a console (dreamcast-tests/common/pvrio.h has the
    two ends: a program built for the PC prints them, the same program built for KallistiOS does them on the
    hardware), so that a picture from here and a picture from a console come from exactly the same input.

    Nothing is initialized behind the caller's back: a render that reads a register, a fog table entry or a palette entry nobody wrote is an error
    (--lax: a warning). The values a console has under KallistiOS are in dreamcast-tests/common/pvrx.h.

    usage: refsw3-offline [options] [script ...]
        script                 a file of commands; - is standard input (the default without script and -c)
        -c "cmd; cmd"          commands on the command line
        --dump <folder> [png]  render a dump made by the emulator (vram.bin and pvr_regs.bin) and write the
                               displayed picture to <folder>/frame.png or to [png]
        --lax                  an unwritten register is a warning
        -v                     say what is done

    Commands, one per line; # starts a comment; every number is hexadecimal, with or without 0x:
        reset                        registers and video memory back to zero
        reg <name|offset> <value>    write a register the way the CPU does (TA_LIST_INIT, SOFTRESET and
                                     STARTRENDER do what they do in the emulator)
        w32 <address> <word>...      write words to video memory, 32 bit area addresses (0x05000000 on a console)
        w64 <address> <word>...      the same, 64 bit area addresses (textures; 0x04000000 on a console)
        ta <word>...                 send to the tile accelerator; a multiple of 8 words
        render                       reg STARTRENDER ffffffff
        wait <events>                nothing here (a console waits for these interrupts)
        dump32 <address> <words> <file>   words of the 32 bit area to a file, as they are
        fill32 <address> <words> <value>   the same word over and over, 32 bit area
        tahash <address> <words> <name>   CRC-32 of the area and of tastate's four words: "PVRIO hash <name> <crc>"
        tastate <file>               four words to a file: TA_ISP_CURRENT, TA_NEXT_OPB, the list-done interrupts
                                     (SB_ISTNRM bits 7-10, 21) and the error interrupts (SB_ISTERR) raised
                                     since the last tastate (dreamcast-tests/ta-tests/ta-tests)
        regdump <offset> <words> <file>   registers (offsets from 0x005f8000) to a file, as they read
        scan32 <address> <words> <value> <name>   every word of the 32 bit area that is not value:
                                     "PVRIO scan <name> <address> <word>" (at most 64), "PVRIO scan <name> count <n>"
        png <file>                   the displayed picture (FB_R_*) as a PNG
        log <file> [x,y ...] | log off   the render log of the following renders (of those tiles only)
        regs <file>                  load a raw register block (pvr_regs.bin: 8 KB, 0x005F8000-0x005F9FFF; the 32 KB
                                     block of older builds is read for its first 8 KB); every register counts as written
        vram <file>                  load raw video memory in 32 bit area order (vram.bin)
        save <folder>                write vram.bin and pvr_regs.bin, as the emulator's dump does
        echo <text>
*/
#include "license/bsd"
#include "types.h"
#include "hw/pvr/pvr_regs.h"
#include "hw/pvr/pvr_mem.h"
#include "refta/refta.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace refcore {
void RenderCORE();
extern FILE* rendlog;
extern std::vector<u32> rendlog_tiles;
u8* emu_vram;           // refsw3/refcore/refcore.cpp (the emulator's backend, not built here) has it otherwise
}
using refcore::RenderCORE;
using refcore::rendlog;
using refcore::rendlog_tiles;

u8 pvr_regs[pvr_RegSize];
bool pal_needs_update, fog_needs_update;
// hw/pvr/pvr_mem.cpp's TA writes: refsw3-ta's (this program sends to refta itself); the emulator's own TA is not here
bool ta_lle = true;
void ta_vtx_data(u32* data, u32 size) {}
void DYNACALL ta_vtx_data32(void* data) {}

static u8* vram;
static bool written[pvr_RegSize / 4];
static bool lax, verbose;
static int problems;
static u32 events_normal, events_error;
static u32 seen_normal, seen_error;        // since the last tastate
static int renders;

// ---- what the emulator provides to the renderer core and the tile accelerator -------------------------

int msgboxf(const wchar* text, unsigned int type, ...) {
    va_list args;
    va_start(args, type);
    vfprintf(stderr, text, args);
    va_end(args);
    fputc('\n', stderr);
    return 0;
}

void os_DebugBreak() {
    fprintf(stderr, "refsw3-offline: the renderer stopped\n");
    exit(3);
}

void asic_RaiseInterrupt(HollyInterruptID inter) {
    u32 bit = 1u << ((u32)inter & 31);
    if (((u32)inter & ~31u) == (u32)holly_err) {
        events_error |= bit;
        seen_error |= bit;
        fprintf(stderr, "refsw3-offline: the tile accelerator reports error %d (2: out of parameter memory, "
                        "3: out of object memory, 4: illegal parameter)\n", (int)((u32)inter & 31));
        problems++;
    } else {
        events_normal |= bit;
        seen_normal |= bit;
    }
}

f32 f16(u16 v) {
    u32 z = v << 16;
    return *(f32*)&z;
}

// ---- registers ----------------------------------------------------------------------------------------

struct RegName { const char* name; u32 addr; };
#define R(x) { #x, x##_addr }
static const RegName reg_names[] = {
    R(ID), R(REVISION), R(SOFTRESET), R(STARTRENDER), R(TEST_SELECT), R(PARAM_BASE), R(REGION_BASE), R(SPAN_SORT_CFG),
    R(VO_BORDER_COL), R(FB_R_CTRL), R(FB_W_CTRL), R(FB_W_LINESTRIDE), R(FB_R_SOF1), R(FB_R_SOF2), R(FB_R_SIZE),
    R(FB_W_SOF1), R(FB_W_SOF2), R(FB_X_CLIP), R(FB_Y_CLIP), R(FPU_SHAD_SCALE), R(FPU_CULL_VAL), R(FPU_PARAM_CFG),
    R(HALF_OFFSET), R(FPU_PERP_VAL), R(ISP_BACKGND_D), R(ISP_BACKGND_T), R(ISP_FEED_CFG), R(SDRAM_REFRESH),
    R(SDRAM_ARB_CFG), R(SDRAM_CFG), R(FOG_COL_RAM), R(FOG_COL_VERT), R(FOG_DENSITY), R(FOG_CLAMP_MAX),
    R(FOG_CLAMP_MIN), R(SPG_TRIGGER_POS), R(SPG_HBLANK_INT), R(SPG_VBLANK_INT), R(SPG_CONTROL), R(SPG_HBLANK),
    R(SPG_LOAD), R(SPG_VBLANK), R(SPG_WIDTH), R(TEXT_CONTROL), R(VO_CONTROL), R(VO_STARTX), R(VO_STARTY),
    R(SCALER_CTL), R(PAL_RAM_CTRL), R(SPG_STATUS), R(FB_BURSTCTRL), R(FB_C_SOF), R(Y_COEFF), R(PT_ALPHA_REF),
    R(TA_OL_BASE), R(TA_ISP_BASE), R(TA_OL_LIMIT), R(TA_ISP_LIMIT), R(TA_NEXT_OPB), R(TA_ISP_CURRENT),
    R(TA_GLOB_TILE_CLIP), R(TA_ALLOC_CTRL), R(TA_LIST_INIT), R(TA_YUV_TEX_BASE), R(TA_YUV_TEX_CTRL),
    R(TA_YUV_TEX_CNT), R(TA_LIST_CONT), R(TA_NEXT_OPB_INIT),
};
#undef R

// what a render, a list initialization and the display read; the hardware uses all of them even where refsw2
// does not yet
static const u32 render_reads[] = {
    PARAM_BASE_addr, REGION_BASE_addr, SPAN_SORT_CFG_addr, FB_W_CTRL_addr, FB_W_LINESTRIDE_addr, FB_W_SOF1_addr,
    FB_W_SOF2_addr, FB_X_CLIP_addr, FB_Y_CLIP_addr, FPU_SHAD_SCALE_addr, FPU_CULL_VAL_addr, FPU_PARAM_CFG_addr,
    HALF_OFFSET_addr, FPU_PERP_VAL_addr, ISP_BACKGND_D_addr, ISP_BACKGND_T_addr, ISP_FEED_CFG_addr,
    FOG_COL_RAM_addr, FOG_COL_VERT_addr, FOG_DENSITY_addr, FOG_CLAMP_MAX_addr, FOG_CLAMP_MIN_addr,
    TEXT_CONTROL_addr, SCALER_CTL_addr, PAL_RAM_CTRL_addr, PT_ALPHA_REF_addr,
};
static const u32 list_init_reads[] = {
    TA_OL_BASE_addr, TA_ISP_BASE_addr, TA_OL_LIMIT_addr, TA_ISP_LIMIT_addr, TA_GLOB_TILE_CLIP_addr,
    TA_ALLOC_CTRL_addr, TA_NEXT_OPB_INIT_addr,
};
static const u32 display_reads[] = { FB_R_CTRL_addr, FB_R_SIZE_addr, FB_R_SOF1_addr, SPG_CONTROL_addr };

static const char* reg_name(u32 addr) {
    static char buf[16];
    for (auto& r : reg_names)
        if (r.addr == addr)
            return r.name;
    snprintf(buf, sizeof(buf), "%04X", addr);
    return buf;
}

static void problem(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "refsw3-offline: %s: ", lax ? "warning" : "error");
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    if (!lax)
        problems++;
}

template <size_t N>
static void check_written(const u32 (&list)[N], const char* what) {
    for (u32 addr : list)
        if (!written[addr / 4])
            problem("%s reads %s, which was never written", what, reg_name(addr));
}

static bool told[pvr_RegSize / 4];

static void reset_all() {
    memset(told, 0, sizeof(told));
    memset(pvr_regs, 0, sizeof(pvr_regs));
    memset(vram, 0, VRAM_SIZE);
    memset(written, 0, sizeof(written));
    ID = 0x17FD11DB;
    REVISION = 0x00000011;
    events_normal = events_error = 0;
    seen_normal = seen_error = 0;
    refta::Reset();
}

// refsw3-core calls this for every fog table and palette entry a render uses (refsw3/refcore/refsw_tile.cpp).
void refsw3_offline_read(unsigned int addr) {
    addr &= pvr_RegMask & ~3u;
    if (written[addr / 4] || told[addr / 4])
        return;
    told[addr / 4] = true;
    if (addr >= PALETTE_RAM_START_addr)
        problem("the render reads palette entry %u, which was never written", (addr - PALETTE_RAM_START_addr) / 4);
    else
        problem("the render reads fog table entry %u, which was never written", (addr - FOG_TABLE_START_addr) / 4);
}

// The renderer walks the region array until an entry says it is the last one: make sure there is one.
static bool region_array_ends() {
    u32 base = REGION_BASE & 0x00FFFFFC;
    u32 step = (FPU_PARAM_CFG.full & (1 << 21)) ? 24 : 20;
    for (int n = 0; n < 8192; n++, base += step)
        if (vri(vram, base) >> 31)
            return true;
    fprintf(stderr, "refsw3-offline: error: the region array at %06X has no last entry\n", REGION_BASE & 0x00FFFFFC);
    problems++;
    return false;
}

// A register write as the emulator's PVRDevice::Write does it (hw/pvr/pvr_sb_regs.cpp).
static void reg_write(u32 addr, u32 data) {
    addr &= pvr_RegMask & ~3u;
    if (addr == ID_addr || addr == REVISION_addr || addr == TA_YUV_TEX_CNT_addr)
        return;
    if (verbose)
        printf("reg %s = %08X\n", reg_name(addr), data);
    if (addr == STARTRENDER_addr) {
        int before = problems;
        check_written(render_reads, "the render");
        if (problems != before || !region_array_ends()) {
            fprintf(stderr, "refsw3-offline: not rendering\n");
            return;
        }
        RenderCORE();
        renders++;
        events_normal |= 7;     // render done: video, ISP, TSP
        return;
    }
    if (addr == TA_LIST_INIT_addr) {
        TA_ISP_CURRENT = TA_ISP_BASE;
        if (data >> 31) {
            check_written(list_init_reads, "the list initialization");
            refta::ListInit(vram);
            data = 0;
        }
    }
    if (addr == SOFTRESET_addr) {
        if (data & 1)
            refta::Reset();
        data = 0;
    }
    if (addr == TA_LIST_CONT_addr && (data >> 31)) {
        refta::ListCont();
        data = 0;
    }
    PvrReg(addr, u32) = data;
    written[addr / 4] = true;
}

// ---- files --------------------------------------------------------------------------------------------

// The register area's 8 KB (0x005F8000-0x005F9FFF); emulator builds before 2026-11-17 wrote 32 KB, of which the
// first 8 KB are these
static bool load_regs(const char* path) {
    FILE* f = fopen(path, "rb");
    long size = -1;
    if (f && fseek(f, 0, SEEK_END) == 0) {
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
    }
    if (!f || (size != (long)sizeof(pvr_regs) && size != 0x8000) || fread(pvr_regs, 1, sizeof(pvr_regs), f) != sizeof(pvr_regs)) {
        fprintf(stderr, "refsw3-offline: %s is not a register block of %u bytes (or of the 32 KB of older builds)\n", path,
                (unsigned)sizeof(pvr_regs));
        if (f) fclose(f);
        return false;
    }
    fclose(f);
    memset(written, 1, sizeof(written));
    return true;
}

// the emulator's dump is in 32 bit area order
static bool load_vram(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "refsw3-offline: cannot open %s\n", path);
        return false;
    }
    for (u32 i = 0; i < VRAM_SIZE; i += 4)
        if (fread(vrp(vram, i), 4, 1, f) != 1) {
            fprintf(stderr, "refsw3-offline: %s is shorter than video memory (%u bytes)\n", path, (unsigned)VRAM_SIZE);
            fclose(f);
            return false;
        }
    fclose(f);
    return true;
}

static bool save_dump(const std::string& folder) {
    FILE* f = fopen((folder + "/vram.bin").c_str(), "wb");
    if (!f) {
        fprintf(stderr, "refsw3-offline: cannot write into %s\n", folder.c_str());
        return false;
    }
    for (u32 i = 0; i < VRAM_SIZE; i += 4)
        fwrite(vrp(vram, i), 4, 1, f);
    fclose(f);
    f = fopen((folder + "/pvr_regs.bin").c_str(), "wb");
    if (!f)
        return false;
    fwrite(pvr_regs, 1, sizeof(pvr_regs), f);
    fclose(f);
    return true;
}

static bool dump32(u32 addr, u32 words, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "refsw3-offline: cannot write %s\n", path);
        return false;
    }
    std::vector<u32> buf(words);
    for (u32 i = 0; i < words; i++)
        buf[i] = vri(vram, addr + i * 4);
    fwrite(buf.data(), 4, words, f);
    fclose(f);
    return true;
}

// ---- PNG, without a compression library: stored blocks --------------------------------------------------

static u32 crc_of(const u8* p, size_t n, u32 c = 0) {
    static u32 table[256];
    if (!table[1])
        for (u32 i = 0; i < 256; i++) {
            u32 v = i;
            for (int k = 0; k < 8; k++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
    c = ~c;
    while (n--)
        c = table[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static void put32(std::vector<u8>& v, u32 x) {
    for (int s = 24; s >= 0; s -= 8)
        v.push_back(u8(x >> s));
}

static void chunk(FILE* f, const char* type, const std::vector<u8>& data) {
    std::vector<u8> c(type, type + 4);
    c.insert(c.end(), data.begin(), data.end());
    std::vector<u8> head, tail;
    put32(head, (u32)data.size());
    put32(tail, crc_of(c.data(), c.size()));
    fwrite(head.data(), 1, 4, f);
    fwrite(c.data(), 1, c.size(), f);
    fwrite(tail.data(), 1, 4, f);
}

static bool write_png(const char* path, const std::vector<u8>& rgb, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "refsw3-offline: cannot write %s\n", path);
        return false;
    }
    std::vector<u8> raw;
    raw.reserve((size_t)h * (w * 3 + 1));
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3);
    }
    u32 a = 1, b = 0;
    for (u8 v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    std::vector<u8> z = { 0x78, 0x01 };
    for (size_t at = 0; at < raw.size() || at == 0;) {
        size_t n = raw.size() - at < 65535 ? raw.size() - at : 65535;
        z.push_back(at + n == raw.size());
        z.push_back(u8(n)); z.push_back(u8(n >> 8)); z.push_back(u8(~n)); z.push_back(u8(~n >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        at += n;
        if (n == 0)
            break;
    }
    put32(z, (b << 16) | a);
    std::vector<u8> head;
    put32(head, w); put32(head, h);
    head.insert(head.end(), { 8, 2, 0, 0, 0 });
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    chunk(f, "IHDR", head);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    fclose(f);
    return true;
}

// What the display shows (refsw2.cpp, Present), as RGB.
static bool write_display(const char* path) {
    check_written(display_reads, "the display");
    int width = (FB_R_SIZE.fb_x_size + 1) << 1;      // in 16 bit words
    int height = FB_R_SIZE.fb_y_size + 1;
    int modulus = (FB_R_SIZE.fb_modulus - 1) << 1;
    int bpp = 2;
    if (FB_R_CTRL.fb_depth == fbde_888) {
        bpp = 3;
        width = (width * 2) / 3;
        modulus = (modulus * 2) / 3;
    } else if (FB_R_CTRL.fb_depth == fbde_C888) {
        bpp = 4;
        width /= 2;
        modulus /= 2;
    }
    bool interlace = SPG_CONTROL.interlace;
    int fields = interlace ? 2 : 1;
    int concat = FB_R_CTRL.fb_concat;
    std::vector<u8> rgb((size_t)width * height * fields * 3);
    for (int field = 0; field < fields; field++) {
        u32 addr = field ? FB_R_SOF2 : FB_R_SOF1;
        for (int y = 0; y < height; y++) {
            u8* dst = &rgb[(size_t)(y * fields + field) * width * 3];
            for (int i = 0; i < width; i++, addr += bpp, dst += 3) {
                if (FB_R_CTRL.fb_depth == fbde_0555) {
                    u16 s = pvr_read_area1_16(vram, addr);
                    dst[0] = (((s >> 10) & 0x1F) << 3) + concat;
                    dst[1] = (((s >> 5) & 0x1F) << 3) + concat;
                    dst[2] = ((s & 0x1F) << 3) + concat;
                } else if (FB_R_CTRL.fb_depth == fbde_565) {
                    u16 s = pvr_read_area1_16(vram, addr);
                    dst[0] = (((s >> 11) & 0x1F) << 3) + concat;
                    dst[1] = (((s >> 5) & 0x3F) << 2) + (concat >> 1);
                    dst[2] = ((s & 0x1F) << 3) + concat;
                } else if (FB_R_CTRL.fb_depth == fbde_888) {
                    u32 s = (addr & 1) ? pvr_read_area1_32(vram, addr - 1) : pvr_read_area1_32(vram, addr) >> 8;
                    dst[0] = s >> 16; dst[1] = s >> 8; dst[2] = s;
                } else {
                    u32 s = pvr_read_area1_32(vram, addr);
                    dst[0] = s >> 16; dst[1] = s >> 8; dst[2] = s;
                }
            }
            addr += modulus * bpp;
        }
    }
    return write_png(path, rgb, width, height * fields);
}

// ---- commands -----------------------------------------------------------------------------------------

static bool hex(const std::string& s, u32* out) {
    const char* p = s.c_str();
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    if (!*p)
        return false;
    u32 v = 0;
    for (; *p; p++) {
        int d = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 : (*p >= 'A' && *p <= 'F') ? *p - 'A' + 10 : -1;
        if (d < 0)
            return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

static bool reg_address(const std::string& s, u32* out) {
    for (auto& r : reg_names)
        if (!strcasecmp(r.name, s.c_str())) {
            *out = r.addr;
            return true;
        }
    return hex(s, out);
}

// One command. False: it could not be done (the caller says where it was).
static bool command(const std::string& line) {
    std::vector<std::string> t;
    size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && isspace((unsigned char)line[at])) at++;
        if (at >= line.size() || line[at] == '#')
            break;
        size_t end = at;
        while (end < line.size() && !isspace((unsigned char)line[end])) end++;
        t.push_back(line.substr(at, end - at));
        at = end;
    }
    if (t.empty())
        return true;
    const std::string& c = t[0];
    std::vector<u32> n(t.size(), 0);
    auto numbers = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; i++)
            if (!hex(t[i], &n[i])) {
                fprintf(stderr, "refsw3-offline: '%s' is not a hexadecimal number\n", t[i].c_str());
                return false;
            }
        return true;
    };
    if (c == "reset") {
        reset_all();
    } else if (c == "reg" && t.size() == 3) {
        u32 addr;
        if (!reg_address(t[1], &addr)) {
            fprintf(stderr, "refsw3-offline: no register '%s'\n", t[1].c_str());
            return false;
        }
        if (!numbers(2, 3))
            return false;
        reg_write(addr, n[2]);
    } else if (c == "render" && t.size() == 1) {
        reg_write(STARTRENDER_addr, 0xFFFFFFFF);
    } else if ((c == "w32" || c == "w64") && t.size() >= 2) {
        if (!numbers(1, t.size()))
            return false;
        if ((u64)n[1] + (t.size() - 2) * 4 > VRAM_SIZE || (n[1] & 3)) {
            fprintf(stderr, "refsw3-offline: %s at %X is not inside video memory\n", c.c_str(), n[1]);
            return false;
        }
        for (size_t i = 2; i < t.size(); i++) {
            u32 a = n[1] + (u32)(i - 2) * 4;
            *(u32*)&vram[c == "w32" ? pvr_map32(a) : a] = n[i];
        }
    } else if (c == "ta") {
        if (!numbers(1, t.size()))
            return false;
        if ((t.size() - 1) % 8) {
            fprintf(stderr, "refsw3-offline: ta takes a multiple of 8 words, not %d\n", (int)t.size() - 1);
            return false;
        }
        if (t.size() > 1) {
            refta::Write(&n[1], (u32)(t.size() - 1));
        }
    } else if (c == "wait" && t.size() == 2) {
        if (!numbers(1, 2))
            return false;
        if ((events_normal & n[1]) != n[1])
            problem("a console would wait forever: of the events %08X, %08X did not happen", n[1], n[1] & ~events_normal);
        events_normal &= ~n[1];
    } else if (c == "dump32" && t.size() == 4) {
        if (!numbers(1, 3))
            return false;
        return dump32(n[1], n[2], t[3].c_str());
    } else if (c == "fill32" && t.size() == 4) {
        if (!numbers(1, 4))
            return false;
        if ((u64)n[1] + (u64)n[2] * 4 > VRAM_SIZE || (n[1] & 3)) {
            fprintf(stderr, "refsw3-offline: fill32 at %X is not inside video memory\n", n[1]);
            return false;
        }
        for (u32 i = 0; i < n[2]; i++)
            *(u32*)&vram[pvr_map32(n[1] + i * 4)] = n[3];
    } else if (c == "tahash" && t.size() == 4) {
        // CRC-32 (poly EDB88320, bytes of each word low first) of the area and then of the four tastate words
        if (!numbers(1, 3))
            return false;
        static u32 table[256];
        if (!table[1])
            for (u32 i = 0; i < 256; i++) {
                u32 v = i;
                for (int k = 0; k < 8; k++)
                    v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
                table[i] = v;
            }
        u32 crc = 0xFFFFFFFFu;
        auto add = [&](u32 w) {
            for (int k = 0; k < 32; k += 8)
                crc = table[(crc ^ (w >> k)) & 255] ^ (crc >> 8);
        };
        for (u32 i = 0; i < n[2]; i++)
            add(vri(vram, n[1] + i * 4));
        const u32 w[4] = { TA_ISP_CURRENT, TA_NEXT_OPB, seen_normal & ((0xFu << 7) | (1u << 21)), seen_error };
        for (u32 x : w)
            add(x);
        seen_normal = seen_error = 0;
        printf("PVRIO hash %s %08x\n", t[3].c_str(), ~crc);
    } else if (c == "tastate" && t.size() == 2) {
        FILE* f = fopen(t[1].c_str(), "wb");
        if (!f) {
            fprintf(stderr, "refsw3-offline: cannot write %s\n", t[1].c_str());
            return false;
        }
        const u32 w[4] = { TA_ISP_CURRENT, TA_NEXT_OPB, seen_normal & ((0xFu << 7) | (1u << 21)), seen_error };
        fwrite(w, 4, 4, f);
        fclose(f);
        seen_normal = seen_error = 0;
    } else if (c == "regdump" && t.size() == 4) {
        if (!numbers(1, 3))
            return false;
        FILE* f = fopen(t[3].c_str(), "wb");
        if (!f) {
            fprintf(stderr, "refsw3-offline: cannot write %s\n", t[3].c_str());
            return false;
        }
        for (u32 i = 0; i < n[2]; i++) {
            u32 w = PvrReg((n[1] + 4 * i) & pvr_RegMask & ~3u, u32);
            fwrite(&w, 4, 1, f);
        }
        fclose(f);
    } else if (c == "scan32" && t.size() == 5) {
        if (!numbers(1, 4))
            return false;
        u32 count = 0;
        for (u32 i = 0; i < n[2]; i++) {
            u32 w = vri(vram, n[1] + 4 * i);
            if (w != n[3] && count++ < 64)
                printf("PVRIO scan %s %06x %08x\n", t[4].c_str(), n[1] + 4 * i, w);
        }
        printf("PVRIO scan %s count %u\n", t[4].c_str(), count);
    } else if (c == "png" && t.size() == 2) {
        return write_display(t[1].c_str());
    } else if (c == "log" && t.size() >= 2) {
        if (rendlog)
            fclose(rendlog);
        rendlog = t[1] == "off" ? nullptr : fopen(t[1].c_str(), "w");
        rendlog_tiles.clear();
        for (size_t i = 2; i < t.size(); i++) {         // tiles x,y: only their lines
            unsigned x, y;
            if (sscanf(t[i].c_str(), "%u,%u", &x, &y) != 2)
                return false;
            rendlog_tiles.push_back(x | y << 8);
        }
        return t[1] == "off" || rendlog;
    } else if (c == "regs" && t.size() == 2) {
        return load_regs(t[1].c_str());
    } else if (c == "vram" && t.size() == 2) {
        return load_vram(t[1].c_str());
    } else if (c == "save" && t.size() == 2) {
        return save_dump(t[1]);
    } else if (c == "echo") {
        size_t p = line.find("echo");
        printf("%s\n", line.c_str() + (p + 4 < line.size() ? p + 5 : line.size()));
    } else {
        fprintf(stderr, "refsw3-offline: do not understand this command\n");
        return false;
    }
    return true;
}

// Where the commands being run come from: refta says it when input would stop a console (it has no CPU to name)
static const char* where_name = "the command line";
static int where_line;
const char* refta_offline_where() {
    static char buf[300];
    snprintf(buf, sizeof(buf), "(no CPU: %s line %d)", where_name, where_line);
    return buf;
}

static bool run_stream(FILE* f, const char* name) {
    std::string line;
    int number = 0, c;
    for (;;) {
        line.clear();
        while ((c = fgetc(f)) != EOF && c != '\n')
            line.push_back((char)c);
        if (c == EOF && line.empty())
            return true;
        number++;
        where_name = name;
        where_line = number;
        if (!command(line)) {
            fprintf(stderr, "refsw3-offline: %s line %d: %.100s\n", name, number, line.c_str());
            return false;
        }
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);    // whole lines: messages on stderr do not cut into them in a shared log
    vram = (u8*)calloc(1, VRAM_SIZE + 4096);
    refcore::emu_vram = vram;
    reset_all();
    bool did = false, ok = true;
    for (int i = 1; i < argc && ok; i++) {
        std::string a = argv[i];
        if (a == "--lax") {
            lax = true;
        } else if (a == "-v") {
            verbose = true;
        } else if (a == "-c" && i + 1 < argc) {
            std::string all = argv[++i];
            did = true;
            for (size_t at = 0; at <= all.size() && ok;) {
                size_t end = all.find(';', at);
                if (end == std::string::npos)
                    end = all.size();
                std::string one = all.substr(at, end - at);
                ok = command(one);
                if (!ok)
                    fprintf(stderr, "refsw3-offline: -c: %.100s\n", one.c_str());
                at = end + 1;
            }
        } else if (a == "--dump" && i + 1 < argc) {
            std::string folder = argv[++i];
            std::string png = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : folder + "/frame.png";
            did = true;
            ok = load_vram((folder + "/vram.bin").c_str()) && load_regs((folder + "/pvr_regs.bin").c_str());
            if (ok) {
                reg_write(STARTRENDER_addr, 0xFFFFFFFF);
                ok = write_display(png.c_str());
            }
        } else if (a == "-h" || a == "--help") {
            printf("usage: refsw3-offline [--lax] [-v] [-c \"cmd; cmd\"] [--dump <folder> [png]] [script | -]...\n"
                   "commands: reset | reg <name|offset> <value> | w32 <addr> <word>... | w64 <addr> <word>... | ta <word>... |\n"
                   "          render | wait <events> | dump32 <addr> <words> <file> | png <file> | log <file|off> |\n"
                   "          regs <file> | vram <file> | save <folder> | echo <text>      (numbers are hexadecimal)\n");
            return 0;
        } else {
            FILE* f = a == "-" ? stdin : fopen(a.c_str(), "r");
            if (!f) {
                fprintf(stderr, "refsw3-offline: cannot open %s\n", a.c_str());
                return 1;
            }
            did = true;
            ok = run_stream(f, a.c_str());
            if (f != stdin)
                fclose(f);
        }
    }
    if (!did && ok)
        ok = run_stream(stdin, "standard input");
    if (rendlog)
        fclose(rendlog);
    if (verbose)
        printf("%d renders\n", renders);
    if (!ok)
        return 1;
    return problems ? 2 : 0;
}
