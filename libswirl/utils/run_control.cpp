/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "run_control.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include "cfg/cfg.h"
#include "libswirl.h"
#include "gui/gui_renderer.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_mmio.h"
#include "hw/pvr/pvr_coredump.h"

#include "deps/xxhash/xxhash.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "deps/stb/stb_image_write.h"

// the controller of port A, as the maple bus reads it (the platform's main has them)
extern u16 kcode[4];
extern u8 rt[4], lt[4];
extern s8 joyx[4], joyy[4];

namespace {

typedef std::chrono::steady_clock wall_clock;

// A line of config:Script.File: at the vblank, what to do
struct ScriptStep
{
	u32 vblank = 0;
	string op;              // press release tap stick lt rt png dump exit
	u16 buttons = 0;        // press release tap: DC_BTN_* bits
	int a = 0, b = 0;       // tap: vblanks held; stick: x, y; lt rt: value
	string path;            // png dump
};

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
	std::vector<ScriptStep> script;		// config:Script.File, in vblank order
	u32 shot_every = 0;					// config:Script.ShotEvery: a PNG every N vblanks into shot_dir
	string shot_dir;
	bool capture() const { return frame_hash || !script.empty() || shot_every != 0; }

	// A run that has no user interface, or that a key ends: nobody is there to act on what it does
	bool automated() const
	{
		return headless || exit_vblanks != 0 || exit_cycles != 0 || exit_on_guest_exit || !script.empty();
	}

	// the run. Emulator thread, but for exit_code: the emulator thread and the thread of the user interface set
	// it, main returns it
	u32 vblanks = 0;
	bool exiting = false;
	const char* exit_reason = "";
	u64 first_vblank_cycles = 0;
	wall_clock::time_point first_vblank_wall;
	std::atomic<int> exit_code { 0 };
	size_t script_next = 0;					// the next step of the script
	std::vector<std::pair<u32, u16>> taps;	// the buttons of a tap, and the vblank they are let go at

	// the last frame a renderer presented. Written by runctl_present on the thread that presents: the thread of
	// the user interface with a window, the emulator thread headless. Read by finish() on the emulator thread,
	// which runs after WaitQueueEmpty(): no present is queued or running by then
	std::vector<u32> frame;
	int frame_width = 0;
	int frame_height = 0;
	std::mutex frame_lock;			// a script's png reads the frame on the emulator thread; with a window the
									// user interface's thread writes it
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

// The last presented frame as a PNG. False: no frame yet, or the file could not be written
bool write_frame_png(const string& path)
{
	RunControl& rc = runctl();
	std::vector<u32> rgba;
	int width, height;
	{
		std::lock_guard<std::mutex> hold(rc.frame_lock);
		if (rc.frame.empty())
			return false;
		width = rc.frame_width;
		height = rc.frame_height;
		// B G R A to R G B A, opaque: the framebuffer has no alpha
		rgba.resize(rc.frame.size());
		for (size_t i = 0; i < rgba.size(); i++)
		{
			u32 p = rc.frame[i];
			rgba[i] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
		}
	}
	return stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4) != 0;
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

	if (!rc.frame_png.empty() && !rc.frame.empty() && !write_frame_png(rc.frame_png))
	{
		printf("Exit.FramePng: cannot write %s\n", rc.frame_png.c_str());
		rc.exit_code = 1;
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

// "A+START" -> the DC_BTN_* bits; false for a name that is none
bool parse_buttons(const string& text, u16& bits)
{
	static const struct { const char* name; u16 bit; } names[] = {
		{ "C", 1 << 0 }, { "B", 1 << 1 }, { "A", 1 << 2 }, { "START", 1 << 3 }, { "UP", 1 << 4 }, { "DOWN", 1 << 5 },
		{ "LEFT", 1 << 6 }, { "RIGHT", 1 << 7 }, { "Z", 1 << 8 }, { "Y", 1 << 9 }, { "X", 1 << 10 }, { "D", 1 << 11 },
		{ "UP2", 1 << 12 }, { "DOWN2", 1 << 13 }, { "LEFT2", 1 << 14 }, { "RIGHT2", 1 << 15 },
	};
	bits = 0;
	std::stringstream parts(text);
	string part;
	while (std::getline(parts, part, '+'))
	{
		std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) { return (char)toupper(c); });
		bool found = false;
		for (auto& n : names)
			if (part == n.name) { bits |= n.bit; found = true; }
		if (part == "ALL") { bits = 0xFFFF; found = true; }
		if (!found)
			return false;
	}
	return bits != 0;
}

