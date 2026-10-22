/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "types.h"
#include "cfg/cfg.h"

#include <pthread.h>

#if HOST_OS==OS_LINUX || HOST_OS == OS_DARWIN

#if HOST_OS == OS_DARWIN
	#define _XOPEN_SOURCE 1
	#define __USE_GNU 1
	#include <TargetConditionals.h>
    #include <dlfcn.h>
    #include <util.h>
	#include <sys/sysctl.h>
	#include "utils/run_control.h"
#endif
#if FEAT_HAS_SERIAL_TTY
    #include <sys/stat.h>
    #if HOST_OS == OS_DARWIN
        #import <util.h>
    #else
        #include <pty.h>
    #endif
#endif
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/time.h>
#if !defined(TARGET_EMSCRIPTEN) && !defined(TARGET_OSX) && !defined(TARGET_OSX_X64)
  #include <sys/personality.h>
  #include <dlfcn.h>
#endif
#include <unistd.h>
#include "hw/sh4/dyna/blockmanager.h"

#include "linux/context.h"

#include "hw/sh4/dyna/ngen.h"
#include "hw/mem/_vmem.h"
#include "rend/TexCache.h"

#include "libswirl.h"

#define fault_printf(...)

#if !defined(TARGET_NO_EXCEPTIONS)

#define TRAP_STACK_SIZE (1024 * 1024)

static u8* trap_ptr_fault;
static u8* trap_ptr_stack;
static u8* trap_ptr_stack_top;

static unat trap_si_addr;
static unat trap_pc;
static bool trap_handled = true;
static bool in_handler = false;

u8* generic_fault_handler ()
{
	fault_printf("generic_fault_handler\n");
	rei_host_context_t ctx;

	fault_printf("generic_fault_handler\n");
	context_from_segfault(&ctx);

	fault_printf("generic_fault_handler: original pc: %p, trap pc: %p\n", ctx.pc, trap_pc);
	ctx.pc = trap_pc;

	fault_printf("generic_fault_handler\n");
	
	// a fault before there is a machine is nobody's to handle
	if (virtualDreamcast && virtualDreamcast->HandleFault(trap_si_addr, &ctx))
	{
		fault_printf("generic_fault_handler: final pc: %p, trap pc: %p\n", ctx.pc, trap_pc);
		context_to_segfault(&ctx);
		fault_printf("generic_fault_handler: handled\n");

		trap_handled = true;
	}
	else
	{
		fault_printf("generic_fault_handler: not in handled SIGSEGV pc: %lx addr:%p here %p\n", ctx.pc, (void*)trap_si_addr, (void*)generic_fault_handler);
		trap_handled = false;
	}

	fault_printf("generic_fault_handler: end\n");
	return trap_ptr_fault;
}

naked void re_raise_fault()
{
	__asm__ __volatile__(
        #if HOST_CPU == CPU_X64
            "movq %0, %%rsp\n"
            "call *%1\n"
            "movb $0, (%%rax)"
	    #elif HOST_CPU == CPU_ARM64
	        "mov sp, %0\n"
	        "blr %1\n"
	        "ldr w1, [x0]"
	    #else
	    	#error missing cpu
        #endif
	        : : "r"(trap_ptr_stack_top), "r"(generic_fault_handler) : "memory"

    );
}

// A fault that is not a guest memory fault ends the process: a line on stderr, then the default action of the
// signal (exit status 128 + signal, a core file where they are enabled).
// Blocking here instead, so that a native debugger can be attached and set break_segfault, is opt-in:
// NULLDC_FAULT_BLOCK=1 in the environment, or config:Debug.BlockOnFault=yes once the config is loaded.
bool break_segfault = false;

// Written by the thread of main only, while the emulator starts: from the environment before it has created a
// thread, from the config key before the emulator thread exists. Read by the signal handlers, on whichever
// thread faults.
static bool fault_blocks = false;

void common_linux_fault_block(bool block)
{
	fault_blocks = block;
}

static const char* fault_signal_name(int sn)
{
	switch (sn)
	{
	case SIGSEGV: return "SIGSEGV";
	case SIGBUS: return "SIGBUS";
	case SIGILL: return "SIGILL";
	case SIGTRAP: return "SIGTRAP";
	case SIGABRT: return "SIGABRT";
	case SIGFPE: return "SIGFPE";
	default: return "signal";
	}
}

