/*
	Preloaded into the emulator by system.vmem.taken (LD_PRELOAD): the host has no memfd_create. The emulator then
	has to make the file of its memory layout the other ways it knows, an shm name or a file of its data directory,
	which the case can see and take away.

	Every call is refused with ENOSYS, as a kernel without the call does, and says so on stderr: the case checks that
	the line is there, so that a run which never asked for a memfd does not pass for one that asked and was refused.
*/
#include <errno.h>
#include <string.h>
#include <unistd.h>

extern "C" int memfd_create(const char* name, unsigned int flags) noexcept
{
	static const char line[] = "vmem_shim: memfd_create refused\n";

	(void)name;
	(void)flags;
	if (write(STDERR_FILENO, line, strlen(line)) < 0) { }
	errno = ENOSYS;
	return -1;
}
