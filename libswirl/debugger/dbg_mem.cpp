/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_mem.h"
#include "dbg_regs_private.h"

#include "hw/aica/aica_mmio.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/holly/sh4_mem_area0.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/pvr/pvr_regs.h"
#include "hw/sh4/sh4_if.h"

#include <algorithm>
#include <cstring>

/*
	Guest memory for the debugger. A read is cut into runs of one region; each run is copied from the backing
	store of its region, or put together from register values. Nothing here calls a guest memory handler or the
	read function of a register.

	The backing stores are looked up from the machine for every run; no pointer outlives a call.
*/

namespace dbg { namespace mem {

namespace {

struct Range {
	u32 first;
	u32 last;
	Region region;
};

// Area 0, by its low 25 bits. The ranges follow one another without a gap.
const Range area0_ranges[] = {
	{ 0x00000000, 0x001FFFFF, Region::Bios },
	{ 0x00200000, 0x0021FFFF, Region::Flash },
	{ 0x00220000, 0x005F67FF, Region::Unreadable },
	{ 0x005F6800, 0x005F6FFF, Region::SbRegs },
	{ 0x005F7000, 0x005F70FF, Region::Unreadable },		// GD-ROM
	{ 0x005F7100, 0x005F7CFF, Region::SbRegs },
	{ 0x005F7D00, 0x005F7FFF, Region::Unreadable },
	{ 0x005F8000, 0x005F9FFF, Region::PvrRegs },
	{ 0x005FA000, 0x006FFFFF, Region::Unreadable },		// the modem is at 0x00600000
	{ 0x00700000, 0x00707FFF, Region::AicaRegs },
	{ 0x00708000, 0x0070FFFF, Region::Unreadable },
	{ 0x00710000, 0x0071000B, Region::AicaRtc },
	{ 0x0071000C, 0x007FFFFF, Region::Unreadable },
	{ 0x00800000, 0x00FFFFFF, Region::AicaRam },
	{ 0x01000000, 0x01FFFFFF, Region::Unreadable },
};

// The 29 bits of an address below P4, behind area 0.
const Range sh4_ranges[] = {
	{ 0x04000000, 0x04FFFFFF, Region::Vram64 },
	{ 0x05000000, 0x05FFFFFF, Region::Vram32 },
	{ 0x06000000, 0x06FFFFFF, Region::Vram64 },
	{ 0x07000000, 0x07FFFFFF, Region::Vram32 },
	{ 0x08000000, 0x0BFFFFFF, Region::Unreadable },
	{ 0x0C000000, 0x0FFFFFFF, Region::Ram },
	{ 0x10000000, 0x1EFFFFFF, Region::Unreadable },
	{ 0x1F000000, 0x1FFFFFFF, Region::Sh4Regs },
};

const Range p4_ranges[] = {
	{ 0xE0000000, 0xE3FFFFFF, Region::StoreQueue },
	{ 0xE4000000, 0xFEFFFFFF, Region::Unreadable },
	{ 0xFF000000, 0xFFFFFFFF, Region::Sh4Regs },
};

const Range arm7_ranges[] = {
	{ 0x00000000, 0x007FFFFF, Region::AicaRam },
	{ 0x00800000, 0x00807FFF, Region::AicaRegs },
	{ 0x00808000, 0xFFFFFFFF, Region::Unreadable },
};

template <size_t N>
const Range& Find(const Range (&ranges)[N], u32 addr)
{
	for (const Range& range : ranges)
	{
		if (addr <= range.last)
			return range;
	}

	return ranges[N - 1];
}

// The region of addr, and the last address of the run of that region addr is in: a run ends where the region
// changes and where a mirror ends.
Region Locate(Target t, u32 addr, u32* last)
{
	switch (t)
	{
	case Target::Sh4:
		if (addr >= 0xE0000000)
		{
			const Range& range = Find(p4_ranges, addr);
			*last = range.last;
			return range.region;
		}
		else
		{
			u32 mirror = addr & 0xE0000000;
			u32 low = addr & 0x1FFFFFFF;

			if (low < 0x04000000)
			{
				// area 0 is there twice
				mirror |= low & 0x02000000;

				const Range& range = Find(area0_ranges, low & 0x01FFFFFF);
				*last = mirror | range.last;
				return range.region;
			}

			const Range& range = Find(sh4_ranges, low);
			*last = mirror | range.last;
			return range.region;
		}

	case Target::Arm7:
	{
		const Range& range = Find(arm7_ranges, addr);
		*last = range.last;
		return range.region;
	}

	case Target::Dsp:
		break;
	}

	*last = 0xFFFFFFFF;
	return Region::Unreadable;
}

// Marks count bytes from byte first as readable.
void Mark(u8* bits, u32 first, u32 count)
{
	if (bits == nullptr)
		return;

	for (u32 i = first; i < first + count; i++)
		bits[i >> 3] |= (u8)(1 << (i & 7));
}

// A store that is mirrored by its size, a power of two: byte i of the run is data[(offset + i) & (size - 1)].
u32 CopyMirrored(const u8* data, u32 size, u32 offset, u32 count, u8* out)
{
	if (data == nullptr || size == 0)
		return 0;

	u32 mask = size - 1;

	for (u32 done = 0; done < count; )
	{
		u32 at = (offset + done) & mask;
		u32 n = std::min(count - done, size - at);

		memcpy(out + done, data + at, n);
		done += n;
	}

	return count;
}

// A run of 32-bit registers: read(word address, &value, &bytes) gives a register and how many of its bytes
// exist. A byte of the run is readable when its register is and the byte is one of those.
template <typename ReadWord>
u32 CopyRegisters(u32 addr, u32 count, u8* out, u8* bits, u32 first, ReadWord read)
{
	u32 readable = 0;

	for (u32 done = 0; done < count; )
	{
		u32 at = addr + done;
		u32 lane = at & 3;
		u32 n = std::min(count - done, 4 - lane);
		u32 value, bytes;

		if (read(at & ~3u, &value, &bytes))
		{
			for (u32 i = 0; i < n; i++)
			{
				if (lane + i < bytes)
				{
					out[done + i] = (u8)(value >> ((lane + i) * 8));
					Mark(bits, first + done + i, 1);
					readable++;
				}
			}
		}

		done += n;
	}

	return readable;
}

// The register window of the AICA as byte reads by the ARM7 deliver it. out holds the run [offset, offset +
// count) of the register file as the SH4 reads it (AICA::DebugReadRegs); the ARM7 reads the same bytes but for
//   0x2C00, 0x2C01   where the SH4 has ARMRST and VREG and the ARM7 the two bytes of the register file behind
//                    them, which no accessor delivers: not readable
//   0x2D00           the interrupt level L of the controller in front of nFIQ, which the sound CPU keeps
//   0x2D04           M of that controller, which reads 0
// Marks the readable bytes from bit `first` on and returns their number.
u32 Arm7RegisterView(u32 offset, u32 count, u8* out, u8* bits, u32 first)
{
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	u32 level = 0, in_service = 0;
	bool controller = scpu != nullptr && scpu->DebugGetInterrupt(&level, &in_service);
	u32 readable = 0;

	for (u32 i = 0; i < count; i++)
	{
		bool known = true;

		switch (offset + i)
		{
		case 0x2C00:
		case 0x2C01:
			out[i] = 0;
			known = false;
			break;

		case 0x2D00:
			out[i] = (u8)level;
			known = controller;
			break;

		case 0x2D04:
			out[i] = 0;
			break;
		}

		if (known)
		{
			Mark(bits, first + i, 1);
			readable++;
		}
	}

	return readable;
}

// Copies the run [addr, addr + count) of one region to out, marks its readable bytes from bit `first` on and
// returns their number. out and bits are zero where nothing is written.
u32 ReadRun(Target t, Region region, u32 addr, u32 count, u8* out, u8* bits, u32 first)
{
	u32 readable = 0;

	switch (region)
	{
	case Region::Unreadable:
		break;

	case Region::Bios:
	{
		const u8* data;
		u32 size;

		// the region starts at a multiple of every size a store can have
		if (area0_DebugRom(&data, &size))
			readable = CopyMirrored(data, size, addr, count, out);
		break;
	}

	case Region::Flash:
	{
		u8 first_byte;

		// What a guest read delivers is not the store everywhere: the chip puts settings in place of a few
		// stored bytes, so every byte is asked for as that read gets it. The region starts at a multiple of
		// every size a store can have, and the offset is taken modulo that size
		if (area0_DebugNvmemByte(addr, &first_byte))
		{
			for (u32 i = 0; i < count; i++)
				area0_DebugNvmemByte(addr + i, &out[i]);
			readable = count;
		}
		break;
	}

	case Region::SbRegs:
		return CopyRegisters(addr & 0x01FFFFFF, count, out, bits, first, [](u32 at, u32* value, u32* bytes) {
			*bytes = 4;
			return regs::ReadSbRegisterAt(at, value);
		});

	case Region::Sh4Regs:
		return CopyRegisters(addr, count, out, bits, first, [](u32 at, u32* value, u32* bytes) {
			return regs::ReadSh4RegisterAt(at, value, bytes);
		});

	case Region::PvrRegs:
		readable = CopyMirrored(pvr_regs, pvr_RegSize, addr & pvr_RegMask, count, out);
		break;

	case Region::AicaRegs:
	{
		AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);

		// a run of this region is inside the register file
		if (aica == nullptr || !aica->DebugReadRegs(addr & 0x7FFF, count, out))
			break;

		if (t == Target::Arm7)
			return Arm7RegisterView(addr & 0x7FFF, count, out, bits, first);

		readable = count;
		break;
	}

	case Region::AicaRtc:
	{
		u32 seconds;
		bool write_enable;

		if (libAICA_DebugGetRTC(&seconds, &write_enable))
		{
			// the high half of the counter, its low half, and the write enable, which reads as 0
			const u32 words[3] = { seconds >> 16, seconds & 0xFFFF, 0 };
			u32 offset = addr & 0xFF;

			for (u32 i = 0; i < count; i++)
				out[i] = (u8)(words[(offset + i) / 4] >> ((offset + i) % 4 * 8));
			readable = count;
		}
		break;
	}

	case Region::AicaRam:
		// the SH4 has it at 0x00800000, a multiple of its size; the ARM7 at 0
		readable = CopyMirrored(sh4_cpu->aica_ram.data, sh4_cpu->aica_ram.size, addr, count, out);
		break;

	case Region::Vram64:
		readable = CopyMirrored(sh4_cpu->vram.data, sh4_cpu->vram.size, addr, count, out);
		break;

	case Region::Vram32:
	{
		const u8* vram = sh4_cpu->vram.data;
		u32 size = sh4_cpu->vram.size;

		// the two banks of the 64-bit view alternate every four bytes; a byte keeps its place in its word
		if (vram != nullptr && size != 0)
		{
			for (u32 i = 0; i < count; i++)
				out[i] = vram[pvr_map32(addr + i) & (size - 1)];
			readable = count;
		}
		break;
	}

	case Region::Ram:
		readable = CopyMirrored(sh4_cpu->mram.data, sh4_cpu->mram.size, addr, count, out);
		break;

	case Region::StoreQueue:
		if (p_sh4rcb != nullptr)
			readable = CopyMirrored((const u8*)p_sh4rcb->sq_buffer, sizeof(p_sh4rcb->sq_buffer), addr, count, out);
		break;
	}

	// everything but the register regions and the ARM7's register window is readable as a whole or not at all
	Mark(bits, first, readable);
	return readable;
}

}

Region Classify(Target t, u32 addr)
{
	u32 last;
	return Locate(t, addr, &last);
}

u32 Read(Target t, u32 addr, u32 len, u8* out, u8* readableBits)
{
	if (len == 0)
		return 0;

	memset(out, 0, len);
	if (readableBits)
		memset(readableBits, 0, (len + 7) / 8);

	if (sh4_cpu == nullptr)
		return 0;

	u32 readable = 0;

	for (u32 done = 0; done < len; )
	{
		u32 at = addr + done;
		u32 last;
		Region region = Locate(t, at, &last);
		u32 count = (u32)std::min<u64>(len - done, (u64)(last - at) + 1);

		readable += ReadRun(t, region, at, count, out + done, readableBits, done);
		done += count;
	}

	return readable;
}

// Nothing can be written yet.
int Write(Target, u32, u32, const u8*) { return kErrNotSupported; }

} }