// A signal that was sent to the process or to one of its threads (kill, tgkill, raise, sigqueue) is not a fault of
// the instruction it interrupts, and it has no fault address: si_addr shares its storage with the pid and the uid of
// the sender. Linux gives such a signal a si_code of 0 or below, and a positive one to what the kernel raises for a
// fault.
static bool signal_was_sent(const siginfo_t* si)
{
#if HOST_OS == OS_LINUX
	return si->si_code <= 0;
#else
	return false;
#endif
}

static void fatal_error(int sn, siginfo_t* si, void* segfault_ctx)
{
	bool sent = signal_was_sent(si);
	const char* action = fault_blocks ? "blocking for a debugger" : "terminating";

	// stdio may be what faulted, or hold its lock: the line is formatted here and written with write()
	char line[256];
	int len;
	if (sent)
		len = snprintf(line, sizeof(line), "fault_handler: %s sent by pid %d (si_code %d), not a fault: %s\n",
			fault_signal_name(sn), (int)si->si_pid, si->si_code, action);
	else
		len = snprintf(line, sizeof(line), "fault_handler: unhandled %s at address %p, pc %p, not a guest memory fault: %s\n",
			fault_signal_name(sn), si->si_addr, (void*)segfault_get_pc(segfault_ctx), action);
	if (len > (int)sizeof(line) - 1)
		len = sizeof(line) - 1;
	if (write(STDERR_FILENO, line, len) < 0) { }

	if (fault_blocks)
	{
		while(!break_segfault)
		{
			printf("fault_handler: Blocking before restoring default %s handler\n", fault_signal_name(sn));
			sleep(1);
		}
	}

#if HOST_OS == OS_DARWIN
	// An automated run (headless, or with a key that ends the run: run control) ends here, with the exit status a
	// process that the signal ends has on Linux, 128 + the signal, by an exit: on macOS every process that a signal
	// ends leaves a crash report in ~/Library/Logs/DiagnosticReports (PL-D45). A run with a person in front of it
	// dies of the signal below, and its crash report is that person's.
	if (!fault_blocks && runctl_automated())
		_exit(128 + sn);
#endif

	signal(sn, SIG_DFL);

	// The signal is blocked while its handler runs and is delivered when the handler returns. Without it a
	// signal that was sent, not caused by the instruction the handler returns to, would be lost. A fault that
	// blocked for a debugger is left to come again from its instruction, where the debugger then stands.
	if (!fault_blocks || sent)
		raise(sn);
}

// SIGBUS and SIGILL are never a guest memory fault here; on Darwin SIGBUS can be one, and SIGILL, SIGTRAP, SIGABRT
// and SIGFPE are never
static void fatal_signal_handler(int sn, siginfo_t* si, void* segfault_ctx)
{
	fatal_error(sn, si, segfault_ctx);
}

static void fault_handler (int sn, siginfo_t * si, void *segfault_ctx)
{
	fault_printf("fault_handler: thread: %p si_addr:%p trap_ptr_fault: %p\n", (void*)pthread_self(), si->si_addr, trap_ptr_fault);

	// Never a guest memory fault. Taken for one, the instruction it interrupted would run again without a fault:
	// the signal would be lost, and the next guest memory fault refused for it.
	if (signal_was_sent(si))
	{
		fatal_error(sn, si, segfault_ctx);
		return;
	}

	if ((unat)si->si_addr == (unat)trap_ptr_fault)
	{
		if (!in_handler) {
			fault_printf("fault_handler: !in_handler\n");

            fatal_error(sn, si, segfault_ctx);
            return;
		}

		segfault_load(segfault_ctx);
		rei_host_context_t ctx;
		context_from_segfault(&ctx);

		in_handler = false;
		fault_printf("fault_handler:resume %p\n", ctx.pc);
	}
	else
	{
		if (in_handler)
		{
			fault_printf("fault_handler: double fault\n");

            fatal_error(sn, si, segfault_ctx);
            return;
		}
		
		in_handler = true;

		if (!trap_handled)
		{
			fault_printf("fault_handler: trap not handled\n");

            fatal_error(sn, si, segfault_ctx);
		}
		else 
		{
			fault_printf("fault_handler:catch -> thunking to %p\n", trap_pc);
			trap_si_addr = (unat)si->si_addr;
			segfault_store(segfault_ctx);
			segfault_set_pc(segfault_ctx, (unat)&re_raise_fault, &trap_pc);
		}
	}

	fault_printf("fault_handler exit\n");
}

