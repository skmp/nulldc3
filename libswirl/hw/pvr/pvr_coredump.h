/*
	This file is part of libswirl
*/
#include "license/bsd"

/*
	A PVR core dump (.pvrdmp): what a render of the PVR core reads, in one file.

	    "pvrcoredump"           the 11 bytes of this cookie, ASCII
	    gzip member             the video memory as the 32 bit area shows it (address 0 first): VRAM_SIZE bytes,
	                            8 MB on a Dreamcast, 16 MB on NAOMI; a reader takes the size from what it inflates to
	    gzip member             the PVR's register file, pvr_RegSize (0x2000) bytes, as pvr_regs holds it

	The two members are the vram_<scene>.bin and pvr_regs_<scene>.bin of polly2-data's older dumps (the refsw2 dump
	key's vram.bin and pvr_regs.bin), byte for byte. Taken at the start of a render, before the renderer runs: the
	tile accelerator's lists, the parameters and the region array are in video memory as STARTRENDER found them
	(with refsw3-ta, the TA of the software renderers; the emulator's own TA writes no lists there).
*/
#pragma once

#include "types.h"

// Write the dump of this video memory and the registers now. True when the file was written whole
bool pvr_coredump_write(const string& path, u8* vram);

// Have the next render that is not a render to texture written to path when it starts. Any thread
void pvr_coredump_request(const string& path);

// The renderers' frame, before the render: write a requested dump. rtt: this render goes to a texture
void pvr_coredump_poll(u8* vram, bool rtt);
