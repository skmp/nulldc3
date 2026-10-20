/*
	nulldc3-winrun: runs a program on the Windows host so that nothing it does can reach the desktop of the person
	who uses the computer (the platform round's run envelope; tests/winrun/README.md).

	    nulldc3-winrun [--timeout <s>] [--own-console] [--detach --log <file> --rc <file>] -- <program> [<arg>...]
	    nulldc3-winrun --probe

	- Its error mode is SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, set first; every
	  process it starts inherits it, and so does every process those start.
	- The program runs in a job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE and
	  JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION: nothing of it outlives nulldc3-winrun, and a process of it that an
	  exception ends does so without the error box. Jobs nest: a program that runs nulldc3-winrun again is fine.
	- The program runs on a desktop of its own, winrun-<pid of nulldc3-winrun>, made in the window station of the
	  caller (GetProcessWindowStation): a window or a message box the program makes is on that desktop, which nobody
	  sees. It is started suspended, assigned to the job and only then resumed: if the desktop cannot be made or the
	  program cannot be put into the job, it never runs, and nulldc3-winrun exits with 126.
	- The program shares the console of nulldc3-winrun, or is started with CREATE_NO_WINDOW when there is none (a
	  detached run): never with a console window of its own. --own-console gives it a pseudo console instead
	  (CreatePseudoConsole), a console that has the program alone, whose output nulldc3-winrun copies to its own
	  standard output.
	- While the program runs, nulldc3-winrun lists the top-level windows of its desktop every 100 ms, and once more
	  when the program has ended (EnumDesktopWindows): the class, whether the window is visible, and the process it
	  belongs to (its id and the name of its executable). It prints them on stderr at the end.
	- Exit status: 126 when the set-up failed (the program did not run); 125 when a visible window, or a window of a
	  class the emulator registers (WINDOW_CLASS of libswirl/windows/win_common.h), was seen on the desktop,
	  whatever else happened; 124 when --timeout ran out (the job and everything in it is ended); otherwise the
	  program's own.
	- --detach --log <file> --rc <file>: starts a copy of itself with DETACHED_PROCESS that does all of the above
	  with <file> (--log) as the program's standard output and error, prints that copy's process id and returns at
	  once with 0; the copy writes the exit status into the --rc file when it is done (written as <file>.tmp, then
	  renamed). For a long step: its log and its rc file are polled.
	- --probe: what nulldc3-winrun runs first on a host: it prints the name of the desktop its thread is on. When
	  that is not winrun-<number> it exits with 3 without making any window (a broken nulldc3-winrun must not put
	  the probe's window on the desktop of the person who uses the computer); otherwise it makes a visible window
	  and waits, so that `nulldc3-winrun --timeout 10 -- nulldc3-winrun --probe` has to end with 125, the name of
	  the private desktop on its output and the probe's window in its list.

	What the private desktop does not hide: crash reporting (WerFault), a just-in-time debugger, the system's error
	boxes and the firewall's prompt are other processes and show on the interactive desktop whatever desktop the
	program is on. The error mode and the job's flag keep them away, with the emulator's own policy; nothing may
	turn them off.

	Built by the tests project (tests/windows) as nulldc3-winrun.exe, and before there is one by bootstrap.cmd of
	this directory with cl.exe alone. Windows 10 1809 or later (CreatePseudoConsole).
*/
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <set>
#include <string>
#include <tuple>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

using std::wstring;

static const DWORD kErrorMode = SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;

// The window classes the emulator registers (libswirl/windows/win_common.h WINDOW_CLASS): one of them seen at all,
// visible or not, is a window the emulator made
static const wchar_t* const kEmulatorClasses[] = { L"nilDC" };

enum
{
	kExitProbeRefused = 3,
	kExitTimeout = 124,
	kExitWindow = 125,
	kExitSetup = 126,
};

