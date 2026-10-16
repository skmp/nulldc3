/*
	sh4bp_test [gate...]: the gates of SH4 code breakpoints and steps that need no emulator thread (README.md).
	Without arguments every gate runs. Exit code 0 when every check of the gates that ran held.
*/
#include "gates.h"
#include "machine.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

static u32 checks;
static u32 failures;
static const char* gate_name = "";

bool check(bool ok, const char* fmt, ...)
{
	char text[512];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	checks++;

	if (!ok)
	{
		failures++;
		printf("%s: FAILED: %s\n", gate_name, text);
	}
	else
	{
		printf("%s: ok: %s\n", gate_name, text);
	}

	fflush(stdout);

	return ok;
}

void note(const char* fmt, ...)
{
	char text[512];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	printf("%s: note: %s\n", gate_name, text);
	fflush(stdout);
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++)
	{
		bool known = false;

		for (u32 g = 0; g < gate_count; g++)
			known = known || strcmp(argv[i], gates[g].name) == 0;

		if (!known)
		{
			printf("no gate is called %s. The gates:", argv[i]);
			for (u32 g = 0; g < gate_count; g++)
				printf(" %s", gates[g].name);
			printf("\n");
			return 2;
		}
	}

	machine_init();

	u32 failed_gates = 0;
	u32 ran = 0;

	for (u32 g = 0; g < gate_count; g++)
	{
		bool selected = argc == 1;

		for (int i = 1; i < argc; i++)
			selected = selected || strcmp(argv[i], gates[g].name) == 0;

		if (!selected)
			continue;

		u32 checks_before = checks;
		u32 failures_before = failures;

		gate_name = gates[g].name;
		gates[g].run();

		//a gate without a check did not test anything
		bool pass = failures == failures_before && checks != checks_before;

		printf("%s: %s, %u of %u checks failed: %s\n", gates[g].name, pass ? "PASS" : "FAIL", failures - failures_before,
			checks - checks_before, gates[g].what);

		ran++;
		if (!pass)
			failed_gates++;
	}

	printf("%u gates, %u failed, %u checks\n", ran, failed_gates, checks);

	return failed_gates == 0 && ran != 0 ? 0 : 1;
}
