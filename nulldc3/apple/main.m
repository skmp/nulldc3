/*
    This file is part of reicast-osx
*/
#include "license/bsd"

#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/task.h>

// The start of the emulator on macOS, headless or with its window (AppDelegate.mm): nothing of AppKit is made here.
// A headless run never makes an NSApplication; a run with a window makes it when the emulator asks for its window.
int nulldc_macos_main(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    // No task-level Mach handler for bad accesses, so that the kernel turns them into SIGSEGV and SIGBUS for the
    // fault handler (linux/common.cpp), also where a debugger had set its own port
    task_set_exception_ports(
                            mach_task_self(),
                            EXC_MASK_BAD_ACCESS,
                            MACH_PORT_NULL,//m_exception_port,
                            EXCEPTION_DEFAULT,
                            0);
    return nulldc_macos_main(argc, argv);
}