typedef int sigaction_fn(int signum, const struct sigaction *act,
                     struct sigaction *oldact);

static sigaction_fn* get_sigaction()
{

	void* libc;
	sigaction_fn* rv = nullptr;

	libc = dlopen("libc.so", RTLD_NOLOAD);
	
	if (libc)
	{
		printf("get_sigaction: found libc.so\n");
		*(void**) (&rv) = dlsym(libc, "sigaction");

		if (rv && rv != sigaction) {
			printf("get_sigaction: libc override detected, working around...\n");
			return rv;
		}
	}

	return &sigaction;
}

static void install_fault_handler(void)
{
	// initialize trap state
	trap_ptr_fault = (u8*)mmap(0, REI_PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    trap_ptr_stack = (u8*)mmap(0, TRAP_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    trap_ptr_stack_top = trap_ptr_stack + TRAP_STACK_SIZE - 128;

	fault_printf("trap_ptr_fault %p\n", trap_ptr_fault);

	struct sigaction act, segv_oact;
	memset(&act, 0, sizeof(act));
	act.sa_sigaction = fault_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;

	auto libc_sigaction = get_sigaction();

	const char* block = getenv("NULLDC_FAULT_BLOCK");
	fault_blocks = block != nullptr && block[0] != 0 && block[0] != '0';

	libc_sigaction(SIGSEGV, &act, &segv_oact);
#if HOST_OS == OS_DARWIN
    // a write to a page that is mapped read-only is a SIGBUS on macOS (an access to no page a SIGSEGV): both can be
    // guest memory faults
    libc_sigaction(SIGBUS, &act, &segv_oact);

    // the fatal signals: in an automated run the line and 128 + the signal by an exit, no crash report
    // (fatal_error). A breakpoint instruction is SIGTRAP there: __builtin_trap(), and what a debugger that is
    // attached takes before it becomes a signal
    act.sa_sigaction = fatal_signal_handler;
    libc_sigaction(SIGILL, &act, &segv_oact);
    libc_sigaction(SIGTRAP, &act, &segv_oact);
    libc_sigaction(SIGABRT, &act, &segv_oact);
    libc_sigaction(SIGFPE, &act, &segv_oact);
#else
	act.sa_sigaction = fatal_signal_handler;
	libc_sigaction(SIGBUS, &act, &segv_oact);
	libc_sigaction(SIGILL, &act, &segv_oact);
#endif
}
#else  // !defined(TARGET_NO_EXCEPTIONS)
// No exceptions/nvmem dummy handlers.
void install_fault_handler(void) {}
void common_linux_fault_block(bool block) {}
#endif // !defined(TARGET_NO_EXCEPTIONS)

#include <errno.h>

double os_GetSeconds()
{
	timeval a;
	gettimeofday (&a,0);
	static u64 tvs_base=a.tv_sec;
	return a.tv_sec-tvs_base+a.tv_usec/1000000.0;
}

#if HOST_OS != OS_LINUX
#if HOST_OS == OS_DARWIN
// A debugger traces this process (P_TRACED of the kernel's view of it)
static bool debugger_attached()
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
	struct kinfo_proc info;
	size_t size = sizeof(info);
	memset(&info, 0, sizeof(info));
	if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0)
		return false;
	return (info.kp_proc.p_flag & P_TRACED) != 0;
}
#endif

// What die() and verify() end in, after their "Fatal error : ..." or "Verify Failed" line on stdout (msgboxf). On
// Darwin, in an automated run with no debugger attached, the process ends itself with 133 (128 + SIGTRAP, as Linux
// ends there), with no fault_handler line and no crash report (PL-D35, PL-D45); otherwise a breakpoint, for a
// debugger, or a SIGTRAP
void os_DebugBreak()
{
#if HOST_OS == OS_DARWIN
	if (runctl_automated() && !debugger_attached())
	{
		fflush(stdout);
		fflush(stderr);
		_exit(128 + SIGTRAP);
	}
#endif
	__builtin_trap();
}
#endif

void linux_fix_personality() {
    #if !defined(TARGET_OS_MAC) && !defined(TARGET_EMSCRIPTEN)
        printf("Personality: %08X\n", personality(0xFFFFFFFF));
        personality(~READ_IMPLIES_EXEC & personality(0xFFFFFFFF));
        printf("Updated personality: %08X\n", personality(0xFFFFFFFF));
    #endif
}

void linux_rpi2_init() {
}

