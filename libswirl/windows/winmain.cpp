/*
	This file is part of libswirl
*/
#include "license/bsd"

// Without /EHsc nlohmann's JSON_THROW is std::abort() and unwinding skips destructors (nulldc3/cmake/config.cmake)
#if defined(_MSC_VER) && !defined(_CPPUNWIND)
#error "MSVC build without /EHsc: JSON_THROW would be std::abort"
#endif

#include "win_common.h"
#include "win_fatal.h"

#include <shlobj.h>		// SHGetKnownFolderPath
#include <knownfolders.h>
#include <io.h>			// _get_osfhandle, _setmode
#include <fcntl.h>		// _O_BINARY

#include "utils/run_control.h"

// Gamepads
u16 kcode[4] = { 0xffff, 0xffff, 0xffff, 0xffff };
u32 vks[4];
s8 joyx[4], joyy[4];
u8 rt[4], lt[4];
// Mouse
extern s32 mo_x_abs;
extern s32 mo_y_abs;
extern u32 mo_buttons;
extern f32 mo_x_delta;
extern f32 mo_y_delta;
extern f32 mo_wheel_delta;
// Keyboard
static Win32KeyboardDevice keyboard(0);

extern int screen_width, screen_height;
bool window_maximized = false;


LONG ExeptionHandler(EXCEPTION_POINTERS *ExceptionInfo)
{
	EXCEPTION_POINTERS* ep = ExceptionInfo;

	u32 dwCode = ep->ExceptionRecord->ExceptionCode;

	EXCEPTION_RECORD* pExceptionRecord=ep->ExceptionRecord;

	if (dwCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;

	unat address=(unat)pExceptionRecord->ExceptionInformation[1];

	rei_host_context_t ctx = { 0 };

	// TODO: Implement context abstraction for windows
	#if HOST_CPU == CPU_X64
		ctx.pc = (unat&)ep->ContextRecord->Rip;
	#else
		#error missing arch for windows
	#endif

	if (virtualDreamcast && virtualDreamcast->HandleFault(address, &ctx))
	{
		// TODO: Implement context abstraction for windows
		#if HOST_CPU == CPU_X64
			(unat&)ep->ContextRecord->Rip = ctx.pc;
		#else
			#error missing arch for windows
		#endif

		return EXCEPTION_CONTINUE_EXECUTION;
	}
	else
	{
		//printf("[GPF] Unhandled access to : 0x%X\n",(unat)address);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

// A path of the system in the code page the rest of the emulator opens files with, the ANSI one; false when the
// path has a character that page does not have. With UTF-8 as that page (the "Use Unicode UTF-8" option of
// Windows, or activeCodePage in a manifest) WideCharToMultiByte takes no WC_NO_BEST_FIT_CHARS and no
// lpUsedDefaultChar, it fails with them; every character has a form there, and only a broken UTF-16 sequence is lost
static bool narrow_path(const wchar_t* wide, string& out)
{
	UINT page = GetACP();
	bool utf8 = page == CP_UTF8;
	DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
	BOOL lossy = FALSE;
	BOOL* used_default = utf8 ? nullptr : &lossy;
	int n = WideCharToMultiByte(page, flags, wide, -1, nullptr, 0, nullptr, used_default);
	if (n <= 0 || lossy)
		return false;
	vector<char> text(n);
	if (WideCharToMultiByte(page, flags, wide, -1, text.data(), n, nullptr, used_default) != n || lossy)
		return false;
	out = text.data();
	return true;
}

// The directories a run can keep its files in, cfg/cl.cpp chooses: the one of the executable, and %APPDATA%\nulldc3
// for --shared
void SetupPath()
{
	HostDirs dirs;

	// the file of the executable, from the system: never the working directory
	vector<wchar_t> path(MAX_PATH);
	for (;;)
	{
		DWORD n = GetModuleFileNameW(NULL, path.data(), (DWORD)path.size());
		if (n == 0)
		{
			dirs.exe_dir_error = "GetModuleFileNameW failed with error " + std::to_string(GetLastError());
			break;
		}
		if (n < path.size())
		{
			wstring exe(path.data(), n);
			size_t separator = exe.find_last_of(L"\\/");
			wstring dir = separator == wstring::npos ? L"" : exe.substr(0, separator);
			if (!dir.empty() && dir.back() == L':')
				dir += L'\\';	// C:\nulldc3.exe is in C:\, not in the working directory of drive C
			if (dir.empty())
				dirs.exe_dir_error = "the path of the executable has no directory";
			else if (!narrow_path(dir.c_str(), dirs.exe_dir))
				dirs.exe_dir_error = "its path has a character that the ANSI code page of the system does not have";
			break;
		}
		path.resize(path.size() * 2);
	}

	PWSTR appdata = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &appdata)))
	{
		string dir;
		if (narrow_path(appdata, dir))
			dirs.shared_config = dirs.shared_data = dir + "\\nulldc3";
		else
			dirs.shared_error = "%APPDATA% has a character that the ANSI code page of the system does not have";
	}
	else
		dirs.shared_error = "the system names no %APPDATA% folder";
	CoTaskMemFree(appdata);

	cl_SetHostDirs(dirs);
}


