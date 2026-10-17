/*
	The emulator's sound chip on a machine of its own (emu_aica.h): libswirl's hw/aica/aica_mmio.cpp, sgc_if.cpp and
	dsp.cpp with the DSP core, compiled with the emulator's definitions and flags, and here what they reference of
	the rest of the emulator: the SH4 as the place the devices find each other, the system bus registers the AICA
	looks at, the interrupt controller, the ARM7's reset and interrupt lines, the scheduler, save states, the
	settings, the CD-DA drive. After the stubs of tests/arm7/arm7_machine.cpp and tests/debugger/regs/aica/aica_test.cpp.
*/
#include "emu_aica.h"

#include "types.h"
#include "hw/aica/aica.h"
#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp.h"
#include "hw/holly/holly_intc.h"
#include "hw/holly/sb.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"
#include "oslib/audiostream.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/*
	What the three units reference and no test reaches
*/
settings_t settings;
SuperH4* sh4_cpu;

bool rc_serialize(void* src, unsigned int src_size, void** dest, unsigned int* total_size)
{
	if (*dest != nullptr)
	{
		memcpy(*dest, src, src_size);
		*dest = (u8*)*dest + src_size;
	}

	*total_size += src_size;
	return true;
}

bool rc_unserialize(void* src, unsigned int src_size, void** dest, unsigned int* total_size)
{
	if (*dest != nullptr)
	{
		memcpy(src, *dest, src_size);
		*dest = (u8*)*dest + src_size;
	}

	*total_size += src_size;
	return true;
}

int sh4_sched_register(void*, int, sh4_sched_callback*) { return 0; }
void sh4_sched_request(int, int) { }
u64 sh4_sched_now64() { return 0; }
void WriteMemBlock_nommu_dma(u32, u32, u32) { }

//the ARM7: there is none, its reset and its interrupt input go nowhere
void libARM_SetResetState(bool) { }
void libARM_InterruptChange(u32, u32) { }

// verify() and die() end the test
int msgboxf(const wchar* text, unsigned int type, ...)
{
	(void)type;

	va_list args;
	va_start(args, type);
	vfprintf(stderr, text, args);
	va_end(args);
	fprintf(stderr, "\n");

	return 0;
}

void os_DebugBreak()
{
	abort();
}

/*
	The CD-DA drive
*/
static bool cdda_on;
static uint64_t cdda_pairs;

void EmuCddaOn(bool on)
{
	cdda_on = on;
}

//A pair of its own in every sample, neither half 0
void EmuCddaPair(uint64_t n, int16_t* left, int16_t* right)
{
	uint32_t x = (uint32_t)(n * 2654435761u) ^ (uint32_t)(n >> 7);

	*left = (int16_t)((x & 0x3FFF) + 1 - ((x & 0x10000) ? 0x4000 : 0));
	*right = (int16_t)(((x >> 17) & 0x3FFF) + 3 - ((x & 0x20000000) ? 0x4000 : 0));

	if (*left == 0)
		*left = 1;
	if (*right == 0)
		*right = -1;
}

//The sound generator pulls a sector of 588 pairs when it has played the one before
void libCore_CDDA_Sector(s16* sector)
{
	for (int i = 0; i < 588; i++)
	{
		int16_t left = 0, right = 0;

		if (cdda_on)
			EmuCddaPair(cdda_pairs, &left, &right);

		sector[2 * i] = left;
		sector[2 * i + 1] = right;
		cdda_pairs++;
	}
}

namespace
{
	//The system bus registers the AICA looks at: SB_ISTEXT, where its interrupt to the SH4 shows
	struct NoSystemBus : SystemBus
	{
		void RegisterRIO(void*, u32, RegIO, RegReadAddrFP*, RegWriteAddrFP*) { }
	};

	//The interrupt controller: the AICA's line to the SH4
	struct Asic : ASIC
	{
		bool spu = false;

		void RaiseInterrupt(HollyInterruptID id) { if (id == holly_SPU_IRQ) spu = true; }
		void CancelInterrupt(HollyInterruptID id) { if (id == holly_SPU_IRQ) spu = false; }
	};

