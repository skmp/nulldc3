/*
	runctl.guest_exit.*: what the end of the guest program does to a run, by the keys the run was started with.
	utils/run_control.cpp on the config store and the command line parser, with stand-ins for the machine and
	for the user interface.

	    run_control_guest_exit <case> <work directory>

	A program that returns to the BIOS stops the CPU, and no vblank follows a stopped CPU. A run that
	Exit.AfterVblanks or Exit.AfterSeconds was to end would stay for ever, with or without a window, so the end
	of the program ends every run that has no user interface or that one of the Exit keys ends. Only a run with
	none of them keeps its window, with the machine stopped.

	The keys are read once per process: one case per invocation.
*/
#include "types.h"
#include "cfg/cfg.h"
#include "libswirl.h"
#include "gui/gui_renderer.h"
#include "hw/sh4/sh4_sched.h"
#include "utils/run_control.h"

#include "deps/xxhash/xxhash.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// What cfg.cpp and cl.cpp take from stdclass.cpp
static std::string work_dir;

string get_writable_config_path(const string& filename) { return work_dir + filename; }
string get_readonly_config_path(const string& filename) { return work_dir + filename; }
void set_user_config_dir(const string& dir) { }
void set_user_data_dir(const string& dir) { }
void add_system_config_dir(const string& dir) { }
void add_system_data_dir(const string& dir) { }
void clear_dirs() { }

// The machine. A run that ends asks it to stop the CPU, and hands over what the emulator thread does once it has
struct TestDreamcast final : VirtualDreamcast
{
	int stops = 0;
	function<void()> stopped;

	void Stop(function<void()> callback) override { stops++; stopped = callback; }

	void LoadState() override { }
	void SaveState() override { }
	void Reset() override { }
	void Resume() override { }
	bool Init() override { return true; }
	void Term() override { }
	int StartGame(const string& path) override { return 0; }
	void RequestReset() override { }
	bool HandleFault(unat addr, rei_host_context_t* ctx) override { return false; }
};

// The user interface. Its loop, and with it the process, ends when it is stopped
struct TestGUIRenderer final : GUIRenderer
{
	int stops = 0;

	void Stop() override { stops++; }

	void Start() override { }
	void UILoop() override { }
	void QueueEmulatorFrame(std::function<bool()> callback) override { }
	void WaitQueueEmpty() override { }
	void UIFrame() override { }
	bool CreateContext() override { return true; }
};

MachinePtr virtualDreamcast;
std::unique_ptr<GUIRenderer> g_GUIRenderer;

// 1 emulated second into the run, which is before the limit of every case
u64 sh4_sched_now64() { return SH4_MAIN_CLOCK; }

// Only the state and frame hashes of the exit line read the machine, and no case asks for one
SuperH4* sh4_cpu;
Sh4RCB* p_sh4rcb;

XXH64_hash_t XXH64(const void* input, size_t length, unsigned long long seed)
{
	fprintf(stderr, "a hash nobody asked for\n");
	abort();
}

struct Case
{
	const char* name;
	const char* key;	// the one key of the run besides the program, nullptr: none
	bool headless;
	bool ends;			// the end of the guest program ends the run
};

static const Case cases[] = {
	{ "window",   nullptr,                         false, false },	// a person's run: the window stays
	{ "key",      "config:Exit.OnGuestExit=yes",   false, true },
	{ "vblanks",  "config:Exit.AfterVblanks=100",  false, true },
	{ "seconds",  "config:Exit.AfterSeconds=2",    false, true },
	{ "headless", "config:Headless=yes",           true,  true },
};

static int failed = 0;

#define CHECK(condition) \
	do { if (!(condition)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); failed++; } } while (0)

int main(int argc, char* argv[])
{
	const Case* test = nullptr;

	for (const Case& c : cases)
	{
		if (argc == 3 && strcmp(argv[1], c.name) == 0)
			test = &c;
	}

	if (test == nullptr)
	{
		fprintf(stderr, "usage: run_control_guest_exit window|key|vblanks|seconds|headless <work directory>\n");
		return 2;
	}

	work_dir = argv[2];
	std::filesystem::create_directories(work_dir);

	// the command line of the run, as main gets it
	std::vector<std::string> words = { "emulator", "--portable", work_dir };
	if (test->key != nullptr)
	{
		words.push_back("--config");
		words.push_back(test->key);
	}
	words.push_back("program.elf");

	std::vector<char*> args;
	for (auto& word : words)
		args.push_back(&word[0]);

	CHECK(ParseCommandLine((int)args.size(), args.data()) == rv_ok);

	TestDreamcast* machine = new TestDreamcast();
	TestGUIRenderer* gui = new TestGUIRenderer();
	virtualDreamcast.reset(machine);
	g_GUIRenderer.reset(gui);

	runctl_configure();
	CHECK(runctl_headless() == test->headless);

	// a few vblanks into the run, before any limit, the program returns to the BIOS
	runctl_vblank();
	runctl_vblank();
	CHECK(machine->stops == 0);

	bool ended = runctl_guest_exit();

	CHECK(ended == test->ends);
	CHECK(machine->stops == (test->ends ? 1 : 0));
	CHECK(gui->stops == 0);

	if (machine->stopped)
	{
		// the CPU has stopped: the emulator thread prints the exit line and stops the user interface
		std::string path = work_dir + "/stdout.txt";
		CHECK(freopen(path.c_str(), "w", stdout) != nullptr);
		machine->stopped();
		fflush(stdout);

		std::stringstream text;
		text << std::ifstream(path).rdbuf();

		if (text.str().find("nulldc-exit: reason=guest-exit emu_s=1.000000 vblanks=2 ") == std::string::npos)
		{
			fprintf(stderr, "not the exit line of a guest exit:\n%s\n", text.str().c_str());
			failed++;
		}

		CHECK(gui->stops == 1);
		CHECK(runctl_exit_code() == 0);

		// the run ends once
		runctl_vblank();
		CHECK(runctl_guest_exit());
		CHECK(machine->stops == 1);
	}

	if (failed)
	{
		fprintf(stderr, "runctl.guest_exit.%s: FAIL (%d checks)\n", test->name, failed);
		return 1;
	}

	fprintf(stderr, "runctl.guest_exit.%s: PASS\n", test->name);
	return 0;
}