static void say(const wchar_t* format, ...)
{
	wchar_t text[2048];
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(text, _countof(text), _TRUNCATE, format, args);
	va_end(args);

	// UTF-8 to whatever stderr is (a pipe, a file, a console): written whole, with WriteFile, so that lines of
	// nested runs do not tear inside a line
	char bytes[4096];
	int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, bytes, (int)sizeof(bytes) - 2, nullptr, nullptr);
	if (n <= 0)
		return;
	n--;	// the terminating zero
	bytes[n++] = '\n';
	DWORD written;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), bytes, (DWORD)n, &written, nullptr);
}

static wstring last_error_text(DWORD code)
{
	wchar_t* text = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	               nullptr, code, 0, (wchar_t*)&text, 0, nullptr);
	wstring out = text ? text : L"";
	if (text)
		LocalFree(text);
	while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r' || out.back() == L' ' || out.back() == L'.'))
		out.pop_back();
	return out + L" (" + std::to_wstring(code) + L")";
}

// A command line argument quoted for CreateProcess, as CommandLineToArgvW and the C runtime read it back
static wstring quote(const wstring& arg)
{
	if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == wstring::npos)
		return arg;
	wstring out = L"\"";
	for (size_t i = 0;; i++)
	{
		size_t backslashes = 0;
		while (i < arg.size() && arg[i] == L'\\')
		{
			i++;
			backslashes++;
		}
		if (i == arg.size())
		{
			out.append(backslashes * 2, L'\\');
			break;
		}
		if (arg[i] == L'"')
		{
			out.append(backslashes * 2 + 1, L'\\');
			out.push_back(L'"');
		}
		else
		{
			out.append(backslashes, L'\\');
			out.push_back(arg[i]);
		}
	}
	out.push_back(L'"');
	return out;
}

static wstring object_name(HANDLE object)
{
	DWORD needed = 0;
	GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &needed);
	if (needed == 0)
		return L"";
	std::vector<wchar_t> name(needed / sizeof(wchar_t) + 1, 0);
	if (!GetUserObjectInformationW(object, UOI_NAME, name.data(), (DWORD)(name.size() * sizeof(wchar_t)), &needed))
		return L"";
	return name.data();
}

// ---- the probe --------------------------------------------------------------------------------------------------

static LRESULT CALLBACK probe_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_DESTROY)
	{
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

static bool is_private_desktop(const wstring& name)
{
	// winrun-<digits>, nothing else
	const wstring prefix = L"winrun-";
	if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
		return false;
	for (size_t i = prefix.size(); i < name.size(); i++)
		if (name[i] < L'0' || name[i] > L'9')
			return false;
	return true;
}

static int probe()
{
	HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
	wstring name = desktop ? object_name(desktop) : L"";
	printf("winrun probe: desktop %ls\n", name.empty() ? L"(unknown)" : name.c_str());
	fflush(stdout);

	if (!is_private_desktop(name))
	{
		// the interlock: never a window on any other desktop
		printf("winrun probe: that is no desktop of nulldc3-winrun: no window made\n");
		fflush(stdout);
		return kExitProbeRefused;
	}

	WNDCLASSW cls = {};
	cls.lpfnWndProc = probe_proc;
	cls.hInstance = GetModuleHandleW(nullptr);
	cls.lpszClassName = L"nulldc3-winrun-probe";
	if (!RegisterClassW(&cls))
	{
		printf("winrun probe: RegisterClass failed\n");
		return 1;
	}
	HWND window = CreateWindowExW(0, cls.lpszClassName, L"nulldc3-winrun probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
	                              0, 0, 320, 200, nullptr, nullptr, cls.hInstance, nullptr);
	if (!window)
	{
		printf("winrun probe: CreateWindow failed\n");
		return 1;
	}
	printf("winrun probe: a visible window of the class nulldc3-winrun-probe is on %ls, waiting\n", name.c_str());
	fflush(stdout);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return 0;
}

// ---- the windows of the desktop ---------------------------------------------------------------------------------

struct SeenWindow
{
	wstring cls;
	bool visible;
	DWORD pid;
	wstring image;

	bool operator<(const SeenWindow& o) const
	{
		return std::tie(cls, visible, pid, image) < std::tie(o.cls, o.visible, o.pid, o.image);
	}
};

static std::set<SeenWindow> seen;

static wstring image_of(DWORD pid)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process)
		return L"?";
	wchar_t path[MAX_PATH * 2];
	DWORD size = _countof(path);
	wstring out = L"?";
	if (QueryFullProcessImageNameW(process, 0, path, &size))
	{
		out = path;
		size_t slash = out.find_last_of(L"\\/");
		if (slash != wstring::npos)
			out = out.substr(slash + 1);	// the name alone: no path of the host in a log
	}
	CloseHandle(process);
	return out;
}

