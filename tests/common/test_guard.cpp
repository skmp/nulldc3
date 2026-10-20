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

void test_guard_init(u32 watchdog)
{
	watchdog_ms = watchdog;

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
