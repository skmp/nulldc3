/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "types.h"
#include "cfg/cfg.h"

#if HOST_OS==OS_LINUX
#include <poll.h>
#include <termios.h>
//#include <curses.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/maple/maple_cfg.h"
#include <unistd.h>
#include <pty.h>
#include <errno.h>
#include <string.h>

#include "libswirl.h"
#include "hw/pvr/Renderer_if.h"
#include "utils/run_control.h"

#if defined(TARGET_EMSCRIPTEN)
	#include "emscripten.h"
#endif


#if defined(SUPPORT_X11)
	#include "linux-dist/x11.h"
#endif

#if defined(USE_SDL)
	#include "sdl/sdl.h"
#endif

#if defined(USE_EVDEV)
	#include "linux-dist/evdev.h"
#endif

#if defined(USE_JOYSTICK)
	#include "linux-dist/joystick.h"
#endif


#if FEAT_HAS_NIXPROF
#include "profiler/profiler.h"
#endif

#if defined(SUPPORT_GLX)
#include "utils/glinit/glx/glx.h"
#endif

#if defined(SUPPORT_EGL)
#include "utils/glinit/egl/egl.h"
#endif

#if defined(SUPPORT_SDL)
#include "utils/glinit/sdl/sdl.h"
#endif

void* x11_win = 0;
void* x11_disp = 0;


void* sdl_win = 0;
void* sdl_disp = 0;

void* libPvr_GetRenderTarget()
{
	return x11_win;
}

void* libPvr_GetRenderSurface()
{
	return x11_disp;
}

u16 kcode[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
u8 rt[4] = {0, 0, 0, 0};
u8 lt[4] = {0, 0, 0, 0};
u32 vks[4];
s8 joyx[4], joyy[4];

void emit_WriteCodeCache();

#if defined(USE_JOYSTICK)
	/* legacy joystick input */
	static int joystick_fd = -1; // Joystick file descriptor
#endif

void os_SetupInput()
{
	// a headless run has no host input: nothing a person or a device does reaches the guest
	if (runctl_headless())
		return;

#if defined(USE_EVDEV)
	input_evdev_init();
#endif

#if defined(USE_JOYSTICK)
	int joystick_device_id = cfgLoadInt("input", "joystick_device_id", JOYSTICK_DEFAULT_DEVICE_ID);
	if (joystick_device_id < 0) {
		puts("Legacy Joystick input disabled by config.\n");
	}
	else
	{
		int joystick_device_length = snprintf(NULL, 0, JOYSTICK_DEVICE_STRING, joystick_device_id);
		char* joystick_device = (char*)malloc(joystick_device_length + 1);
		sprintf(joystick_device, JOYSTICK_DEVICE_STRING, joystick_device_id);
		joystick_fd = input_joystick_init(joystick_device);
		free(joystick_device);
	}
#endif

#if defined(SUPPORT_X11)
	input_x11_init();
#endif

#if defined(USE_SDL)
	input_sdl_init();
#endif

#if defined(TARGET_EMSCRIPTEN)
	input_emscripten_init();
#endif
}

void UpdateInputState(u32 port)
{
	#if defined(TARGET_EMSCRIPTEN)
		return;
	#endif

	#if defined(USE_JOYSTICK)
		input_joystick_handle(joystick_fd, port);
	#endif

	#if defined(USE_EVDEV)
		input_evdev_handle(port);
	#endif

	#if defined(USE_SDL)
		input_sdl_handle(port);
	#endif
}

void os_DoEvents()
{
	#if defined(SUPPORT_X11)
		input_x11_handle();
		event_x11_handle();
	#endif
}

void os_SetWindowText(const char * text)
{
	#if defined(SUPPORT_X11)
		x11_window_set_text(text);
	#elif defined(USE_SDL)
		sdl_window_set_text(text);
	#else
		printf("Status: %s\n", text);
	#endif
}

void os_LaunchFromURL(const string& url)
{
	auto cmd = "xdg-open " + url; 
	auto rv = system(cmd.c_str());
}

void os_CreateWindow()
{
	if (runctl_headless())
		return;

	#if defined(SUPPORT_X11)
		x11_window_create();
	#endif
	#if defined(USE_SDL)
		sdl_window_create(&sdl_win, &sdl_disp);
	#endif
}


// The directories a run can keep its files in (cfg/cl.cpp chooses): the one of the executable, and the XDG ones
// of --shared, $XDG_CONFIG_HOME/nulldc3 and $XDG_DATA_HOME/nulldc3 with the XDG system lists behind them. The
// reicast directories of before (~/.reicast, the XDG ones named reicast) are not read.
static string xdg_home(const char* variable, const char* fallback, string& why)
{
	// a value that is not an absolute path is invalid and ignored (XDG Base Directory Specification)
	const char* value = getenv(variable);
	if (value != NULL && value[0] == '/')
		return string(value) + "/nulldc3";
	const char* home = getenv("HOME");
	if (home != NULL && home[0] == '/')
		return string(home) + fallback + "/nulldc3";
	why = string("neither ") + variable + " nor HOME is set to an absolute path";
	return "";
}

static vector<string> xdg_list(const char* variable, const char* fallback)
{
	const char* value = getenv(variable);
	string list = value != NULL && value[0] != 0 ? value : fallback;
	vector<string> dirs;
	string::size_type pos = 0;
	for (;;)
	{
		string::size_type n = list.find(':', pos);
		string dir = list.substr(pos, n == string::npos ? string::npos : n - pos);
		while (dir.size() > 1 && dir.back() == '/')
			dir.pop_back();
		if (!dir.empty() && dir[0] == '/')
			dirs.push_back(dir + "/nulldc3");
		if (n == string::npos)
			break;
		pos = n + 1;
	}
	return dirs;
}

static HostDirs find_host_dirs()
{
	HostDirs dirs;

	// /proc/self/exe is the file the kernel runs, with every symbolic link resolved: started through a link, the
	// directory is the one of the file the link points at. Never the working directory.
	vector<char> path(4096);
	for (;;)
	{
		ssize_t n = readlink("/proc/self/exe", path.data(), path.size());
		if (n < 0)
		{
			dirs.exe_dir_error = string("/proc/self/exe: ") + strerror(errno);
			break;
		}
		if ((size_t)n < path.size())
		{
			string exe(path.data(), n);
			string::size_type slash = exe.find_last_of('/');
			if (slash == string::npos)
				dirs.exe_dir_error = "/proc/self/exe is " + exe;
			else
				dirs.exe_dir = slash == 0 ? "/" : exe.substr(0, slash);
			break;
		}
		path.resize(path.size() * 2);
	}

	string why_config, why_data;
	string config = xdg_home("XDG_CONFIG_HOME", "/.config", why_config);
	string data = xdg_home("XDG_DATA_HOME", "/.local/share", why_data);
	if (config.empty() || data.empty())
		dirs.shared_error = config.empty() ? why_config : why_data;
	else
	{
		dirs.shared_config = config;
		dirs.shared_data = data;
		dirs.shared_config_ro = xdg_list("XDG_CONFIG_DIRS", "/etc/xdg");
		dirs.shared_data_ro = xdg_list("XDG_DATA_DIRS", "/usr/local/share/:/usr/share/");
	}
	return dirs;
}

void common_linux_setup();
void common_linux_check_pages();
void common_linux_fault_block(bool block);
#if FEAT_HAS_SERIAL_TTY
bool common_serial_pty_setup();
#endif

bool os_gl_init(void* hwnd, void* hdc)
{
	#if defined(SUPPORT_GLX)
		return glx_Init(x11_win, x11_disp);
	#elif defined(SUPPORT_EGL)
		return egl_Init(x11_win, x11_disp);
	#elif defined(SUPPORT_SDL)
		return sdlgl_Init(sdl_win, sdl_win);
	#else
		#error "only glx/egl/sdl supported"
		return true;
	#endif
}

bool os_gl_swap()
{
	#if defined(SUPPORT_GLX)
		return glx_Swap();
	#elif defined(SUPPORT_EGL)
		return egl_Swap();
	#elif defined(SUPPORT_SDL)
		return sdlgl_Swap();
	#else
		#error "only glx/egl/sdl supported"
		return true;
	#endif
}

void os_gl_term()
{
	#if defined(SUPPORT_GLX)
		glx_Term();
	#elif defined(SUPPORT_EGL)
		egl_Term();
	#elif defined(SUPPORT_SDL)
		return sdlgl_Term();
	#else
		#error "only glx/egl/sdl supported"
	#endif
}

// The first of --help and --version in front of the first "--" is --version
static bool asks_version(int argc, wchar* argv[])
{
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--") == 0 || strcmp(argv[i], "--help") == 0)
			return false;
		if (strcmp(argv[i], "--version") == 0)
			return true;
	}
	return false;
}