static BOOL CALLBACK enum_window(HWND window, LPARAM)
{
	wchar_t cls[256] = L"";
	GetClassNameW(window, cls, _countof(cls));
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	SeenWindow w = { cls, IsWindowVisible(window) != FALSE, pid, image_of(pid) };
	seen.insert(w);
	return TRUE;
}

static void sample(HDESK desktop)
{
	EnumDesktopWindows(desktop, enum_window, 0);
}

static bool is_emulator_class(const wstring& cls)
{
	for (const wchar_t* c : kEmulatorClasses)
		if (cls == c)
			return true;
	return false;
}

// ---- running the program ----------------------------------------------------------------------------------------

struct Options
{
	double timeout = 0;	// seconds, 0: none
	bool own_console = false;
	bool detach = false;
	bool worker = false;	// the detached copy
	wstring log, rc;
	std::vector<wstring> command;
};

static void usage()
{
	say(L"usage: nulldc3-winrun [--timeout <s>] [--own-console] [--detach --log <file> --rc <file>] -- <program> [<arg>...]");
	say(L"       nulldc3-winrun --probe");
}

// The copier of --own-console: what the pseudo console writes, to the standard output of nulldc3-winrun
struct Copier
{
	HANDLE from;
	HANDLE to;
};

static DWORD WINAPI copy_output(void* param)
{
	Copier* c = (Copier*)param;
	char buffer[4096];
	DWORD n;
	while (ReadFile(c->from, buffer, sizeof(buffer), &n, nullptr) && n > 0)
	{
		DWORD written;
		if (c->to && c->to != INVALID_HANDLE_VALUE)
			WriteFile(c->to, buffer, n, &written, nullptr);
	}
	return 0;
}

typedef HRESULT(WINAPI* CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, void**);
typedef void(WINAPI* ClosePseudoConsoleFn)(void*);