#if HOST_OS == OS_LINUX && HOST_CPU == CPU_ARM64
// The page of an arm64 Linux kernel is 4, 16 or 64 KiB, as the kernel was configured (Asahi Linux and the Raspberry
// Pi 5's own kernel have 16 KiB). The build is made for one, REI_PAGE_SIZE (stdclass.h: 4 KiB on Linux), which the
// guest memory's mirrors, the write protection of its pages and the fault handler's trap page are laid out in. On a
// kernel with other pages the build refuses to start, before it sets anything up: one line on stderr, exit status 1
// (PL-D52). NULLDC_TEST_PAGE_SIZE=<bytes> in the environment is taken for the kernel's page size here, and nowhere
// else: the test hook that shows the refusal on a kernel with 4 KiB pages (tests/linux_arm64)
static void refuse_other_page_size()
{
	long page = sysconf(_SC_PAGESIZE);
	const char* test_page = getenv("NULLDC_TEST_PAGE_SIZE");
	if (test_page != nullptr && test_page[0] != 0)
		page = strtol(test_page, nullptr, 10);

	if (page == REI_PAGE_SIZE)
		return;

	fflush(stdout);
	fprintf(stderr, "nulldc3: this build needs %d KiB pages; this kernel uses %ld KiB\n", REI_PAGE_SIZE / 1024, page / 1024);
	_exit(1);
}
#endif

void common_linux_setup()
{
#if HOST_OS == OS_LINUX && HOST_CPU == CPU_ARM64
	refuse_other_page_size();
#endif

	linux_fix_personality();
	linux_rpi2_init();

	install_fault_handler();
	// signal(SIGINT, exit);
	
	settings.profile.run_counts=0;
	
#if HOST_OS == OS_DARWIN
	// on stderr: this runs before the command line is read, and --version answers with its one line on stdout
	fprintf(stderr, "Linux paging: %ld %08X %08X\n",sysconf(_SC_PAGESIZE),REI_PAGE_SIZE,REI_PAGE_MASK);
#else
	printf("Linux paging: %ld %08X %08X\n",sysconf(_SC_PAGESIZE),REI_PAGE_SIZE,REI_PAGE_MASK);
#endif

#if !defined(TARGET_EMSCRIPTEN)
	verify(REI_PAGE_MASK==(sysconf(_SC_PAGESIZE)-1));
#endif
}

#if FEAT_HAS_SERIAL_TTY
int pty_master = -1;
bool common_serial_pty_setup() {
    bool cleanup_pty_symlink = false;
    if (settings.debug.VirtualSerialPort) {
        int slave;
        char slave_name[2048];
        pty_master = -1;
        if (openpty(&pty_master, &slave, slave_name, nullptr, nullptr) >= 0) {
            // Set the port to be non-blocking
            int flags = fcntl(pty_master, F_GETFL, 0);
            fcntl(pty_master, F_SETFL, flags | O_NONBLOCK);
            
            // Turn ECHO off, we don't want to loop-back
            struct termios tp;
            tcgetattr(pty_master, &tp);
            tp.c_lflag &= ~ECHO;
            tcsetattr(pty_master, TCSAFLUSH, &tp);
            printf("Serial: Created virtual serial port at %s\n", slave_name);

            if (settings.debug.VirtualSerialPortFile.size()) {
                const char *file_name = settings.debug.VirtualSerialPortFile.c_str();
                
                // Check if a symlink already exists at this location, and if so, delete it so we can make a new one
                struct stat stat_buffer;
                if (lstat(file_name, &stat_buffer) == 0 && S_ISLNK(stat_buffer.st_mode)) {
                    // File is a symlink, so delete it
                    if (unlink(file_name) == 0) {
                        printf("Serial: Removed existing symlink at %s\n", file_name);
                    } else {
                        printf("Serial: Failed to remove existing symlink at %s, %s\n", file_name, strerror(errno));
                    }
                }
                
                if (symlink(slave_name, file_name) == 0) {
                    cleanup_pty_symlink = true;
                    printf("Serial: Created symlink to %s\n", file_name);
                } else {
                    printf("Serial: Failed to create symlink to %s, %s\n", file_name, strerror(errno));
                }
            }
            // not for us to use, we use master
            // do not close to avoid EIO though
            // close(slave);
        } else {
            printf("Serial: Failed to create PTY: %s\n", strerror(errno));
        }
    }
    return cleanup_pty_symlink;
}
#endif

#endif