// config:Script.File: "<vblank> <op> [arguments]" a line, # a comment. False (and said) on a line it cannot read
bool load_script(const string& path, std::vector<ScriptStep>& out)
{
	std::ifstream in(path);
	if (!in)
	{
		printf("Script.File: cannot read %s\n", path.c_str());
		return false;
	}
	string line;
	int number = 0;
	while (std::getline(in, line))
	{
		number++;
		size_t hash = line.find('#');
		if (hash != string::npos)
			line.resize(hash);
		std::stringstream words(line);
		ScriptStep st;
		long long vb;
		if (!(words >> vb))
			continue;						// empty
		st.vblank = (u32)std::max(0LL, vb);
		bool ok = (bool)(words >> st.op);
		string arg;
		if (ok && (st.op == "press" || st.op == "release" || st.op == "tap"))
		{
			ok = (bool)(words >> arg) && parse_buttons(arg, st.buttons);
			st.a = 4;
			if (ok && st.op == "tap")
				words >> st.a;
		}
		else if (ok && st.op == "stick")
			ok = (bool)(words >> st.a >> st.b);
		else if (ok && (st.op == "lt" || st.op == "rt"))
			ok = (bool)(words >> st.a);
		else if (ok && (st.op == "png" || st.op == "dump"))
		{
			std::getline(words, st.path);
			st.path.erase(0, st.path.find_first_not_of(" \t"));
			st.path.erase(st.path.find_last_not_of(" \t\r") + 1);
			ok = !st.path.empty();
		}
		else if (ok && st.op != "exit")
			ok = false;
		if (!ok)
		{
			printf("Script.File: %s line %d: what is \"%s\"?\n", path.c_str(), number, line.c_str());
			return false;
		}
		out.push_back(st);
	}
	std::stable_sort(out.begin(), out.end(), [](const ScriptStep& p, const ScriptStep& q) { return p.vblank < q.vblank; });
	return true;
}

// Emulator thread, at a vblank: the script's steps that are due, the taps that end, the periodic picture
void run_script(RunControl& rc)
{
	for (size_t k = 0; k < rc.taps.size();)
	{
		if (rc.vblanks >= rc.taps[k].first)
		{
			kcode[0] |= rc.taps[k].second;
			rc.taps.erase(rc.taps.begin() + k);
		}
		else
			k++;
	}

	while (rc.script_next < rc.script.size() && rc.script[rc.script_next].vblank <= rc.vblanks)
	{
		const ScriptStep& st = rc.script[rc.script_next++];
		if (st.op == "press")
			kcode[0] &= (u16)~st.buttons;
		else if (st.op == "release")
			kcode[0] |= st.buttons;
		else if (st.op == "tap")
		{
			kcode[0] &= (u16)~st.buttons;
			rc.taps.push_back({ rc.vblanks + (u32)std::max(1, st.a), st.buttons });
		}
		else if (st.op == "stick")
		{
			joyx[0] = (s8)std::max(-128, std::min(127, st.a));
			joyy[0] = (s8)std::max(-128, std::min(127, st.b));
		}
		else if (st.op == "lt")
			lt[0] = (u8)std::max(0, std::min(255, st.a));
		else if (st.op == "rt")
			rt[0] = (u8)std::max(0, std::min(255, st.a));
		else if (st.op == "png")
		{
			if (!write_frame_png(st.path))
				printf("Script.File: no picture for %s at vblank %u\n", st.path.c_str(), rc.vblanks);
		}
		else if (st.op == "dump")
			pvr_coredump_request(st.path);
		else if (st.op == "exit")
			request_exit("script");
	}

	if (rc.shot_every != 0 && rc.vblanks % rc.shot_every == 0)
	{
		char name[32];
		snprintf(name, sizeof(name), "/%06u.png", rc.vblanks);
		write_frame_png(rc.shot_dir + name);
	}
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

	string script = cfgLoadStr("config", "Script.File", "");
	if (!script.empty() && !load_script(script, rc.script))
	{
		// a run that was to be driven by a script does not start without it
		printf("nulldc-exit: reason=error error=\"Script.File\"\n");
		fflush(stdout);
		exit(1);
	}
	rc.shot_every = (u32)std::max<s32>(0, cfgLoadInt("config", "Script.ShotEvery", 0));
	rc.shot_dir = cfgLoadStr("config", "Script.ShotDir", ".");

	// A run that ends by itself, or that has no user interface to change a setting with, leaves emu.cfg as it is
	if (rc.automated())
		cfgSetReadOnly();
}

bool runctl_headless()
{
	return runctl().headless;
}

bool runctl_automated()
{
	return runctl().automated();
}

void runctl_vblank()
{
	RunControl& rc = runctl();

	if (rc.vblanks++ == 0)
	{
		rc.first_vblank_cycles = sh4_sched_now64();
		rc.first_vblank_wall = wall_clock::now();
	}

	if (!rc.script.empty() || rc.shot_every != 0)
		run_script(rc);

	if (rc.exiting)
		return;							// the script's exit
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

	if (rc.capture() && width > 0 && height > 0)
	{
		std::lock_guard<std::mutex> hold(rc.frame_lock);
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