static int run(const Options& opt)
{
	DWORD self = GetCurrentProcessId();

	// the desktop, in the window station of the caller
	HWINSTA station = GetProcessWindowStation();
	wstring station_name = station ? object_name(station) : L"";
	if (station_name.empty())
	{
		say(L"winrun: the window station of this process has no name: %ls", last_error_text(GetLastError()).c_str());
		return kExitSetup;
	}
	wstring desktop_name = L"winrun-" + std::to_wstring(self);
	HDESK desktop = CreateDesktopW(desktop_name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
	if (!desktop)
	{
		say(L"winrun: CreateDesktop %ls failed: %ls; the program is not started", desktop_name.c_str(),
		    last_error_text(GetLastError()).c_str());
		return kExitSetup;
	}
	wstring desktop_path = station_name + L"\\" + desktop_name;

	// the job
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (!job)
	{
		say(L"winrun: CreateJobObject failed: %ls; the program is not started", last_error_text(GetLastError()).c_str());
		CloseDesktop(desktop);
		return kExitSetup;
	}
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
	if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
	{
		say(L"winrun: the job's limits cannot be set: %ls; the program is not started", last_error_text(GetLastError()).c_str());
		CloseHandle(job);
		CloseDesktop(desktop);
		return kExitSetup;
	}

	wstring cmdline;
	for (size_t i = 0; i < opt.command.size(); i++)
		cmdline += (i ? L" " : L"") + quote(opt.command[i]);
	std::vector<wchar_t> cmdbuf(cmdline.begin(), cmdline.end());
	cmdbuf.push_back(0);

	STARTUPINFOEXW si = {};
	si.StartupInfo.cb = sizeof(si);
	si.StartupInfo.lpDesktop = (wchar_t*)desktop_path.c_str();
	DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
	BOOL inherit = FALSE;

	void* pseudo = nullptr;
	ClosePseudoConsoleFn close_pseudo = nullptr;
	HANDLE pc_in_read = nullptr, pc_in_write = nullptr, pc_out_read = nullptr, pc_out_write = nullptr;
	std::vector<char> attributes;
	HANDLE copier_thread = nullptr;
	Copier copier = {};

	if (opt.own_console)
	{
		HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
		CreatePseudoConsoleFn create_pseudo = (CreatePseudoConsoleFn)GetProcAddress(kernel, "CreatePseudoConsole");
		close_pseudo = (ClosePseudoConsoleFn)GetProcAddress(kernel, "ClosePseudoConsole");
		if (!create_pseudo || !close_pseudo)
		{
			say(L"winrun: this system has no CreatePseudoConsole (Windows 10 1809 or later); the program is not started");
			CloseHandle(job);
			CloseDesktop(desktop);
			return kExitSetup;
		}
		if (!CreatePipe(&pc_in_read, &pc_in_write, nullptr, 0) || !CreatePipe(&pc_out_read, &pc_out_write, nullptr, 0))
		{
			say(L"winrun: CreatePipe failed: %ls; the program is not started", last_error_text(GetLastError()).c_str());
			CloseHandle(job);
			CloseDesktop(desktop);
			return kExitSetup;
		}
		COORD size = { 120, 40 };
		HRESULT hr = create_pseudo(size, pc_in_read, pc_out_write, 0, &pseudo);
		if (FAILED(hr))
		{
			say(L"winrun: CreatePseudoConsole failed: 0x%08lx; the program is not started", (unsigned long)hr);
			CloseHandle(job);
			CloseDesktop(desktop);
			return kExitSetup;
		}
		// the pseudo console holds its own ends now
		CloseHandle(pc_in_read);
		CloseHandle(pc_out_write);
		pc_in_read = pc_out_write = nullptr;

		SIZE_T bytes = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
		attributes.resize(bytes);
		si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attributes.data();
		if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes) ||
		    !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudo, sizeof(pseudo),
		                               nullptr, nullptr))
		{
			say(L"winrun: the pseudo console cannot be given to the program: %ls; the program is not started",
			    last_error_text(GetLastError()).c_str());
			close_pseudo(pseudo);
			CloseHandle(job);
			CloseDesktop(desktop);
			return kExitSetup;
		}
		// The program's standard handles are those of its pseudo console: none of this process's (a redirected
		// standard handle of nulldc3-winrun would otherwise be handed down in their place)
		si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
		si.StartupInfo.hStdInput = nullptr;
		si.StartupInfo.hStdOutput = nullptr;
		si.StartupInfo.hStdError = nullptr;
		flags |= EXTENDED_STARTUPINFO_PRESENT;

		copier.from = pc_out_read;
		copier.to = GetStdHandle(STD_OUTPUT_HANDLE);
		copier_thread = CreateThread(nullptr, 0, copy_output, &copier, 0, nullptr);
	}
	else
	{
		// The program shares this process's console and standard handles; with no console it gets none, never a
		// console window of its own
		if (!GetConsoleWindow())
			flags |= CREATE_NO_WINDOW;
		HANDLE handles[3] = { GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE) };
		for (HANDLE h : handles)
			if (h && h != INVALID_HANDLE_VALUE)
				SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
		si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
		si.StartupInfo.hStdInput = handles[0];
		si.StartupInfo.hStdOutput = handles[1];
		si.StartupInfo.hStdError = handles[2];
		inherit = TRUE;
	}

	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, inherit, flags, nullptr, nullptr, &si.StartupInfo, &pi))
	{
		say(L"winrun: cannot start %ls: %ls", opt.command[0].c_str(), last_error_text(GetLastError()).c_str());
		if (pseudo)
			close_pseudo(pseudo);
		CloseHandle(job);
		CloseDesktop(desktop);
		return kExitSetup;
	}
	if (!AssignProcessToJobObject(job, pi.hProcess))
	{
		say(L"winrun: the program cannot be put into the job: %ls; it is ended before it ran",
		    last_error_text(GetLastError()).c_str());
		TerminateProcess(pi.hProcess, kExitSetup);
		WaitForSingleObject(pi.hProcess, 5000);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		if (pseudo)
			close_pseudo(pseudo);
		CloseHandle(job);
		CloseDesktop(desktop);
		return kExitSetup;
	}
	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);

	// the program runs: its windows, every 100 ms, until it ends or the time runs out
	ULONGLONG start = GetTickCount64();
	bool timed_out = false;
	for (;;)
	{
		sample(desktop);
		DWORD w = WaitForSingleObject(pi.hProcess, 100);
		if (w == WAIT_OBJECT_0)
			break;
		if (w == WAIT_FAILED)
		{
			say(L"winrun: waiting for the program failed: %ls", last_error_text(GetLastError()).c_str());
			break;
		}
		if (opt.timeout > 0 && (double)(GetTickCount64() - start) >= opt.timeout * 1000.0)
		{
			timed_out = true;
			sample(desktop);	// what is there in the moment before the job is ended
			say(L"winrun: timeout: %.0f s are over: the job is ended", opt.timeout);
			TerminateJobObject(job, kExitTimeout);
			WaitForSingleObject(pi.hProcess, 10000);
			break;
		}
	}
	sample(desktop);

	DWORD code = 0;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);

	JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = {};
	if (QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) &&
	    accounting.ActiveProcesses > 0)
		say(L"winrun: %lu process(es) of the job still ran when the program ended: ended with the job",
		    (unsigned long)accounting.ActiveProcesses);
	CloseHandle(job);	// KILL_ON_JOB_CLOSE: whatever is left of it ends here

	if (pseudo)
	{
		// closing the pseudo console ends its output: the copier reads to the end and stops
		close_pseudo(pseudo);
		if (copier_thread)
		{
			WaitForSingleObject(copier_thread, 5000);
			CloseHandle(copier_thread);
		}
		CloseHandle(pc_out_read);
		CloseHandle(pc_in_write);
	}

	// the windows that were seen, and the verdict
	size_t visible = 0, emulator = 0;
	for (const SeenWindow& w : seen)
	{
		say(L"winrun: window class=%ls visible=%d pid=%lu image=%ls", w.cls.c_str(), w.visible ? 1 : 0,
		    (unsigned long)w.pid, w.image.c_str());
		if (w.visible)
			visible++;
		if (is_emulator_class(w.cls))
			emulator++;
	}
	CloseDesktop(desktop);

	int verdict;
	if (visible || emulator)
		verdict = kExitWindow;
	else if (timed_out)
		verdict = kExitTimeout;
	else
		verdict = (int)code;
	say(L"winrun: desktop %ls: %zu window(s) seen, %zu visible, %zu of the emulator's class; the program's exit status %lu%ls; winrun exits with %d",
	    desktop_path.c_str(), seen.size(), visible, emulator, (unsigned long)code, timed_out ? L" (timeout)" : L"", verdict);
	return verdict;
}

