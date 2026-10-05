/*
	cfg.overrides: the config store (cfg/cfg.cpp, cfg/ini.cpp) and the command line parser (cfg/cl.cpp) on a
	config file of their own.

	    cfg_overrides <work directory>

	SaveSettings writes every setting back with cfgSave*. The checks are on what that leaves in emu.cfg when
	some of the settings came from -config.
*/
#include "types.h"
#include "cfg/cfg.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// What cfg.cpp and cl.cpp take from stdclass.cpp: the config directory is the work directory of the test
static std::string work_dir;

string get_writable_config_path(const string& filename) { return work_dir + filename; }
string get_readonly_config_path(const string& filename) { return work_dir + filename; }
void set_user_config_dir(const string& dir) { }
void set_user_data_dir(const string& dir) { }
void add_system_config_dir(const string& dir) { }
void add_system_data_dir(const string& dir) { }
void clear_dirs() { }

static int failed = 0;

#define CHECK(condition) \
	do { if (!(condition)) { printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); failed++; } } while (0)

// emu.cfg as it is on disk: "section:key" -> value
static std::map<std::string, std::string> read_file()
{
	std::map<std::string, std::string> entries;
	std::ifstream file(work_dir + "/emu.cfg");
	std::string line, section;

	while (std::getline(file, line))
	{
		if (line.size() > 2 && line[0] == '[')
			section = line.substr(1, line.size() - 2);
		else if (line.find(" = ") != std::string::npos)
			entries[section + ":" + line.substr(0, line.find(" = "))] = line.substr(line.find(" = ") + 3);
	}

	return entries;
}

static std::string read_text()
{
	std::ifstream file(work_dir + "/emu.cfg");
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("usage: cfg_overrides <work directory>\n");
		return 2;
	}

	work_dir = argv[1];
	std::filesystem::create_directories(work_dir);

	{
		std::ofstream file(work_dir + "/emu.cfg");
		file << "[config]\n"
		        "Dynarec.Enabled = yes\n"
		        "pvr.backend = refsw2\n"
		        "Dreamcast.Cable = 3\n"
		        "rend.ShowFPS = no\n"
		        "\n"
		        "[x11]\n"
		        "width = 640\n"
		        "\n";
	}

	// the command line of a test run, as main gets it
	std::vector<std::string> words = {
		"emulator", "-config",
		"config:Dynarec.Enabled=0", ",",			// a boolean, not spelled the way a save spells it
		"config:pvr.backend=none", ",",
		"config:Debug.SerialConsoleEnabled=true", ",",	// not in the file at all
		"x11:width=0x140", ",",					// a number, in hex
		"config:rend.ShowFPS=yes",
	};
	std::vector<char*> args;
	for (auto& word : words)
		args.push_back(&word[0]);

	CHECK(!ParseCommandLine((int)args.size(), args.data()));
	CHECK(cfgOpen());

	// the command line is what the run sees
	CHECK(cfgLoadBool("config", "Dynarec.Enabled", true) == false);
	CHECK(cfgLoadStr("config", "pvr.backend", "auto") == "none");
	CHECK(cfgLoadBool("config", "Debug.SerialConsoleEnabled", false) == true);
	CHECK(cfgLoadInt("x11", "width", 0) == 320);
	CHECK(cfgLoadBool("config", "rend.ShowFPS", false) == true);
	CHECK(cfgLoadInt("config", "Dreamcast.Cable", 0) == 3);

	// SaveSettings: every setting is saved with the value it has
	cfgSaveBool("config", "Dynarec.Enabled", false);
	cfgSaveStr("config", "pvr.backend", "none");
	cfgSaveBool("config", "Debug.SerialConsoleEnabled", true);
	cfgSaveInt("x11", "width", 320);
	cfgSaveInt("config", "Dreamcast.Cable", 3);
	cfgSaveBool("config", "aica.LimitFPS", true);		// one that is neither in the file nor on the command line
	// ... and one that the user changed in the settings while the run had a command line value for it
	cfgSaveBool("config", "rend.ShowFPS", false);

	auto file = read_file();

	// the file keeps what it had for the values of the command line, and gets no entry it did not have
	CHECK(file["config:Dynarec.Enabled"] == "yes");
	CHECK(file["config:pvr.backend"] == "refsw2");
	CHECK(file.count("config:Debug.SerialConsoleEnabled") == 0);
	CHECK(file["x11:width"] == "640");
	// what did not come from the command line is saved as ever
	CHECK(file["config:Dreamcast.Cable"] == "3");
	CHECK(file["config:aica.LimitFPS"] == "yes");
	// the changed one is saved, and is what the run sees from then on
	CHECK(file["config:rend.ShowFPS"] == "no");
	CHECK(cfgLoadBool("config", "rend.ShowFPS", true) == false);

	// the run still sees its command line
	CHECK(cfgLoadBool("config", "Dynarec.Enabled", true) == false);
	CHECK(cfgLoadStr("config", "pvr.backend", "auto") == "none");
	CHECK(cfgLoadInt("x11", "width", 0) == 320);

	// a second save of everything changes nothing
	std::string before = read_text();
	cfgSaveBool("config", "Dynarec.Enabled", false);
	cfgSaveStr("config", "pvr.backend", "none");
	cfgSaveBool("config", "Debug.SerialConsoleEnabled", true);
	cfgSaveInt("x11", "width", 320);
	cfgSaveBool("config", "rend.ShowFPS", false);
	CHECK(read_text() == before);

	// a new value for a setting of the command line replaces it, in the file too
	cfgSaveStr("config", "pvr.backend", "gles");
	CHECK(read_file()["config:pvr.backend"] == "gles");
	CHECK(cfgLoadStr("config", "pvr.backend", "auto") == "gles");

	// read-only: saves are there for the run, the file stays
	before = read_text();
	cfgSetReadOnly();
	cfgSaveInt("config", "Dreamcast.Cable", 0);
	cfgSaveStr("config", "pvr.backend", "refmister");
	cfgSaveBool("config", "Dynarec.Enabled", true);
	CHECK(read_text() == before);
	CHECK(cfgLoadInt("config", "Dreamcast.Cable", 3) == 0);
	CHECK(cfgLoadStr("config", "pvr.backend", "auto") == "refmister");

	if (failed)
	{
		printf("cfg.overrides: FAIL (%d checks)\n", failed);
		return 1;
	}

	printf("cfg.overrides: PASS\n");
	return 0;
}
