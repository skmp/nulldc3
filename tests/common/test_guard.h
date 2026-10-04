/*
	Runs a piece of emulator code so that the ways it can go wrong end that piece and not the test run:
	die()/verify() (msgboxf followed by os_DebugBreak), test_guard_abort(), fatal signals and, through the
	watchdog, hangs.

	This file provides msgboxf and os_DebugBreak for everything that links it.
*/
#pragma once
#include "types.h"

enum TestGuardResult
{
	TGR_OK,			//the function returned
	TGR_DIE,		//die() or verify()
	TGR_ABORT,		//test_guard_abort()
	TGR_SIGNAL,		//SIGSEGV, SIGBUS, SIGILL, SIGFPE or SIGTRAP
	TGR_TIMEOUT,	//still running when the watchdog fired
};

typedef void TestGuardFP(void* arg);

//Installs the signal handlers. A guarded call that takes longer than watchdog_ms is ended with TGR_TIMEOUT
//(0: no watchdog). Signals and the watchdog need POSIX, elsewhere only die() and test_guard_abort() are caught
void test_guard_init(u32 watchdog_ms);

//Runs fn(arg). Not reentrant
TestGuardResult test_guard_run(TestGuardFP* fn, void* arg);

//Ends the running test_guard_run with TGR_ABORT. Does not return. Outside of a guard it ends the process
void test_guard_abort(const char* fmt, ...);

//Why the last test_guard_run did not return TGR_OK
const char* test_guard_message();

//msgboxf calls since the last test_guard_run started and the text of the last one. A msgboxf that is not
//followed by os_DebugBreak is only a notification, the code carries on
u32 test_guard_msgbox_count();
const char* test_guard_last_msgbox();

//msgboxf text is only recorded, this prints it as well
void test_guard_echo_msgbox(bool echo);

const char* test_guard_result_name(TestGuardResult result);