	//The SH4 as the devices find each other through it
	struct NoSh4 : SuperH4
	{
		MMIODevice* devices[A0H_MAX];

		void SetA0Handler(Area0Hanlders slot, MMIODevice* dev) { devices[slot] = dev; }
		MMIODevice* GetA0Handler(Area0Hanlders slot) { return devices[slot]; }

		bool setBackend(SuperH4Backends) { return true; }

		bool Init() { return true; }
		void Reset(bool) { }
		void Term() { }

		void Run() { }
		void Stop() { }
		void Start() { }
		void Step() { }
		void Skip() { }

		bool IsRunning() { return false; }

		void ResetCache() { }
	};

	//The audio stream: what a sample hands it
	struct Stream : AudioStream
	{
		uint64_t pairs = 0;
		int16_t left = 0, right = 0;

		void InitAudio() override { }
		void TermAudio() override { }

		void WriteSample(s16 r, s16 l) override
		{
			right = r;
			left = l;
			pairs++;
		}
	};

	struct Machine final : EmuAica
	{
		NoSh4 sh4;
		NoSystemBus sb;
		Asic asic;
		Stream stream;
		AicaContext context;
		std::vector<u8> ram;
		DSP* dsp;
		AICA* aica;

		explicit Machine(uint32_t ram_size) : ram(ram_size, 0)
		{
			memset(sh4.devices, 0, sizeof(sh4.devices));
			memset(context.regs, 0, sizeof(context.regs));
			sh4_cpu = &sh4;
			cdda_pairs = 0;

			dsp = DSP::Create(&context, ram.data(), ram_size);
			aica = AICA::Create(&stream, &sb, &asic, dsp, &context, ram.data(), ram_size);
			sh4.SetA0Handler(A0H_AICA, aica);
			sh4.SetA0Handler(A0H_DSP, dsp);

			//in the order of the emulator's devices (SuperH4_impl: Init, then Reset, in A0H order)
			aica->Init();
			dsp->Init();
			aica->Reset(false);
			dsp->Reset(false);
		}

		~Machine()
		{
			delete aica;
			delete dsp;
			sh4_cpu = nullptr;
		}

		uint32_t RegRead(uint32_t off, uint32_t size) override { return aica->ReadReg(off, size); }
		void RegWrite(uint32_t off, uint32_t value, uint32_t size) override { aica->WriteReg(off, value, size); }
		const uint8_t* File() override { return context.regs; }
		uint8_t* Ram() override { return ram.data(); }
		uint32_t RamSize() override { return (uint32_t)ram.size(); }
		void Sample() override { libAICA_TimeStep(); }
		bool Sh4Irq() override { return asic.spu; }
		AicaDsp* Core() override { return dsp->Core(); }
		void DspReset() override { dsp->Reset(false); }

		std::vector<uint8_t> DspSave() override
		{
			return Save(dsp);
		}

		void DspLoad(const std::vector<uint8_t>& state) override
		{
			Load(dsp, state);
		}

		std::vector<uint8_t> AicaSave() override
		{
			return Save(aica);
		}

		void AicaLoad(const std::vector<uint8_t>& state) override
		{
			Load(aica, state);
		}

		static std::vector<uint8_t> Save(MMIODevice* device)
		{
			void* data = nullptr;
			unsigned int size = 0;
			device->serialize(&data, &size);

			std::vector<uint8_t> state(size);
			data = state.data();
			size = 0;
			device->serialize(&data, &size);

			return state;
		}

		static void Load(MMIODevice* device, const std::vector<uint8_t>& state)
		{
			std::vector<uint8_t> copy = state;
			void* data = copy.data();
			unsigned int size = 0;
			device->unserialize(&data, &size);
		}

		uint64_t Pairs() override { return stream.pairs; }
		int16_t LastLeft() override { return stream.left; }
		int16_t LastRight() override { return stream.right; }
	};
}

EmuAica* EmuAica::Create(uint32_t ram_size)
{
	return new Machine(ram_size);
}