// --detach: a copy of this program with the same command, detached, its output to the log
static int detach(const Options& opt, int argc, wchar_t** argv)
{
	SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
	HANDLE log = CreateFileW(opt.log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
	                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (log == INVALID_HANDLE_VALUE)
	{
		say(L"winrun: cannot write the log %ls: %ls", opt.log.c_str(), last_error_text(GetLastError()).c_str());
		return kExitSetup;
	}
	DeleteFileW(opt.rc.c_str());	// a stale rc file would read as the result of this run
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

	wchar_t self[MAX_PATH * 2];
	GetModuleFileNameW(nullptr, self, _countof(self));
	wstring cmdline = quote(self) + L" --worker";
	// every argument but --detach goes to the copy
	for (int i = 1; i < argc; i++)
	{
		if (!wcscmp(argv[i], L"--"))
		{
			for (; i < argc; i++)
				cmdline += L" " + quote(argv[i]);
			break;
		}
		if (!wcscmp(argv[i], L"--detach"))
			continue;
		cmdline += L" " + quote(argv[i]);
	}
	std::vector<wchar_t> cmdbuf(cmdline.begin(), cmdline.end());
	cmdbuf.push_back(0);

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nul;
	si.hStdOutput = log;
	si.hStdError = log;
	PROCESS_INFORMATION pi = {};
	// out of a job the caller may be in, when that job allows it: the copy outlives this process
	DWORD flags = DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP;
	BOOL ok = CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, flags | CREATE_BREAKAWAY_FROM_JOB, nullptr,
	                         nullptr, &si, &pi);
	if (!ok)
		ok = CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi);
	CloseHandle(log);
	if (nul != INVALID_HANDLE_VALUE)
		CloseHandle(nul);
	if (!ok)
	{
		say(L"winrun: cannot start the detached copy: %ls", last_error_text(GetLastError()).c_str());
		return kExitSetup;
	}
	printf("%lu\n", (unsigned long)pi.dwProcessId);
	fflush(stdout);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}

