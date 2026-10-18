/*
	aica_dsp_guest_oracle: the DSP of the sounding guest (tests/debugger/guest/dsptest) on the caique sample model,
	for the mode oracle of tests/debugger/guest/dsptest/soundlines.py (debugger.sound.oracle.dsptest.*).

	    aica_dsp_guest_oracle --elf <dsptest.elf or a copy of it> --ticks <N>

	It reads from the ELF, by its symbols, what the guest's _start writes to the DSP between the first tick of the
	AICA and the second: the entries of its table (g_table to g_table_end, (address, value) pairs, address 0 an
	empty entry) in their order, of which it takes the writes of the DSP's registers (AicaDsp::RegOwned: here the
	MEMS_H entries and RBP / RBL) and of the channels' ISEL (0x20 of a channel, bits 3:0), and behind the table the
	block g_dsp, which _start copies with long writes: g_dsp_coef to 0x3000, g_dsp_madrs to 0x3200, g_dsp_mpro to
	0x3400, one 32-bit word for each register, its low 16 bits written. Then it drives the DSP core and the caique
	model's DSP in lockstep (dsp_lockstep.h: the two are compared behind every step) on the guest's schedule:

	  tick 1         32 samples of the empty program, from the state after reset
	  the writes     all of them, in the order _start makes them: the table, then the block
	  ticks 2 to N   32 samples each, on the guest's program

	with what the emulator's sound generator gives the DSP when no channel's send can be other than 0 (the copy of
	the guest that the mode oracle runs, whose first channel has IMXL 0): in every sample its sweep, SweepBegin,
	then one send of 0 to every bus that a channel's ISEL names (before the table bus 0, which all 64 channels
	have; behind it bus 0 and the bus of the first channel's ISEL); EXTS 0 (no disc), no channel fetch in a slot
	of the DSP and no bus read in the latch (the emulator wires neither). The sends matter although they are 0:
	the core's state has the buses a sweep sent to (mixs_sent), and a send marks its bus whatever its value.
	Behind every tick the core's state blob (AicaDsp::SaveState) is folded as the sound regression harness folds
	it into the field dsp of its line (libswirl/hw/aica/aica_hash.h: FNV-1a 64 from the offset basis, every byte,
	at every tick), and at the end the tool prints

	    dsp=<hex>

	which is the field dsp of the emulator's line of that copy at N ticks, when the emulator's DSP computes what
	caique's model computes. Exit code 0: the model and the core agreed in every step; 1: they did not (the first
	difference is printed), 2: the ELF or the arguments are not usable.
*/
#include "dsp_lockstep.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

static const uint32_t kAicaRegs = 0xA0700000;	// the AICA's register area, as the guest's SH4 writes it
static const uint32_t kAicaRegsEnd = 0xA0708000;

struct Elf
{
	std::vector<uint8_t> image;
	std::map<std::string, uint32_t> symbols;
	std::string error;

	uint32_t u32(size_t at) const { return image[at] | (image[at + 1] << 8) | (image[at + 2] << 16) | ((uint32_t)image[at + 3] << 24); }
	uint32_t u16(size_t at) const { return image[at] | (image[at + 1] << 8); }

