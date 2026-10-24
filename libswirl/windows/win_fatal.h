/*
	This file is part of libswirl
*/
#pragma once

// The no-dialog policy of every run on Windows and the fatal ends (win_fatal.cpp). Installed first thing in main.
void win_install_fatal_policy();

// The fatal line of the platform (as Linux words it: fault_handler: unhandled <SIG> at address <a>, pc <p>, not a
// guest memory fault: terminating) on stderr, then the process ends with 128 + the signal's number
[[noreturn]] void win_fatal(const char* sig, int signal_number, const void* address, const void* pc);

// Whether an address is in the code region of the recompiler (win_jit_unwind.cpp)
bool win_in_code_region(const void* address);

// The test hooks of the Windows build: NULLDC_TEST_FATAL=<kind> raises that fault (called right after
// nulldc_init), NULLDC_TEST_UI_REPORT=1 prints the line of the windows and the policy at the end of main
void win_test_fatal();
void win_test_ui_report(bool window_class_registered, const char* window_class);
