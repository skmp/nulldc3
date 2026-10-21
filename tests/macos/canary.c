/*
	This file is part of the tests of nullDC

	macos.sandbox.canary: what a program under tests/macos/macrun.py can reach. It tries to reach the window server
	the way the smallest client does, without AppKit (CGSessionCopyCurrentDictionary: the window server's session of
	this process, NULL when the window server cannot be reached), and it prints the kernel's view of the process,
	from sandbox_check: whether it is sandboxed at all, and whether the window server's mach-lookup is denied. It
	makes no NSApplication, no window and no Dock icon in any case, so it may also run outside the profile (where
	what it prints is only reported).

	    canary: sandboxed=<0|1> windowserver-lookup=<denied|allowed> windowserver=<unreached|reached>

	Exit status 0 when the process is sandboxed, the lookup is denied and the window server was not reached; 1
	otherwise.
*/
#include <stdio.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

/* sandbox_check and its filter types are SPI, not in the SDK's <sandbox.h>: declared here as WebKit's SandboxSPI.h
   declares them */
enum sandbox_filter_type {
	SANDBOX_FILTER_NONE,
	SANDBOX_FILTER_PATH,
	SANDBOX_FILTER_GLOBAL_NAME,
};
extern int sandbox_check(pid_t pid, const char* operation, enum sandbox_filter_type type, ...);

int main(void)
{
	int sandboxed = sandbox_check(getpid(), NULL, SANDBOX_FILTER_NONE);
	int denied = sandbox_check(getpid(), "mach-lookup", SANDBOX_FILTER_GLOBAL_NAME, "com.apple.windowserver.active");

	CFDictionaryRef session = CGSessionCopyCurrentDictionary();
	int reached = session != NULL;
	if (session)
		CFRelease(session);

	printf("canary: sandboxed=%d windowserver-lookup=%s windowserver=%s\n", sandboxed,
	       denied == 1 ? "denied" : denied == 0 ? "allowed" : "unknown", reached ? "reached" : "unreached");
	fflush(stdout);
	return sandboxed == 1 && denied == 1 && !reached ? 0 : 1;
}
