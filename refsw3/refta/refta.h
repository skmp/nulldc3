#pragma once
/*
    refta: the PowerVR2 tile accelerator as a console does it (refta.cpp). The caller owns the registers
    (pvr_regs) and video memory; refta reads and writes them as the hardware does, including TA_OL_POINTERS.
*/
#include <cstdint>

namespace refta {

void Reset();                                       // SOFTRESET bit 0
void ListInit(uint8_t* vram);                       // TA_LIST_INIT with bit 31
void ListCont();                                    // TA_LIST_CONT with bit 31
void Write(const uint32_t* words, uint32_t count);  // data for the TA, count a multiple of 8

}
