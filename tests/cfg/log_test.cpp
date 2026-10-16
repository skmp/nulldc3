/*
	cfg.log_file: cl_LogToFile (cfg/cl.cpp), what Windows' --log does with stdout and stderr, on Linux.

	    cfg_log <work directory>

	A child process sends both streams into a log file that is there with other text, writes lines to stdout and
	stderr in turn and ends with _exit, which flushes nothing, as a crash or die() does. The file has to hold
	every line, in the order they were written, and nothing of before: one stream may not write over the other,
	and neither may keep lines in a buffer. A log that cannot be opened is refused and leaves the streams as they
	were.
*/
#include "types.h"
#include "cfg/cfg.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

// What cl.cpp takes from stdclass.cpp, unused here
string get_writable_config_path(const string& filename) { return filename; }
string get_readonly_config_path(const string& filename) { return filename; }
void set_user_config_dir(const string& dir) { }
void set_user_data_dir(const string& dir) { }
void add_system_config_dir(const string& dir) { }
void add_system_data_dir(const string& dir) { }
void clear_dirs() { }

static int failed = 0;

#define CHECK(condition) \
	do { if (!(condition)) { printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); failed++; } } while (0)

static const int LINES = 200;

static std::string read_text(const std::string& path)
{
	std::ifstream file(path);
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

// Runs body in a child process; its exit status
static int in_child(void (*body)(const std::string&), const std::string& path)
{
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0)
	{
		body(path);
		_exit(99);
	}
	int status = 0;
	if (pid < 0 || waitpid(pid, &status, 0) != pid)
		return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static void write_lines(const std::string& path)
{
	if (!cl_LogToFile(path))
		_exit(3);
	for (int i = 0; i < LINES; i++)
	{
		printf("stdout line %d\n", i);
		fprintf(stderr, "stderr line %d\n", i);
	}
	_exit(7);	// no flush
}

static void cannot_open(const std::string& path)
{
	if (cl_LogToFile(path))
		_exit(4);
	// the streams are the ones of before: this line reaches the output of the test
	if (printf("cfg.log_file: a log that cannot be opened leaves stdout as it was\n") < 0 || fflush(stdout) != 0)
		_exit(5);
	_exit(0);
}

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("usage: cfg_log <work directory>\n");
		return 2;
	}
	std::string work = argv[1];
	std::filesystem::remove_all(work);
	std::filesystem::create_directories(work);
	std::string path = work + "/nulldc3-log.txt";

	{
		std::ofstream old(path);
		for (int i = 0; i < 4 * LINES; i++)
			old << "a line of the run before\n";
	}

	CHECK(in_child(write_lines, path) == 7);
	std::string expected;
	for (int i = 0; i < LINES; i++)
		expected += "stdout line " + std::to_string(i) + "\nstderr line " + std::to_string(i) + "\n";
	std::string text = read_text(path);
	CHECK(text == expected);
	if (text != expected)
	{
		size_t at = 0;
		while (at < text.size() && at < expected.size() && text[at] == expected[at])
			at++;
		printf("the log has %zu bytes, %zu expected; they differ from byte %zu on:\n  log:      '%s'\n  expected: '%s'\n",
			text.size(), expected.size(), at, text.substr(at, 60).c_str(), expected.substr(at, 60).c_str());
	}

	CHECK(in_child(cannot_open, work + "/no/such/directory/nulldc3-log.txt") == 0);

	if (failed)
	{
		printf("cfg.log_file: FAIL (%d checks)\n", failed);
		return 1;
	}
	printf("cfg.log_file: PASS\n");
	return 0;
}
