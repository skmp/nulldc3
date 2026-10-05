/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "run_control.h"

#include <atomic>
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

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "deps/stb/stb_image_write.h"

namespace {

typedef std::chrono::steady_clock wall_clock;

struct RunControl
{
	// the keys. Written once by runctl_configure, while the emulator starts and before the emulator thread
	// exists; only read from then on, by any thread
	bool headless = false;
	u32 exit_vblanks = 0;		// 0: no limit
	u64 exit_cycles = 0;		// 0: no limit
	bool exit_on_guest_exit = false;
	bool state_hash = false;
	bool frame_hash = false;
	string frame_png;

	// A run that has no user interface, or that a key ends: nobody is there to act on what it does
	bool automated() const
	{
		return headless || exit_vblanks != 0 || exit_cycles != 0 || exit_on_guest_exit;
	}

	// the run. Emulator thread, but for exit_code: the emulator thread and the thread of the user interface set
	// it, main returns it
	u32 vblanks = 0;
	bool exiting = false;
	const char* exit_reason = "";
	u64 first_vblank_cycles = 0;
	wall_clock::time_point first_vblank_wall;
	std::atomic<int> exit_code { 0 };

	// the last frame a renderer presented. Written by runctl_present on the thread that presents: the thread of
	// the user interface with a window, the emulator thread headless. Read by finish() on the emulator thread,
	// which runs after WaitQueueEmpty(): no present is queued or running by then
	std::vector<u32> frame;
	int frame_width = 0;
	int frame_height = 0;
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

	if (rc.frame_hash)
	{
		if (rc.frame.empty())
			printf(" frame=none");
		else
			printf(" frame=%016llx frame_size=%dx%d",
				(unsigned long long)hash_of(rc.frame.data(), rc.frame.size() * sizeof(u32)), rc.frame_width, rc.frame_height);
	}

	printf("\n");
	fflush(stdout);

	if (!rc.frame_png.empty() && !rc.frame.empty())
	{
		// B G R A to R G B A, opaque: the framebuffer has no alpha
		std::vector<u32> rgba(rc.frame.size());
		for (size_t i = 0; i < rgba.size(); i++)
		{
			u32 p = rc.frame[i];
			rgba[i] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
		}

		if (!stbi_write_png(rc.frame_png.c_str(), rc.frame_width, rc.frame_height, 4, rgba.data(), rc.frame_width * 4))
		{
			printf("Exit.FramePng: cannot write %s\n", rc.frame_png.c_str());
			rc.exit_code = 1;
		}
	}

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

	rc.headless = cfgLoadBool("config", "Headless", false);
	rc.exit_vblanks = (u32)std::max<s32>(0, cfgLoadInt("config", "Exit.AfterVblanks", 0));

	double seconds = atof(cfgLoadStr("config", "Exit.AfterSeconds", "0").c_str());
	rc.exit_cycles = seconds > 0 ? (u64)llround(seconds * SH4_MAIN_CLOCK) : 0;

	rc.exit_on_guest_exit = cfgLoadBool("config", "Exit.OnGuestExit", false);
	rc.state_hash = cfgLoadBool("config", "Exit.StateHash", false);
	rc.frame_png = cfgLoadStr("config", "Exit.FramePng", "");
	rc.frame_hash = cfgLoadBool("config", "Exit.FrameHash", false) || !rc.frame_png.empty();

	// A run that ends by itself, or that has no user interface to change a setting with, leaves emu.cfg as it is
	if (rc.automated())
		cfgSetReadOnly();
}

bool runctl_headless()
{
	return runctl().headless;
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

	// Only a run with a person in front of it goes on with a stopped machine. In an automated one nothing can
	// follow the end of the program: there is no user interface, or no vblank left for another exit key to
	// end the run at
	if (!rc.automated())
		return false;

	request_exit("guest-exit");
	return true;
}

bool runctl_present(const u32* pixels, int width, int height)
{
	RunControl& rc = runctl();

	if (rc.frame_hash && width > 0 && height > 0)
	{
		rc.frame.assign(pixels, pixels + (size_t)width * height);
		rc.frame_width = width;
		rc.frame_height = height;
	}

	return rc.headless;
}

void runctl_fail(const char* reason)
{
	printf("nulldc-exit: reason=error error=\"%s\"\n", reason);
	fflush(stdout);

	runctl().exit_code = 1;
}

int runctl_exit_code()
{
	return runctl().exit_code;
}
