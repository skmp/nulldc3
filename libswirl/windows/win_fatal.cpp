/*
	This file is part of libswirl
*/
#include "license/bsd"

/*
	The no-dialog policy of every run on Windows, and the fatal ends (docs/nulldc3/automated-runs.md).

	A run of nulldc3.exe shows no dialog of any kind, windowed or headless: no box of the system's error mode (a drive
	without a disc, a fault), no Windows Error Reporting dialog, no just-in-time debugger, no box of the C runtime
	(abort, an invalid parameter, a pure call, its debug reports). win_install_fatal_policy() sets it up first thing
	in main, before anything can fail:
	- SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX)
	- WerSetFlags(WER_FAULT_REPORTING_NO_UI)
	- _set_abort_behavior: neither the message box of abort() nor its report to Windows Error Reporting; a SIGABRT
	  handler, which abort() and std::terminate reach
	- _set_invalid_parameter_handler and _set_purecall_handler: without them the C runtime ends an invalid parameter
	  with __fastfail, past every handler, straight to Windows Error Reporting
	- the C runtime's debug reports to stderr (_CrtSetReportMode; the debug runtime only)
	- a vectored fatal handler behind the guest memory fault handler of main (winmain.cpp), and the unhandled
	  exception filter
	- die() and verify() break into a debugger only when one is attached (win_common.cpp, os_DebugBreak)

	A fatal end prints one line on stderr, Linux's (linux/common.cpp), and ends the process with 128 + the signal's
	number, as a fatal signal ends a process on Linux:
	    fault_handler: unhandled <SIG> at address <a>, pc <p>, not a guest memory fault: terminating
	    SIGSEGV 139   an access violation, an in-page error, a misaligned access, a stack overflow
	    SIGILL  132   an illegal or a privileged instruction
	    SIGTRAP 133   a breakpoint instruction with no debugger attached
	    SIGABRT 134   abort(), std::terminate, an invalid parameter, a pure call (and an exception no handler took)
	    SIGFPE  136   an integer divide by zero, or an overflow of a divide
	die() and verify() print their own "Fatal error : ..." / "Verify Failed" line on stdout and end with 133 without
	a fault_handler line (os_DebugBreak).

	The vectored handler's scope: an access violation, an in-page error and a misaligned access are taken only when
	they happen in nulldc3.exe's image (the static C runtime included) or in the code region of the recompiler.
	Another module's (a graphics driver's) first fault goes on to that module's own handlers, which may take it; one
	that nobody takes reaches the unhandled exception filter, which prints the same line and ends the same way. The
	illegal and privileged instruction, the integer divide, the stack overflow and the breakpoint are taken wherever
	they happen. C++ exceptions, DBG_PRINTEXCEPTION_C, the naming of a thread and everything else pass.

	The line is put together without the C runtime and written with WriteFile: what faulted may be the runtime, or
	hold its locks.
*/

#include "win_common.h"
#include "win_fatal.h"

#include <crtdbg.h>
#include <intrin.h>
#include <signal.h>
#include <stdlib.h>
#include <werapi.h>

#include <string>

#pragma comment(lib, "wer.lib")

#define SIG_ILL  4
#define SIG_TRAP 5
#define SIG_ABRT 6
#define SIG_FPE  8
#define SIG_SEGV 11

// ---- the line ---------------------------------------------------------------------------------------------------

static char* put_text(char* p, char* end, const char* text)
{
	while (*text && p < end)
		*p++ = *text++;
	return p;
}

// as glibc's %p: 0x and the digits without leading zeros
static char* put_pointer(char* p, char* end, const void* pointer)
{
	unsigned long long v = (unsigned long long)(uintptr_t)pointer;
	char digits[16];
	int n = 0;
	do
	{
		digits[n++] = "0123456789abcdef"[v & 15];
		v >>= 4;
	} while (v);
	p = put_text(p, end, "0x");
	while (n && p < end)
		*p++ = digits[--n];
	return p;
}

void win_fatal(const char* sig, int signal_number, const void* address, const void* pc)
{
	char line[256];
	char* end = line + sizeof(line) - 1;
	char* p = put_text(line, end, "fault_handler: unhandled ");
	p = put_text(p, end, sig);
	p = put_text(p, end, " at address ");
	p = put_pointer(p, end, address);
	p = put_text(p, end, ", pc ");
	p = put_pointer(p, end, pc);
	p = put_text(p, end, ", not a guest memory fault: terminating\n");

	DWORD written;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, (DWORD)(p - line), &written, NULL);
	TerminateProcess(GetCurrentProcess(), 128 + signal_number);
	for (;;)
		Sleep(1000);	// TerminateProcess of this process ends this thread too
}