static void write_rc(const wstring& path, int code)
{
	wstring tmp = path + L".tmp";
	HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return;
	char text[32];
	int n = sprintf_s(text, "%d\n", code);
	DWORD written;
	WriteFile(f, text, (DWORD)n, &written, nullptr);
	CloseHandle(f);
	MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

int wmain(int argc, wchar_t** argv)
{
	// first: nothing of this process or its descendants may raise the system's error boxes
	SetErrorMode(kErrorMode);

	Options opt;
	bool probe_mode = false;
	int i = 1;
	for (; i < argc; i++)
	{
		wstring a = argv[i];
		if (a == L"--")
		{
			i++;
			break;
		}
		else if (a == L"--probe")
			probe_mode = true;
		else if (a == L"--own-console")
			opt.own_console = true;
		else if (a == L"--detach")
			opt.detach = true;
		else if (a == L"--worker")
			opt.worker = true;
		else if ((a == L"--timeout" || a == L"--log" || a == L"--rc") && i + 1 < argc)
		{
			wstring v = argv[++i];
			if (a == L"--timeout")
			{
				wchar_t* end = nullptr;
				opt.timeout = wcstod(v.c_str(), &end);
				if (!end || *end || opt.timeout <= 0)
				{
					say(L"winrun: --timeout takes a number of seconds above 0, not \"%ls\"", v.c_str());
					return kExitSetup;
				}
			}
			else if (a == L"--log")
				opt.log = v;
			else
				opt.rc = v;
		}
		else
		{
			say(L"winrun: unknown switch \"%ls\"", a.c_str());
			usage();
			return kExitSetup;
		}
	}
	for (; i < argc; i++)
		opt.command.push_back(argv[i]);

	if (probe_mode)
	{
		if (!opt.command.empty() || opt.detach || opt.own_console)
		{
			say(L"winrun: --probe takes nothing else");
			return kExitSetup;
		}
		return probe();
	}
	if (opt.command.empty())
	{
		usage();
		return kExitSetup;
	}
	if ((opt.detach || opt.worker) && (opt.log.empty() || opt.rc.empty()))
	{
		say(L"winrun: --detach needs --log <file> and --rc <file>");
		return kExitSetup;
	}
	if (opt.detach && opt.worker)
	{
		say(L"winrun: --detach and --worker together");
		return kExitSetup;
	}
	if (opt.detach)
		return detach(opt, argc, argv);

	int code = run(opt);
	if (opt.worker)
		write_rc(opt.rc, code);
	return code;
}
