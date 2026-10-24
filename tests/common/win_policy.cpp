/*
	Windows: the policy of the tests project for a program that dies, the emulator's (libswirl/windows/win_fatal.cpp,
	docs/nulldc3/automated-runs.md) for every program of the project that the tests run there, from before main: a
	program that dies shows no box and leaves no crash report (Windows Error Reporting keeps one, outside the build
	directory, for every process an exception nobody handled ended, also under nulldc3-winrun, whose error mode and
	job keep the box away and not the report).

	Which programs have it: every program of the tests project but nulldc3-winrun, the runner the others run inside
	(and the control of common.policy.windows): every suite that links test_guard.cpp of nulldc_test_common, which
	pulls this object in, and every other program, which links this file in its own directory (CMake's target_sources
	on Windows). common.policy.windows reads the mark below in every program the project builds
	(policy_windows.cmake); common.guard.windows makes four of them fault (guard_windows.cmake): common_selftest,
	disasm_test, sh4bp_test and aica_dsp_guest_oracle.

	What it sets: the error mode, no report UI, abort() without its box and its report and with a SIGABRT handler,
	the C runtime's invalid-parameter and pure-call handlers (without them the runtime ends with __fastfail, past
	every handler, straight to Windows Error Reporting), the debug runtime's reports to stderr, a vectored handler and
	an unhandled-exception filter. Each prints one line on stderr, which names it,

	    <program>: unhandled <SIG> (<what>) at address <a>, pc <p>, <handler>: terminating

	and ends the process with 128 + the signal's number, as a fatal signal ends one on Linux (SIGSEGV 139, SIGILL 132,
	SIGTRAP 133, SIGABRT 134, SIGFPE 136). No exception reaches Windows Error Reporting through it.

	The vectored handler runs before any frame is searched: it takes an access violation, an in-page error and a
	misaligned access where the pc is in the program's image or in no module at all (generated code, or a pc that a
	broken stack returned to: the frames of such a stack cannot be searched, and the filter would never run), an
	illegal or privileged instruction, an integer divide and a stack overflow wherever they are. A fault inside a
	module of the system goes on to that module's own handlers (a stack walk that guards its reads: windows.abi.xmm,
	the walks of tests/debugger/sh4bp/machine.cpp). The filter takes what nobody else took: a breakpoint, a C++
	exception nobody caught, and every other exception.

	The faults of common.guard.windows: NULLDC_TEST_GUARD_FAULT=<kind> raises one right behind the policy, before main.
	Each is taken by one handler, which the line names; without that handler the fault goes elsewhere:

	    segv      a write to address 16              139  the vectored handler   (else the filter)
	    abort     abort()                            134  the SIGABRT handler    (else exit code 3)
	    div       an integer divide by zero          136  the vectored handler   (else the filter)
	    throw     a C++ exception nobody catches     134  the filter             (else the C runtime's filter:
	                                                                              std::terminate, abort())
	    trap      __debugbreak()                     133  the filter             (else Windows Error Reporting)
	    invparam  strcpy_s into a buffer of size 0   134  the invalid-parameter  (else __fastfail: Windows Error
	                                                      handler                 Reporting)
	    purecall  a pure virtual function called     134  the pure-call handler  (else abort())
	              in its destructor
	    stack     a write to address 0 with rsp in   139  the vectored handler   (else no handler at all: no frame
	              memory of its own, out of the                                   can be searched and no filter is
	              thread's stack                                                  reached, Windows Error Reporting)
*/
#if defined(_WIN32)

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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#pragma comment(lib, "wer.lib")

// What test_guard.cpp names (/include) so that the linker takes this object out of nulldc_test_common
extern "C" int nulldc_win_policy;
int nulldc_win_policy = 1;

// What common.policy.windows reads in the file of every program of the tests project (policy_windows.cmake): this
// text is in the image of a program that has the policy, whose constructor below names it, so that no linker drops it
extern "C" const char nulldc_win_policy_mark[];
const char nulldc_win_policy_mark[] = "nulldc_win_policy: no box, no crash report, one line and 128 + the signal's number";

