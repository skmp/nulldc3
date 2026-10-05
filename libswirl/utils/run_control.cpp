/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "run_control.h"

#include <chrono>
#include <cmath>
#include <vector>

#include "cfg/cfg.h"
#include "libswirl.h"
#include "gui/gui_renderer.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_mmio.h"

#include "deps/xxhash/xxhash.h"

namespace {

typedef std::chrono::steady_clock wall_clock;

struct RunControl
{
	// the keys
	u32 exit_vblanks = 0;		// 0: no limit
	u64 exit_cycles = 0;		// 0: no limit
	bool exit_on_guest_exit = false;
	bool state_hash = false;

	// the run. Emulator thread
	u32 vblanks = 0;
	bool exiting = false;
	const char* exit_reason = "";
	u64 first_vblank_cycles = 0;
	wall_clock::time_point first_vblank_wall;
};

RunControl run_control;

RunControl& runctl()
{
	return run_control;
}

u64 hash_of(const void* data, size_t size)
{
	return XXH64(data, size, 0);
}

// The registers of the SH4 as a program can see them, whichever engine ran it
u64 hash_sh4()
{
	Sh4Context& ctx = p_sh4rcb->cntx;

	struct
	{
		u32 r[16];
		u32 r_bank[8];
		u32 pc, pr, sr, gbr, vbr, ssr, spc, sgr, dbr;
		u32 mach, macl;
		u32 fpscr, fpul;
		u32 fr_xf[32];
		u64 sq[8];
	} regs;

	memset(&regs, 0, sizeof(regs));

	memcpy(regs.r, ctx.r, sizeof(regs.r));
	memcpy(regs.r_bank, ctx.r_bank, sizeof(regs.r_bank));
	regs.pc = ctx.pc;
	regs.pr = ctx._pr;
	regs.sr = sh4_sr_GetFull();
	regs.gbr = ctx.gbr;
	regs.vbr = ctx.vbr;
	regs.ssr = ctx.ssr;
	regs.spc = ctx.spc;
	regs.sgr = ctx.sgr;
	regs.dbr = ctx.dbr;
	regs.mach = ctx.mac.h;
	regs.macl = ctx.mac.l;
	regs.fpscr = ctx.fpscr.full;
	regs.fpul = ctx.fpul;
	memcpy(regs.fr_xf, ctx.xffr, sizeof(regs.fr_xf));
	memcpy(regs.sq, p_sh4rcb->sq_buffer, sizeof(regs.sq));

	return hash_of(&regs, sizeof(regs));
}

// What a device puts into a save state
u64 hash_device(Area0Hanlders slot)
{
	MMIODevice* device = sh4_cpu->GetA0Handler(slot);

	// without a buffer it only counts
	void* data = nullptr;
	unsigned int size = 0;
	device->serialize(&data, &size);

	std::vector<u8> buffer(size);
	data = buffer.data();
	size = 0;
	device->serialize(&data, &size);

	return hash_of(buffer.data(), buffer.size());
}

// Emulator thread, the CPU has stopped
void finish()
{
	RunControl& rc = runctl();

	u64 now = sh4_sched_now64();
	double wall = 0;
	double speed = 0;

	if (rc.vblanks != 0)
	{
		wall = std::chrono::duration<double>(wall_clock::now() - rc.first_vblank_wall).count();
		if (wall > 0)
			speed = (double)(now - rc.first_vblank_cycles) / SH4_MAIN_CLOCK / wall * 100;
	}

	// the guest's serial output may have left the cursor in the middle of a line
	printf("\nnulldc-exit: reason=%s emu_s=%.6f vblanks=%u wall_s=%.6f speed_pct=%.1f",
		rc.exit_reason, (double)now / SH4_MAIN_CLOCK, rc.vblanks, wall, speed);

	if (rc.state_hash)
	{
		u64 parts[6];

		parts[0] = hash_sh4();
		parts[1] = now;
		parts[2] = hash_of(sh4_cpu->mram.data, sh4_cpu->mram.size);
		parts[3] = hash_of(sh4_cpu->vram.data, sh4_cpu->vram.size);
		parts[4] = hash_of(sh4_cpu->aica_ram.data, sh4_cpu->aica_ram.size);
		parts[5] = hash_device(A0H_SCPU);

		printf(" hash=%016llx sh4=%016llx sched=%llu ram=%016llx vram=%016llx aram=%016llx arm7=%016llx",
			(unsigned long long)hash_of(parts, sizeof(parts)),
			(unsigned long long)parts[0], (unsigned long long)parts[1], (unsigned long long)parts[2],
			(unsigned long long)parts[3], (unsigned long long)parts[4], (unsigned long long)parts[5]);
	}

	printf("\n");
	fflush(stdout);

	g_GUIRenderer->Stop();
}

// Emulator thread, the CPU is running: it stops at the end of the time slice, then finish() runs
void request_exit(const char* reason)
{
	RunControl& rc = runctl();

	if (rc.exiting)
		return;

	rc.exiting = true;
	rc.exit_reason = reason;

	virtualDreamcast->Stop([] { finish(); });
}

}

void runctl_configure()
{
	RunControl& rc = runctl();

	rc.exit_vblanks = (u32)std::max<s32>(0, cfgLoadInt("config", "Exit.AfterVblanks", 0));

	double seconds = atof(cfgLoadStr("config", "Exit.AfterSeconds", "0").c_str());
	rc.exit_cycles = seconds > 0 ? (u64)llround(seconds * SH4_MAIN_CLOCK) : 0;

	rc.exit_on_guest_exit = cfgLoadBool("config", "Exit.OnGuestExit", false);
	rc.state_hash = cfgLoadBool("config", "Exit.StateHash", false);

	// A run that ends by itself leaves emu.cfg as it is
	if (rc.exit_vblanks != 0 || rc.exit_cycles != 0 || rc.exit_on_guest_exit)
		cfgSetReadOnly();
}

void runctl_vblank()
{
	RunControl& rc = runctl();

	if (rc.vblanks++ == 0)
	{
		rc.first_vblank_cycles = sh4_sched_now64();
		rc.first_vblank_wall = wall_clock::now();
	}

	if (rc.exit_vblanks != 0 && rc.vblanks >= rc.exit_vblanks)
		request_exit("vblanks");
	else if (rc.exit_cycles != 0 && sh4_sched_now64() >= rc.exit_cycles)
		request_exit("seconds");
}

bool runctl_guest_exit()
{
	RunControl& rc = runctl();

	if (!rc.exit_on_guest_exit)
		return false;

	request_exit("guest-exit");
	return true;
}