	bool Load(const char* path)
	{
		FILE* f = fopen(path, "rb");

		if (!f)
		{
			error = std::string("cannot read ") + path;
			return false;
		}

		uint8_t buffer[65536];
		size_t n;

		while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
			image.insert(image.end(), buffer, buffer + n);

		fclose(f);

		if (image.size() < 52 || memcmp(image.data(), "\x7f" "ELF\x01\x01", 6) != 0)
		{
			error = std::string(path) + " is no ELF of 32 bits, low byte first";
			return false;
		}

		//the symbol table and its strings
		const uint32_t shoff = u32(0x20), shentsize = u16(0x2E), shnum = u16(0x30);

		if ((uint64_t)shoff + (uint64_t)shentsize * shnum > image.size())
		{
			error = std::string(path) + ": its section headers are outside of the file";
			return false;
		}

		for (uint32_t i = 0; i < shnum; i++)
		{
			const size_t sh = shoff + (size_t)i * shentsize;

			if (u32(sh + 4) != 2)	//SHT_SYMTAB
				continue;

			const uint32_t off = u32(sh + 0x10), size = u32(sh + 0x14), link = u32(sh + 0x18), entsize = u32(sh + 0x24);
			const size_t strsh = shoff + (size_t)link * shentsize;
			const uint32_t stroff = u32(strsh + 0x10), strsize = u32(strsh + 0x14);

			if (link >= shnum || entsize < 16 || (uint64_t)off + size > image.size() || (uint64_t)stroff + strsize > image.size())
			{
				error = std::string(path) + ": its symbol table is outside of the file";
				return false;
			}

			for (uint32_t at = off; at + 16 <= off + size; at += entsize)
			{
				const uint32_t name = u32(at);

				if (name < strsize)
					symbols[std::string((const char*)&image[stroff + name], strnlen((const char*)&image[stroff + name], strsize - name))] = u32(at + 4);
			}
		}

		return true;
	}

	//The bytes of a loaded segment at an address, or -1
	long Offset(uint32_t address, uint32_t length) const
	{
		const uint32_t phoff = u32(0x1C), phentsize = u16(0x2A), phnum = u16(0x2C);

		for (uint32_t i = 0; i < phnum; i++)
		{
			const size_t ph = phoff + (size_t)i * phentsize;

			if (ph + 32 > image.size() || u32(ph) != 1)	//PT_LOAD
				continue;

			const uint32_t offset = u32(ph + 4), vaddr = u32(ph + 8), filesz = u32(ph + 16);

			if (address >= vaddr && (uint64_t)address + length <= (uint64_t)vaddr + filesz && (uint64_t)offset + filesz <= image.size())
				return (long)(offset + (address - vaddr));
		}

		return -1;
	}
};

struct Write
{
	uint32_t off;
	uint32_t value;
};

static int Usage(const char* what)
{
	fprintf(stderr, "aica_dsp_guest_oracle: %s\nusage: aica_dsp_guest_oracle --elf <dsptest.elf> --ticks <N>\n", what);
	return 2;
}