void ToggleFullscreen()
{
	extern void* window_win;
	static RECT rSaved;
	static bool fullscreen=false;
	HWND hWnd = (HWND)window_win;

	fullscreen = !fullscreen;


	if (fullscreen)
	{
		GetWindowRect(hWnd, &rSaved);

		MONITORINFO mi = { sizeof(mi) };
		HMONITOR hmon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
		if (GetMonitorInfo(hmon, &mi)) {

			SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
			SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

			SetWindowPos(hWnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, 
				SWP_SHOWWINDOW|SWP_FRAMECHANGED|SWP_ASYNCWINDOWPOS);
		}
	}
	else {
		
		SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
		SetWindowLongPtr(hWnd, GWL_STYLE, WS_VISIBLE | WS_OVERLAPPEDWINDOW | (window_maximized ? WS_MAXIMIZE : 0));

		SetWindowPos(hWnd, NULL, rSaved.left, rSaved.top,
			rSaved.right - rSaved.left, rSaved.bottom - rSaved.top, 
			SWP_SHOWWINDOW|SWP_FRAMECHANGED|SWP_ASYNCWINDOWPOS|SWP_NOZORDER);
	}
}


// Whether what the emulator wrote to stream can still be read when the process has ended: in a file or a pipe, or
// in the console of the cmd.exe or PowerShell it was started from. Not when there is no console, nor in a console
// that has this process alone (the one Windows opens for a start from Explorer or a shortcut, or the one of
// --console): that one closes with it.
static bool output_is_kept(FILE* stream)
{
	int fd = _fileno(stream);
	if (fd < 0)
		return false;
	HANDLE handle = (HANDLE)_get_osfhandle(fd);
	if (handle == INVALID_HANDLE_VALUE || handle == NULL)
		return false;
	DWORD mode;
	if (!GetConsoleMode(handle, &mode))
		return true;	// a file, a pipe, or a device that is no console
	DWORD processes[2];
	return GetConsoleProcessList(processes, 2) > 1;
}

// Whether a --config item of the command line sets config:Headless, read ahead of the parser (cfg/cl.cpp, whose
// grammar this follows): the help and error box is for a person who started the emulator, never for a headless run,
// also one whose command line is refused. The last item wins, as in the parser; a value is true as cfgLoadBool has it.
static bool argv_headless(int argc, char** argv)
{
	bool headless = false;
	for (int i = 1; i < argc; i++)
	{
		string word = argv[i];
		if (word == "--" || word == "--help" || word == "--version")
			break;
		if (word != "--config")
			continue;
		while (i + 1 < argc)
		{
			string item = argv[++i];
			size_t colon = item.find(':');
			size_t equals = colon == string::npos ? string::npos : item.find('=', colon + 1);
			if (equals != string::npos)
			{
				auto trim = [](const string& t) {
					size_t first = t.find_first_not_of(" \t\r\n\v\f"), last = t.find_last_not_of(" \t\r\n\v\f");
					return first == string::npos ? string() : t.substr(first, last - first + 1);
				};
				string section = trim(item.substr(0, colon));
				string key = trim(item.substr(colon + 1, equals - colon - 1));
				string value = trim(item.substr(equals + 1));
				if (section == "config" && key == "Headless")
					headless = !stricmp(value.c_str(), "yes") || !stricmp(value.c_str(), "true") ||
					           !stricmp(value.c_str(), "on") || !stricmp(value.c_str(), "1");
			}
			if (i + 1 < argc && !strcmp(argv[i + 1], ","))
				i++;
			else
				break;
		}
	}
	return headless;
}

// nulldc3.exe is a console program (PL-D31): run from a terminal or by a harness it writes into the console or the
// handles it was given and never opens one of its own; started from Explorer, Windows gives it a console beside its
// window. --console and --log are accepted switches of the command line (cfg/cl.cpp).
#pragma comment(linker, "/subsystem:console")

int main(int argc, char **argv)
{
	// First of all: no dialog of any kind, and the fatal ends of the platform (win_fatal.cpp)
	win_install_fatal_policy();

	// The bytes Linux writes: no \r in front of every \n (libswirl.cpp makes both streams unbuffered)
	_setmode(_fileno(stdout), _O_BINARY);
	_setmode(_fileno(stderr), _O_BINARY);

	SetupPath();

	// the guest memory faults first; the fatal handler of the policy comes behind it
	AddVectoredExceptionHandler(1, ExeptionHandler);

	int init_rv = nulldc_init(argc, argv);
	if (init_rv == rv_cli_finish || init_rv == rv_cli_error)
	{
		// --help or --version (what was asked for is on stdout) or a command line that was refused (the reason is
		// on stderr): where that cannot be read once the process has ended, without a console or in one that
		// closes with the process, a message box shows it as well; never in a headless run
		bool finish = init_rv == rv_cli_finish;
		if (!output_is_kept(finish ? stdout : stderr) && !argv_headless(argc, argv))
			MessageBoxA(NULL, (finish ? cl_FinishText() : cl_ErrorText()).c_str(), "nullDC",
			            MB_OK | (finish ? MB_ICONINFORMATION : MB_ICONERROR));
		return finish ? 0 : 2;
	}
	if (init_rv != 0)
		die("nullDC initialization failed");

	win_test_fatal();

	nulldc_ui_loop();

	nulldc_term();

	extern bool window_class_registered;
	win_test_ui_report(window_class_registered, WINDOW_CLASS);

	cfgSaveBool("windows", "maximized", window_maximized);
	if (!window_maximized && screen_width != 0 && screen_height != 0)
	{
		cfgSaveInt("windows", "width", screen_width);
		cfgSaveInt("windows", "height", screen_height);
	}

	return runctl_exit_code();
}

int get_mic_data(u8* buffer) { return 0; }
int push_vmu_screen(u8* buffer) { return 0; }
