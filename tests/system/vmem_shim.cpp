/*
	Preloaded into the emulator by the cases of vmem_test.py (LD_PRELOAD): it takes away the ways to the file of the
	memory layout that a case says, which the emulator then has to do without.

	memfd_create: the host has none. Every call is refused with ENOSYS, as a kernel without the call does, and says
	so on stderr: a case checks that the line is there, so that a run which never asked for a memfd does not pass
	for one that asked and was refused. With VMEM_SHIM_MEMFD=1 in the environment the call is the host's.

	mkstemp: with VMEM_SHIM_NO_FILE=1 a name with dcnzorz_mem in it is refused with EACCES, as in a data directory
	that cannot be written, and the shim says so; every other call is the host's. The data directory of a run is the
	directory of --portable, which the emulator checks it can write before it starts: this is the run that has a
	data directory and still gets no file for its memory there, which a run with a fresh HOME whose XDG_DATA_HOME
	could not be made was before the emulator was portable.
*/
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void say(const char* line)
{
	if (write(STDERR_FILENO, line, strlen(line)) < 0) { }
}

static bool asked(const char* variable)
{
	const char* value = getenv(variable);
	return value != nullptr && strcmp(value, "1") == 0;
}

extern "C" int memfd_create(const char* name, unsigned int flags) noexcept
{
	if (asked("VMEM_SHIM_MEMFD"))
	{
		using MemfdCreate = int (*)(const char*, unsigned int);
		static MemfdCreate next = (MemfdCreate)dlsym(RTLD_NEXT, "memfd_create");
		if (next != nullptr)
			return next(name, flags);
	}
	say("vmem_shim: memfd_create refused\n");
	errno = ENOSYS;
	return -1;
}

extern "C" int mkstemp(char* name_template)
{
	if (asked("VMEM_SHIM_NO_FILE") && strstr(name_template, "dcnzorz_mem") != nullptr)
	{
		say("vmem_shim: mkstemp refused\n");
		errno = EACCES;
		return -1;
	}
	using Mkstemp = int (*)(char*);
	static Mkstemp next = (Mkstemp)dlsym(RTLD_NEXT, "mkstemp");
	if (next == nullptr)
	{
		errno = ENOSYS;
		return -1;
	}
	return next(name_template);
}
