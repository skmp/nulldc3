/*
	Preloaded into the emulator by the fault cases of the system tests (LD_PRELOAD): makes the process fault
	the way a defect of the emulator would, from a thread of its own, and counts the faults the emulator takes
	by itself.

	NULLDC_TEST_FAULT = segv | bus | ill | raise selects the fault. raise is not one: a SIGSEGV that the thread
	sends to itself with raise(). Without the variable no fault is made.
	NULLDC_TEST_FAULT_WHEN = the path of a file. The fault comes once the emulator has installed its handler for
	that signal and the file exists: the test creates it at the point of the run where it wants the fault, so how
	long the emulator takes to get there does not matter. Without the variable the fault comes as soon as the
	handler is installed.
	NULLDC_TEST_FAULT_COUNT = the path of a file. It holds, as an unsigned of the host, how many SIGSEGVs the kernel
	raised for a fault (si_code > 0: not the ones that were sent) and gave to the handler of the emulator. The
	file is mapped, so the count is there however the process ends.
*/
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

// The emulator installs its handlers with sigaction(), and the dynamic linker finds the one below in front of the
// one of libc: with NULLDC_TEST_FAULT_COUNT the handler for SIGSEGV is installed behind one that counts.
static unsigned* segv_faults;
static struct sigaction emulator_segv;

static void counting_segv_handler(int signum, siginfo_t* si, void* context)
{
	if (si->si_code > 0)
		__atomic_add_fetch(segv_faults, 1, __ATOMIC_RELAXED);

	emulator_segv.sa_sigaction(signum, si, context);
}

extern "C" int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) noexcept
{
	typedef int sigaction_fn(int, const struct sigaction*, struct sigaction*);
	static sigaction_fn* libc_sigaction = (sigaction_fn*)dlsym(RTLD_NEXT, "sigaction");

	if (segv_faults != nullptr && signum == SIGSEGV && act != nullptr && (act->sa_flags & SA_SIGINFO) != 0)
	{
		emulator_segv = *act;

		struct sigaction counting = *act;
		counting.sa_sigaction = counting_segv_handler;
		return libc_sigaction(signum, &counting, oldact);
	}

	return libc_sigaction(signum, act, oldact);
}

static bool has_handler(int signum)
{
	struct sigaction current;

	if (sigaction(signum, nullptr, &current) != 0)
		return false;

	return (current.sa_flags & SA_SIGINFO) != 0 || (current.sa_handler != SIG_DFL && current.sa_handler != SIG_IGN);
}

// NULLDC_TEST_FAULT_WHEN, read before main() runs: no thread of the emulator can be changing the environment then
static const char* fault_when;

static void* fault_thread(void* arg)
{
	const char* kind = (const char*)arg;
	int signum = !strcmp(kind, "bus") ? SIGBUS : !strcmp(kind, "ill") ? SIGILL : SIGSEGV;

	while (!has_handler(signum))
		usleep(10 * 1000);
	while (fault_when != nullptr && access(fault_when, F_OK) != 0)
		usleep(10 * 1000);

	if (!strcmp(kind, "raise"))
	{
		raise(SIGSEGV);
	}
	else if (signum == SIGSEGV)
	{
		// a page nothing may touch, and nothing of the emulator knows
		volatile int* p = (volatile int*)mmap(nullptr, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		*p = 1;
	}
	else if (signum == SIGBUS)
	{
		// a mapping behind the end of its file
		char name[] = "/tmp/nulldc-fault-XXXXXX";
		int fd = mkstemp(name);
		unlink(name);
		volatile char* p = (volatile char*)mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		*p = 1;
	}
	else
	{
#if defined(__x86_64__) || defined(__i386__)
		__asm__ volatile("ud2");
#elif defined(__aarch64__) || defined(__arm__)
		__asm__ volatile(".inst 0");
#else
		raise(SIGILL);
#endif
	}

	return nullptr;
}

__attribute__((constructor)) static void fault_shim_start()
{
	const char* count = getenv("NULLDC_TEST_FAULT_COUNT");

	if (count != nullptr)
	{
		int fd = open(count, O_RDWR | O_CREAT | O_TRUNC, 0600);

		if (fd < 0 || ftruncate(fd, sizeof(unsigned)) != 0)
			abort();

		void* mapped = mmap(nullptr, sizeof(unsigned), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (mapped == MAP_FAILED)
			abort();
		close(fd);

		segv_faults = (unsigned*)mapped;
	}

	const char* kind = getenv("NULLDC_TEST_FAULT");

	if (kind != nullptr)
	{
		fault_when = getenv("NULLDC_TEST_FAULT_WHEN");

		pthread_t thread;
		pthread_create(&thread, nullptr, fault_thread, (void*)kind);
		pthread_detach(thread);
	}
}
