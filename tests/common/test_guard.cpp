#include "test_guard.h"

// Every suite links this: an MSVC build of the tests project without /EHsc stops here (nulldc3/cmake/config.cmake),
// also where a suite keeps CMake's own flags
#if defined(_MSC_VER) && !defined(_CPPUNWIND)
#error "MSVC build without /EHsc: JSON_THROW would be std::abort"
#endif

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>

#if !defined(_WIN32)
#define TEST_GUARD_POSIX 1
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#else
#define TEST_GUARD_POSIX 0
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <crtdbg.h>
#include <intrin.h>
#include <signal.h>
#include <werapi.h>
#pragma comment(lib, "wer.lib")
#endif

#if TEST_GUARD_POSIX
static sigjmp_buf guard_jb;
#define guard_setjmp() sigsetjmp(guard_jb, 1)
#define guard_longjmp(v) siglongjmp(guard_jb, v)
#else
static jmp_buf guard_jb;
#define guard_setjmp() setjmp(guard_jb)
#define guard_longjmp(v) longjmp(guard_jb, v)
#endif

static volatile int guard_active;
static volatile u32 guard_serial;

static char guard_message[1024];
static char last_msgbox[1024];
static u32 msgbox_count;
static char msgbox_text[TEST_GUARD_MSGBOX_TEXTS + 1][sizeof(last_msgbox)];
static u32 msgbox_texts;
static bool echo_msgbox;

static u32 watchdog_ms;
static u32 watchdog_seen;

//one line, no trailing blanks
static void flatten(char* text)
{
	size_t len = strlen(text);

	for (size_t i = 0; i < len; i++)
	{
		if (text[i] == '\n' || text[i] == '\r' || text[i] == '\t')
			text[i] = ' ';
	}

	while (len > 0 && text[len - 1] == ' ')
		text[--len] = 0;
}

static void fatal_outside_guard(const char* what, const char* text)
{
	fflush(stdout);
	fprintf(stderr, "\n%s outside of a guarded call: %s\n", what, text);
	fflush(stderr);
	_Exit(3);
}

int msgboxf(const wchar* text, unsigned int type, ...)
{
	va_list args;

	va_start(args, type);
	vsnprintf(last_msgbox, sizeof(last_msgbox), text, args);
	va_end(args);

	flatten(last_msgbox);
	msgbox_count++;

	u32 seen = 0;

	while (seen < msgbox_texts && strcmp(msgbox_text[seen], last_msgbox) != 0)
		seen++;

	if (seen == msgbox_texts && msgbox_texts <= TEST_GUARD_MSGBOX_TEXTS)
	{
		//the first text there is no room for stands for all that follow
		strcpy(msgbox_text[msgbox_texts], msgbox_texts < TEST_GUARD_MSGBOX_TEXTS ? last_msgbox : "(more texts than are kept)");
		msgbox_texts++;
	}

	if (echo_msgbox)
		printf("msgboxf: %s\n", last_msgbox);

	return 1;
}

void os_DebugBreak()
{
	if (!guard_active)
		fatal_outside_guard("die()/verify()", last_msgbox);

	strcpy(guard_message, last_msgbox);
	guard_longjmp(TGR_DIE);
}

void test_guard_abort(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(guard_message, sizeof(guard_message), fmt, args);
	va_end(args);

	if (!guard_active)
		fatal_outside_guard("test_guard_abort()", guard_message);

	guard_longjmp(TGR_ABORT);
}

#if TEST_GUARD_POSIX
static void on_fatal_signal(int sig)
{
	if (!guard_active)
	{
		static const char text[] = "\nfatal signal outside of a guarded call\n";
		ssize_t rv = write(2, text, sizeof(text) - 1);
		(void)rv;
		_exit(128 + sig);
	}

	snprintf(guard_message, sizeof(guard_message), "signal %d (%s)", sig,
		sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE" : "SIGTRAP");
	guard_longjmp(TGR_SIGNAL);
}

//Ticks twice per watchdog period: a call that is seen by two ticks has been running for at least half of it
static void on_watchdog(int)
{
	if (!guard_active)
		return;

	if (watchdog_seen != guard_serial)
	{
		watchdog_seen = guard_serial;
		return;
	}

	snprintf(guard_message, sizeof(guard_message), "no result after %u ms", watchdog_ms / 2);
	guard_longjmp(TGR_TIMEOUT);
}
#endif

#if !TEST_GUARD_POSIX
/*
	Windows: the policy of the emulator (libswirl/windows/win_fatal.cpp, docs/nulldc3/automated-runs.md) for every
	program that links this file, from before main: a test program that dies shows no box and leaves no crash
	report (Windows Error Reporting keeps one, outside the build directory, for every process an exception nobody
	handled ended, also under nulldc3-winrun). Its error mode, no report UI, abort() without its box and its report
	and with a SIGABRT handler, the C runtime's invalid-parameter and pure-call handlers (without them the runtime
	ends with __fastfail, past every handler, straight to Windows Error Reporting), the debug runtime's reports to
	stderr, and an unhandled-exception filter: each prints the line of Linux's fault handler on stderr and ends the
	process with 128 + the signal's number, as a fatal signal ends one on Linux (SIGSEGV 139, SIGILL 132, SIGTRAP
	133, SIGABRT 134, SIGFPE 136). No exception reaches Windows Error Reporting through it.
*/
static void policy_fatal(const char* sig, int number, const void* address)
{
	char line[160];
	int n = snprintf(line, sizeof(line), "\ntest_guard: unhandled %s at address %p, outside of a guarded call: terminating\n", sig, address);
	DWORD written;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, n > 0 ? (DWORD)n : 0, &written, NULL);
	TerminateProcess(GetCurrentProcess(), 128 + number);
	for (;;)
		Sleep(1000);
}

