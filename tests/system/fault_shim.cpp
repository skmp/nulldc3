/*
	Preloaded into the emulator by the fault cases of the system tests (LD_PRELOAD): makes the process fault
	the way a defect of the emulator would, from a thread of its own.

	NULLDC_TEST_FAULT = segv | bus | ill selects the fault. Without it the library does nothing.
	The fault comes 300 ms after the emulator has installed its handler for that signal.
*/
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static bool has_handler(int signum)
{
	struct sigaction current;

	if (sigaction(signum, nullptr, &current) != 0)
		return false;

	return (current.sa_flags & SA_SIGINFO) != 0 || (current.sa_handler != SIG_DFL && current.sa_handler != SIG_IGN);
}

static void* fault_thread(void* arg)
{
	const char* kind = (const char*)arg;
	int signum = !strcmp(kind, "bus") ? SIGBUS : !strcmp(kind, "ill") ? SIGILL : SIGSEGV;

	while (!has_handler(signum))
		usleep(10 * 1000);
	usleep(300 * 1000);

	if (signum == SIGSEGV)
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
	const char* kind = getenv("NULLDC_TEST_FAULT");

	if (kind != nullptr)
	{
		pthread_t thread;
		pthread_create(&thread, nullptr, fault_thread, (void*)kind);
		pthread_detach(thread);
	}
}