namespace {

char program_name[96] = "program";
const char* image_start;
const char* image_end;

[[noreturn]] void policy_fatal(const char* sig, int number, const char* what, const void* address, const void* pc,
	const char* handler)
{
	char line[384];
	int n = snprintf(line, sizeof(line), "\n%s: unhandled %s (%s) at address %p, pc %p, %s: terminating\n", program_name, sig,
		what, address, pc, handler);
	DWORD written;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, n > 0 ? (DWORD)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1) : 0, &written,
		NULL);
	TerminateProcess(GetCurrentProcess(), 128 + number);
	for (;;)
		Sleep(1000);
}

// The data address of an access violation or an in-page error, else where the exception was raised
const void* fault_address(const EXCEPTION_RECORD* r)
{
	if ((r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && r->NumberParameters >= 2)
		return (const void*)r->ExceptionInformation[1];
	return r->ExceptionAddress;
}

[[noreturn]] void policy_exception(const EXCEPTION_POINTERS* ep, const char* handler)
{
	const EXCEPTION_RECORD* r = ep->ExceptionRecord;
	const void* pc = (const void*)ep->ContextRecord->Rip;
	const void* address = fault_address(r);
	switch (r->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:     policy_fatal("SIGSEGV", 11, "access violation", address, pc, handler);
	case EXCEPTION_IN_PAGE_ERROR:        policy_fatal("SIGSEGV", 11, "in-page error", address, pc, handler);
	case EXCEPTION_DATATYPE_MISALIGNMENT: policy_fatal("SIGSEGV", 11, "misaligned access", address, pc, handler);
	case EXCEPTION_STACK_OVERFLOW:       policy_fatal("SIGSEGV", 11, "stack overflow", address, pc, handler);
	case EXCEPTION_ILLEGAL_INSTRUCTION:  policy_fatal("SIGILL", 4, "illegal instruction", address, pc, handler);
	case EXCEPTION_PRIV_INSTRUCTION:     policy_fatal("SIGILL", 4, "privileged instruction", address, pc, handler);
	case EXCEPTION_INT_DIVIDE_BY_ZERO:   policy_fatal("SIGFPE", 8, "integer divide by zero", address, pc, handler);
	case EXCEPTION_INT_OVERFLOW:         policy_fatal("SIGFPE", 8, "integer overflow", address, pc, handler);
	case EXCEPTION_BREAKPOINT:           policy_fatal("SIGTRAP", 5, "breakpoint", address, pc, handler);
	case 0xE06D7363:                     policy_fatal("SIGABRT", 6, "C++ exception", address, pc, handler);
	default:
	{
		char what[32];
		snprintf(what, sizeof(what), "exception %08lx", (unsigned long)r->ExceptionCode);
		policy_fatal("SIGABRT", 6, what, address, pc, handler);
	}
	}
}

LONG CALLBACK policy_vectored(EXCEPTION_POINTERS* ep)
{
	const EXCEPTION_RECORD* r = ep->ExceptionRecord;
	const char* pc = (const char*)r->ExceptionAddress;
	PVOID base = nullptr;
	switch (r->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		if ((pc < image_start || pc >= image_end) && RtlPcToFileHeader((PVOID)pc, &base) != nullptr)
			return EXCEPTION_CONTINUE_SEARCH;
		policy_exception(ep, "the vectored handler");
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
		policy_exception(ep, "the vectored handler");
	default:
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

LONG WINAPI policy_unhandled(EXCEPTION_POINTERS* ep)
{
	policy_exception(ep, "the filter");
}

void policy_sigabrt(int)
{
	policy_fatal("SIGABRT", 6, "abort", nullptr, _ReturnAddress(), "the SIGABRT handler");
}

void policy_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
	policy_fatal("SIGABRT", 6, "invalid parameter", nullptr, _ReturnAddress(), "the invalid-parameter handler");
}

void policy_purecall()
{
	policy_fatal("SIGABRT", 6, "pure virtual call", nullptr, _ReturnAddress(), "the pure-call handler");
}

// purecall: during the destructor of PureBase the object is a PureBase, whose Pure is the C runtime's _purecall.
// The call goes through a pointer the compiler cannot see through, so that it is the virtual call of the object
struct PureBase
{
	virtual ~PureBase();
	virtual void Pure() = 0;
};

struct PureDerived : PureBase
{
	void Pure() override {}
};

PureBase* volatile pure_self;

PureBase::~PureBase()
{
	pure_self = this;
	pure_self->Pure();
}

// stack: rsp moved into memory of its own (rcx, the first argument), then a write to address 0: the frames from there
// cannot be searched, neither the program's nor the C runtime's, and only a vectored handler sees the fault
void fault_on_a_broken_stack()
{
	static const unsigned char code[] = {
		0x48, 0x89, 0xCC,	// mov rsp, rcx
		0x31, 0xC0,		// xor eax, eax
		0x89, 0x00,		// mov [rax], eax
		0xCC, 0xCC, 0xCC,	// int3
	};
	const SIZE_T stack_size = 1024 * 1024;
	unsigned char* stack = (unsigned char*)VirtualAlloc(nullptr, stack_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	unsigned char* page = (unsigned char*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	DWORD old;
	if (!stack || !page)
		return;
	memcpy(page, code, sizeof(code));
	if (!VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old))
		return;
	FlushInstructionCache(GetCurrentProcess(), page, 4096);
	((void (*)(void*))(void*)page)(stack + stack_size - 64);
}

// NULLDC_TEST_GUARD_FAULT=<kind>: the fault of common.guard.windows (the table at the top of the file)
void fault_from_the_environment()
{
	char kind[32];
	size_t n = 0;
	if (getenv_s(&n, kind, sizeof(kind), "NULLDC_TEST_GUARD_FAULT") != 0 || n == 0)
		return;
	fprintf(stderr, "NULLDC_TEST_GUARD_FAULT: %s\n", kind);
	fflush(stderr);
	static volatile uintptr_t bad_address = 16;
	static volatile int zero = 0;
	static volatile size_t no_room = 0;
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
	else if (!strcmp(kind, "trap"))
		__debugbreak();
	else if (!strcmp(kind, "invparam"))
	{
		char buffer[4];
		strcpy_s(buffer, no_room, "x");
	}
	else if (!strcmp(kind, "purecall"))
		delete new PureDerived;
	else if (!strcmp(kind, "stack"))
		fault_on_a_broken_stack();
	fprintf(stderr, "NULLDC_TEST_GUARD_FAULT: %s did not end the program\n", kind);
	fflush(stderr);
}

const char* volatile policy_mark;

struct WindowsPolicy
{
	WindowsPolicy()
	{
		policy_mark = nulldc_win_policy_mark;

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

		// room for the handlers on a thread whose stack has run out
		ULONG guarantee = 64 * 1024;
		SetThreadStackGuarantee(&guarantee);

		// the program's image, from its headers, and its name for the line
		const char* self = (const char*)GetModuleHandleW(NULL);
		const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(self + ((const IMAGE_DOS_HEADER*)self)->e_lfanew);
		image_start = self;
		image_end = self + nt->OptionalHeader.SizeOfImage;

		char path[MAX_PATH];
		DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
		if (length > 0 && length < sizeof(path))
		{
			const char* name = path;
			for (const char* p = path; *p; p++)
				if (*p == '\\' || *p == '/')
					name = p + 1;
			strncpy_s(program_name, sizeof(program_name), name, _TRUNCATE);
			char* dot = strrchr(program_name, '.');
			if (dot && dot != program_name)
				*dot = 0;
		}

		AddVectoredExceptionHandler(0, policy_vectored);
		SetUnhandledExceptionFilter(policy_unhandled);

		fault_from_the_environment();
	}
};

WindowsPolicy windows_policy;

}

#endif