static LONG WINAPI policy_unhandled(EXCEPTION_POINTERS* ep)
{
	const EXCEPTION_RECORD* r = ep->ExceptionRecord;
	switch (r->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_STACK_OVERFLOW:
		policy_fatal("SIGSEGV", 11, r->ExceptionAddress);
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
		policy_fatal("SIGILL", 4, r->ExceptionAddress);
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
		policy_fatal("SIGFPE", 8, r->ExceptionAddress);
	case EXCEPTION_BREAKPOINT:
		policy_fatal("SIGTRAP", 5, r->ExceptionAddress);
	default:
		// a C++ exception nobody caught, or any other exception nobody took
		policy_fatal("SIGABRT", 6, r->ExceptionAddress);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static void policy_sigabrt(int)
{
	policy_fatal("SIGABRT", 6, _ReturnAddress());
}

static void policy_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
	policy_fatal("SIGABRT", 6, _ReturnAddress());
}

static void policy_purecall()
{
	policy_fatal("SIGABRT", 6, _ReturnAddress());
}

static struct WindowsPolicy
{
	WindowsPolicy()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
		WerSetFlags(WER_FAULT_REPORTING_NO_UI);

		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		signal(SIGABRT, policy_sigabrt);
		_set_invalid_parameter_handler(policy_invalid_parameter);
		_set_purecall_handler(policy_purecall);

		_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

		// room for the filter on a thread whose stack has run out
		ULONG guarantee = 64 * 1024;
		SetThreadStackGuarantee(&guarantee);

		SetUnhandledExceptionFilter(policy_unhandled);
	}
} windows_policy;

// What the crash-policy test of the tests project (common.guard.windows) raises: NULLDC_TEST_GUARD_FAULT=<kind>,
// at the start of test_guard_init, outside of any guarded call
static void guard_test_fault()
{
	char kind[32];
	size_t n = 0;
	if (getenv_s(&n, kind, sizeof(kind), "NULLDC_TEST_GUARD_FAULT") != 0 || n == 0)
		return;
	fprintf(stderr, "NULLDC_TEST_GUARD_FAULT: %s\n", kind);
	fflush(stderr);
	static volatile uintptr_t bad_address = 16;
	static volatile int zero = 0;
	if (!strcmp(kind, "segv"))
		*(volatile int*)bad_address = 1;
	else if (!strcmp(kind, "abort"))
		abort();
	else if (!strcmp(kind, "div"))
	{
		volatile int result = 1 / zero;
		(void)result;
	}
	else if (!strcmp(kind, "throw"))
		throw 1;
	fprintf(stderr, "NULLDC_TEST_GUARD_FAULT: %s did not end the program\n", kind);
}
#endif

void test_guard_init(u32 watchdog)
{
	watchdog_ms = watchdog;

#if !TEST_GUARD_POSIX
	guard_test_fault();
#endif

#if TEST_GUARD_POSIX
	static const int fatal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP };

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);

	sa.sa_handler = on_fatal_signal;
	for (size_t i = 0; i < sizeof(fatal) / sizeof(fatal[0]); i++)
		sigaction(fatal[i], &sa, 0);

	if (watchdog_ms != 0)
	{
		sa.sa_handler = on_watchdog;
		sa.sa_flags = SA_RESTART;
		sigaction(SIGALRM, &sa, 0);

		struct itimerval tick;
		tick.it_interval.tv_sec = (watchdog_ms / 2) / 1000;
		tick.it_interval.tv_usec = ((watchdog_ms / 2) % 1000) * 1000;
		tick.it_value = tick.it_interval;
		setitimer(ITIMER_REAL, &tick, 0);
	}
#endif
}

TestGuardResult test_guard_run(TestGuardFP* fn, void* arg)
{
	guard_message[0] = 0;
	last_msgbox[0] = 0;
	msgbox_count = 0;
	msgbox_texts = 0;
	guard_serial = guard_serial + 1;

	int rv = guard_setjmp();

	if (rv == 0)
	{
#if defined(_MSC_VER) && defined(_M_X64)
		// MSVC's longjmp unwinds the stack it leaves (RtlUnwindEx) when the buffer names a frame, and the code of
		// a recompiler on that stack has no unwind data: the unwind ends the process (STATUS_BAD_FUNCTION_TABLE).
		// Without the frame it restores the registers alone, as siglongjmp does
		((_JUMP_BUFFER*)&guard_jb)->Frame = 0;
#endif
		guard_active = 1;
		fn(arg);
	}

	guard_active = 0;

	return (TestGuardResult)rv;
}

const char* test_guard_message()
{
	return guard_message;
}

u32 test_guard_msgbox_count()
{
	return msgbox_count;
}

const char* test_guard_last_msgbox()
{
	return last_msgbox;
}

u32 test_guard_msgbox_texts()
{
	return msgbox_texts;
}

const char* test_guard_msgbox_text(u32 index)
{
	return index < msgbox_texts ? msgbox_text[index] : "";
}

void test_guard_echo_msgbox(bool echo)
{
	echo_msgbox = echo;
}

const char* test_guard_result_name(TestGuardResult result)
{
	switch (result)
	{
	case TGR_OK:      return "ok";
	case TGR_DIE:     return "die";
	case TGR_ABORT:   return "abort";
	case TGR_SIGNAL:  return "signal";
	case TGR_TIMEOUT: return "timeout";
	}

	return "?";
}