int main(int argc, wchar* argv[])
{
	// A process that a signal ends loses what stdio still holds: with a file or a pipe behind stdout
	// that would be the lines right before the fault
	setvbuf(stdout, nullptr, _IOLBF, 0);


	// the directories to choose from, the command line chooses in nulldc_init()
	cl_SetHostDirs(find_host_dirs());

	// --version is answered before the setup below prints its lines on stdout, so that its one line is the whole
	// stdout: when the first --help or --version in front of a "--" is --version, the command line is read here,
	// and that is either the version's line or the command line refused before it (a --version as the value of
	// --portable or --config is refused too); --help goes on as before. A Linux arm64 kernel whose pages are not
	// the build's is refused before either, as it is for every command line
	if (asks_version(argc, argv))
	{
		common_linux_check_pages();
		return ParseCommandLine(argc, argv) == rv_cli_finish ? 0 : 2;
	}

	#if defined(USE_SDL)
		if (SDL_Init(0) != 0)
		{
			die("SDL: Initialization failed!");
		}
	#endif

	common_linux_setup();

	settings.profile.run_counts=0;

	int init_rv = nulldc_init(argc, argv);
	if (init_rv == rv_cli_finish)
		return 0;	// --help: the usage text is all that was asked for
	if (init_rv == rv_cli_error)
		return 2;	// the command line was refused, cfg/cl.cpp has said why on stderr
	if (init_rv)
		die("nullDC initialization failed\n");

	// see fatal_error() in linux/common.cpp, the environment has the same switch for faults before this point
	if (cfgLoadBool("config", "Debug.BlockOnFault", false))
		common_linux_fault_block(true);
    
    #if FEAT_HAS_SERIAL_TTY
    bool cleanup_pty_symlink = common_serial_pty_setup();
    #endif

	#if FEAT_HAS_NIXPROF
	install_prof_handler(1);
	#endif
	
	nulldc_ui_loop();
#if FEAT_HAS_SERIAL_TTY
	if (cleanup_pty_symlink)
	{
		unlink(settings.debug.VirtualSerialPortFile.c_str());
	}
#endif
	nulldc_term();


	#if defined(USE_EVDEV)
		input_evdev_close();
	#endif

	#if defined(SUPPORT_X11)
		x11_window_destroy();
	#endif

	return runctl_exit_code();
}
#endif
 
int get_mic_data(u8* buffer) { return 0; }

void os_DebugBreak()
{
	#if !defined(TARGET_EMSCRIPTEN)
		raise(SIGTRAP);
	#else
		printf("DEBUGBREAK!\n");
		exit(-1);
	#endif
}