int main(int argc, char** argv)
{
	const char* path = nullptr;
	long ticks = 0;

	for (int i = 1; i + 1 < argc; i += 2)
	{
		if (!strcmp(argv[i], "--elf"))
			path = argv[i + 1];
		else if (!strcmp(argv[i], "--ticks"))
			ticks = strtol(argv[i + 1], nullptr, 10);
		else
			return Usage("an unknown argument");
	}

	if (!path || ticks < 1 || (argc - 1) % 2)
		return Usage("--elf and --ticks (1 or more) are needed");

	Elf elf;

	if (!elf.Load(path))
		return Usage(elf.error.c_str());

	static const char* const labels[] = { "g_table", "g_table_end", "g_dsp_coef", "g_dsp_coef_end", "g_dsp_madrs",
		"g_dsp_madrs_end", "g_dsp_mpro", "g_dsp_mpro_end" };

	for (const char* label : labels)
	{
		if (!elf.symbols.count(label))
			return Usage((std::string(path) + " has no symbol " + label + ": it is not the sounding guest with its DSP program").c_str());
	}

	//What _start writes to the DSP and to the channels' ISEL, in its order: the table, then the block
	std::vector<Write> writes;
	uint8_t isel[64] = { 0 };			//the bus of each channel: 0 from the reset on
	uint8_t isel_table[64] = { 0 };		//and as the table leaves it
	unsigned table_writes = 0, isel_writes = 0;

	{
		const uint32_t from = elf.symbols["g_table"], to = elf.symbols["g_table_end"];
		const long at = elf.Offset(from, to - from);

		if (to < from || (to - from) % 8 || at < 0)
			return Usage("g_table to g_table_end is no table of (address, value) pairs that the ELF loads");

		for (uint32_t n = 0; n < (to - from) / 8; n++)
		{
			const uint32_t address = elf.u32((size_t)at + 8 * n), value = elf.u32((size_t)at + 8 * n + 4);

			if (address < kAicaRegs || address >= kAicaRegsEnd)
				continue;

			const uint32_t off = address - kAicaRegs;

			if (AicaDsp::RegOwned(off))
			{
				writes.push_back({ off, value & 0xFFFF });
				table_writes++;
			}
			else if (off < 0x2000 && (off & 0x7F) == 0x20)
			{
				isel_table[off >> 7] = (uint8_t)(value & 15);
				isel_writes++;
			}
		}
	}

	static const struct { const char* from; const char* to; uint32_t off; } parts[] =
	{
		{ "g_dsp_coef", "g_dsp_coef_end", 0x3000 },
		{ "g_dsp_madrs", "g_dsp_madrs_end", 0x3200 },
		{ "g_dsp_mpro", "g_dsp_mpro_end", 0x3400 },
	};

	unsigned block_writes = 0;

	for (const auto& part : parts)
	{
		const uint32_t from = elf.symbols[part.from], to = elf.symbols[part.to];
		const long at = elf.Offset(from, to - from);

		if (to < from || (to - from) % 4 || at < 0)
			return Usage((std::string(part.from) + " to " + part.to + " is no block of 32-bit words that the ELF loads").c_str());

		for (uint32_t n = 0; n < (to - from) / 4; n++)
		{
			writes.push_back({ part.off + 4 * n, elf.u32((size_t)at + 4 * n) & 0xFFFF });
			block_writes++;
		}
	}

	DspLockstep l;
	l.Reset();

	uint64_t dsp = 0xcbf29ce484222325ull;
	std::vector<uint8_t> blob(AicaDsp::StateSize());

	//The sweep of a sample: a send of 0 to every bus a channel's ISEL names, once for each bus, in the order of the
	//buses, as the emulator's SampleSweep makes it
	auto sweep = [&]() {
		LockstepEvents events;
		bool named[16] = { false };

		for (int ch = 0; ch < 64; ch++)
			named[isel[ch] & 15] = true;

		for (int bus = 0; bus < 16; bus++)
		{
			if (named[bus])
				events.sends.push_back({ (uint8_t)bus, 0 });
		}

		return events;
	};

	auto tick = [&](const LockstepEvents& events) {
		for (int s = 0; s < 32; s++)
			l.StepDsp(events);

		l.CompareRam();
		l.core.SaveState(blob.data());

		for (uint8_t byte : blob)
		{
			dsp ^= byte;
			dsp *= 0x100000001b3ull;
		}
	};

	//tick 1, on the empty program
	tick(sweep());

	//_start's writes between the first tick and the second
	for (const Write& w : writes)
		l.RegWrite(w.off, w.value);

	memcpy(isel, isel_table, sizeof(isel));

	const LockstepEvents program = sweep();

	for (long t = 2; t <= ticks && l.ok; t++)
		tick(program);

	std::string buses;

	for (const LockstepSend& send : program.sends)
		buses += (buses.empty() ? "" : " and ") + std::to_string(send.bus);

	printf("the guest's DSP writes: %u of the table, %u of the block g_dsp; %u writes of a channel's ISEL; from tick 2 on the sweep sends 0 to bus %s\n",
		table_writes, block_writes, isel_writes, buses.c_str());
	printf("the core and the caique model in lockstep: %llu steps compared, %llu wave memory reads, %llu writes, %llu IWT raw, %llu IWT unpacked\n",
		(unsigned long long)l.steps, (unsigned long long)l.cover.ram_reads, (unsigned long long)l.cover.ram_writes,
		(unsigned long long)l.cover.iwt_raw, (unsigned long long)l.cover.iwt_float);

	if (!l.ok)
	{
		printf("the model and the core differ: %s\n", l.first.c_str());
		return 1;
	}

	printf("dsp=%016llx\n", (unsigned long long)dsp);
	return 0;
}