// ---- where a fault is -------------------------------------------------------------------------------------------

static const u8* image_start;
static const u8* image_end;

static bool in_image(const void* address)
{
	return (const u8*)address >= image_start && (const u8*)address < image_end;
}

static const void* fault_address(const EXCEPTION_RECORD* r)
{
	// the data address of an access violation and of an in-page error
	if ((r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
	    r->NumberParameters >= 2)
		return (const void*)r->ExceptionInformation[1];
	return r->ExceptionAddress;
}

static const void* fault_pc(const EXCEPTION_POINTERS* ep)
{
#if HOST_CPU == CPU_X64
	return (const void*)ep->ContextRecord->Rip;
#else
	#error missing arch for windows
#endif
}

// ---- the handlers -----------------------------------------------------------------------------------------------

static LONG CALLBACK fatal_vectored(EXCEPTION_POINTERS* ep)
{
	const EXCEPTION_RECORD* r = ep->ExceptionRecord;
	switch (r->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		// the guest memory fault handler (winmain.cpp) has had it first; another module's fault goes on to its
		// own handlers
		if (!in_image(r->ExceptionAddress) && !win_in_code_region(r->ExceptionAddress))
			return EXCEPTION_CONTINUE_SEARCH;
		win_fatal("SIGSEGV", SIG_SEGV, fault_address(r), fault_pc(ep));

	case EXCEPTION_STACK_OVERFLOW:
		win_fatal("SIGSEGV", SIG_SEGV, r->ExceptionAddress, fault_pc(ep));

	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
		win_fatal("SIGILL", SIG_ILL, r->ExceptionAddress, fault_pc(ep));

	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
		win_fatal("SIGFPE", SIG_FPE, r->ExceptionAddress, fault_pc(ep));

	case EXCEPTION_BREAKPOINT:
		if (IsDebuggerPresent())
			return EXCEPTION_CONTINUE_SEARCH;
		win_fatal("SIGTRAP", SIG_TRAP, r->ExceptionAddress, fault_pc(ep));

	default:
		// C++ exceptions, DBG_PRINTEXCEPTION_C, the naming of a thread, ...
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

static LONG WINAPI fatal_unhandled(EXCEPTION_POINTERS* ep)
{
	const EXCEPTION_RECORD* r = ep->ExceptionRecord;
	switch (r->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_STACK_OVERFLOW:
		win_fatal("SIGSEGV", SIG_SEGV, fault_address(r), fault_pc(ep));
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
		win_fatal("SIGILL", SIG_ILL, r->ExceptionAddress, fault_pc(ep));
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
		win_fatal("SIGFPE", SIG_FPE, r->ExceptionAddress, fault_pc(ep));
	case EXCEPTION_BREAKPOINT:
		win_fatal("SIGTRAP", SIG_TRAP, r->ExceptionAddress, fault_pc(ep));
	default:
		// a C++ exception nobody caught (std::terminate's case), or any other exception nobody took
		win_fatal("SIGABRT", SIG_ABRT, r->ExceptionAddress, fault_pc(ep));
	}
}

static void on_sigabrt(int)
{
	win_fatal("SIGABRT", SIG_ABRT, nullptr, _ReturnAddress());
}

static void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
	win_fatal("SIGABRT", SIG_ABRT, nullptr, _ReturnAddress());
}

static void on_purecall()
{
	win_fatal("SIGABRT", SIG_ABRT, nullptr, _ReturnAddress());
}

void win_install_fatal_policy()
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	WerSetFlags(WER_FAULT_REPORTING_NO_UI);

	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	signal(SIGABRT, on_sigabrt);
	_set_invalid_parameter_handler(on_invalid_parameter);
	_set_purecall_handler(on_purecall);

	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

	// nulldc3.exe's image, from its headers
	HMODULE self = GetModuleHandleW(NULL);
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)self;
	const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const u8*)self + dos->e_lfanew);
	image_start = (const u8*)self;
	image_end = image_start + nt->OptionalHeader.SizeOfImage;

	// room for the fatal handler on the main thread when its stack has run out
	ULONG guarantee = 64 * 1024;
	SetThreadStackGuarantee(&guarantee);

	AddVectoredExceptionHandler(0, fatal_vectored);
	SetUnhandledExceptionFilter(fatal_unhandled);
}

