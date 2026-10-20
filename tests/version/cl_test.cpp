/*
	version.cl: --version and --help of the command line parser (cfg/cl.cpp), without the emulator.

	    version_cl <work directory>

	ParseCommandLine of "nulldc3 --version" returns rv_cli_finish, and what it wrote on stdout is "nullDC <version>"
	and a line end, nothing else, <version> being nulldc_version(); cl_FinishText() is that same text. For "nulldc3
	--help" stdout and cl_FinishText() are the usage text, cl_HelpText(). The Windows build shows cl_FinishText() in
	a message box where stdout cannot be read once the process has ended: it has to be what was asked for, and the
	version is not the usage text. stdout goes into files of the work directory; what the test says goes to stderr.
*/
#include "types.h"
#include "cfg/cfg.h"
#include "nulldc_version.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// What cl.cpp takes from stdclass.cpp, unused here: --version and --help return before a directory is chosen
string get_writable_config_path(const string& filename) { return filename; }
string get_readonly_config_path(const string& filename) { return filename; }
void set_user_config_dir(const string& dir) { }
void set_user_data_dir(const string& dir) { }
void add_system_config_dir(const string& dir) { }
void add_system_data_dir(const string& dir) { }
void clear_dirs() { }

static int failed = 0;

#define CHECK(condition) \
	do { if (!(condition)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); failed++; } } while (0)

static std::string read_text(const std::string& path)
{
	std::ifstream file(path);
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

// ParseCommandLine of "nulldc3 <word>" with stdout into <path>: its return value; the text in stdout_text
static int parse(const std::string& word, const std::string& path, std::string& stdout_text)
{
	fflush(stdout);
	if (freopen(path.c_str(), "w", stdout) == nullptr)
	{
		fprintf(stderr, "stdout cannot be sent into %s\n", path.c_str());
		failed++;
		return -1;
	}
	std::vector<std::string> words = { "nulldc3", word };
	std::vector<char*> args;
	for (auto& w : words)
		args.push_back(&w[0]);
	int rv = ParseCommandLine((int)args.size(), args.data());
	fflush(stdout);
	stdout_text = read_text(path);
	return rv;
}

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		fprintf(stderr, "usage: version_cl <work directory>\n");
		return 2;
	}
	std::string work_dir = argv[1];
	std::filesystem::remove_all(work_dir);
	std::filesystem::create_directories(work_dir);

	const std::string version_line = std::string("nullDC ") + nulldc_version() + "\n";

	std::string text;
	int rv = parse("--version", work_dir + "/version.txt", text);
	CHECK(rv == rv_cli_finish);
	CHECK(text == version_line);
	CHECK(cl_FinishText() == version_line);
	CHECK(cl_ErrorText().empty());
	fprintf(stderr, "version.cl: --version: %d, stdout \"%s\", cl_FinishText() \"%s\"\n", rv, text.c_str(),
	        cl_FinishText().c_str());

	rv = parse("--help", work_dir + "/help.txt", text);
	CHECK(rv == rv_cli_finish);
	CHECK(text == cl_HelpText());
	CHECK(cl_FinishText() == cl_HelpText());
	CHECK(cl_FinishText() != version_line);
	CHECK(cl_HelpText().find("--version") != std::string::npos);
	fprintf(stderr, "version.cl: --help: %d, %zu characters on stdout, cl_FinishText() the usage text: %s\n", rv,
	        text.size(), cl_FinishText() == cl_HelpText() ? "yes" : "no");

	// a refused command line finishes nothing: the text of an earlier run is gone
	rv = parse("-version", work_dir + "/refused.txt", text);
	CHECK(rv == rv_cli_error);
	CHECK(text.empty());
	CHECK(cl_FinishText().empty());
	CHECK(!cl_ErrorText().empty());
	fprintf(stderr, "version.cl: -version: %d, cl_FinishText() \"%s\"\n", rv, cl_FinishText().c_str());

	if (failed)
	{
		fprintf(stderr, "version.cl: %d checks failed\n", failed);
		return 1;
	}
	fprintf(stderr, "version.cl: all checks passed\n");
	return 0;
}
