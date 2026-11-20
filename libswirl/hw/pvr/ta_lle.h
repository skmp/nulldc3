#pragma once
/*
    The tile accelerator. Two of them: refsw3-ta (refsw3/refta, the TA as a console does it, writing object lists
    and parameters into video memory) for the renderers that draw from there (rendererbackend_t::lle_ta: refsw3,
    refsw2, refsw, refmister, none), and the emulator's own (hw/pvr/ta.cpp, "HLE": TA_contexts for Process()) for
    the others (legacy GL and the fast software ones). Which one a frame goes to is latched at TA_LIST_INIT
    (ta_lle_select), so that a change of renderer never splits a frame between the two.
*/
#include "types.h"
#include "refta/refta.h"

// the TA of the frame being sent: true refsw3-ta, false the emulator's own (hw/pvr/pvr_sb_regs.cpp)
extern bool ta_lle;

// the renderer's choice (hw/pvr/Renderer_if.cpp)
bool rend_wants_lle_ta();

// TA_LIST_INIT with bit 31, and a reset of the PVR: the frame that starts goes to the TA the renderer wants
static inline void ta_lle_select()
{
    ta_lle = rend_wants_lle_ta();
}

// SOFTRESET bit 0, and a reset of the PVR
static inline void ta_lle_reset()
{
    refta::Reset();
}

// TA_LIST_INIT with bit 31
static inline void ta_lle_list_init(u8* vram)
{
    refta::ListInit(vram);
}

// TA_LIST_CONT with bit 31
static inline void ta_lle_list_cont()
{
    refta::ListCont();
}

// data for the TA: bytes, whole 32 byte blocks
static inline void ta_lle_write(const void* data, u32 bytes)
{
    refta::Write((const u32*)data, bytes / 4);
}

// one store queue to the TA's FIFO: 32 bytes
static inline void ta_lle_write_burst(u32 addr, const u8* sq)
{
    (void)addr;
    refta::Write((const u32*)sq, 8);
}