// ---- the test hooks ---------------------------------------------------------------------------------------------

struct PureBase
{
	PureBase();
	virtual void pure() = 0;
	virtual ~PureBase() {}
};

// called while PureBase is being made: its table has the pure function, which the C runtime's _purecall stands in for
__declspec(noinline) static void call_pure(PureBase* object)
{
	object->pure();
}

PureBase::PureBase()
{
	call_pure(this);
}

struct PureDerived : PureBase
{
	void pure() override {}
};

#pragma warning(push)
#pragma warning(disable: 4717)	// recursive on all control paths: that is the point
__declspec(noinline) static int overflow_stack(volatile char* previous)
{
	volatile char frame[4096];
	frame[0] = previous ? previous[0] : 1;
	return overflow_stack(frame) + frame[1];
}
#pragma warning(pop)

static volatile uintptr_t bad_address = 16;
static volatile int zero = 0;
static FILE* volatile no_file = nullptr;

// NULLDC_TEST_FATAL=<kind>: the fault of that kind, right after nulldc_init (winmain.cpp). Every kind ends the run
// through the policy above; a kind that did not end it says so on stderr and the run goes on
void win_test_fatal()
{
	const char* kind = getenv("NULLDC_TEST_FATAL");
	if (!kind || !*kind)
		return;
	std::string k = kind;
	fprintf(stderr, "NULLDC_TEST_FATAL: %s\n", kind);
	if (k == "segv")
		*(volatile int*)bad_address = 1;
	else if (k == "ill")
		__ud2();
	else if (k == "trap")
	{
		die("NULLDC_TEST_FATAL");
	}
	else if (k == "abort")
		abort();
	else if (k == "div")
	{
		volatile int result = 1 / zero;
		(void)result;
	}
	else if (k == "stack")
		overflow_stack(nullptr);
	else if (k == "invparam")
		fclose(no_file);
	else if (k == "purecall")
	{
		PureDerived object;
	}
	else
	{
		fprintf(stderr, "NULLDC_TEST_FATAL: unknown kind %s\n", kind);
		return;
	}
	fprintf(stderr, "NULLDC_TEST_FATAL: %s did not end the run\n", kind);
}

// NULLDC_TEST_UI_REPORT=1: one line at the end of main, what the run left of windows and of the policy, read back
// without raising anything
struct ReportWindows
{
	DWORD pid;
	const char* emulator_class;
	int counted;
	std::string classes;
};

static void report_window(HWND window, ReportWindows* r)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	if (pid != r->pid)
		return;
	char cls[256] = "";
	GetClassNameA(window, cls, sizeof(cls));
	if (!r->classes.empty())
		r->classes += ",";
	r->classes += cls;
	if (IsWindowVisible(window) || !strcmp(cls, r->emulator_class))
		r->counted++;
}

static BOOL CALLBACK report_top_level(HWND window, LPARAM param)
{
	report_window(window, (ReportWindows*)param);
	return TRUE;
}

void win_test_ui_report(bool window_class_registered, const char* window_class)
{
	const char* on = getenv("NULLDC_TEST_UI_REPORT");
	if (!on || strcmp(on, "1"))
		return;

	// the top-level windows of this process on its desktop, and its message-only windows (COM's apartment window
	// is one)
	ReportWindows r = { GetCurrentProcessId(), window_class, 0, "" };
	EnumWindows(report_top_level, (LPARAM)&r);
	for (HWND w = FindWindowExA(HWND_MESSAGE, NULL, NULL, NULL); w; w = FindWindowExA(HWND_MESSAGE, w, NULL, NULL))
		report_window(w, &r);

	DWORD wer = 0;
	if (FAILED(WerGetFlags(GetCurrentProcess(), &wer)))
		wer = 0;
	int invparam = _get_invalid_parameter_handler() == on_invalid_parameter;
	int purecall = _get_purecall_handler() == on_purecall;
	int sigabrt = signal(SIGABRT, SIG_GET) == on_sigabrt;
	unsigned int abort_flags = _set_abort_behavior(0, 0);	// reads the flags, changes nothing

	printf("nulldc-ui: app=%d windows=%d errmode=%x wer=%lx invparam=%d purecall=%d sigabrt=%d abort=%x classes=%s\n",
	       window_class_registered ? 1 : 0, r.counted, GetErrorMode(), (unsigned long)wer, invparam, purecall, sigabrt,
	       abort_flags, r.classes.c_str());
	fflush(stdout);
}
