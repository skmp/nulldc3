/*
	This file is part of libswirl

	The command line: the switches, the game, and the directory the emulator keeps its files in.

	A switch is written with two dashes. Every word that begins with a dash is a switch, up to a "--", behind which
	every word is a game: a one-dash word is an error that names the two-dash form, an unknown switch is an error,
	neither is ever taken for a game. Nothing is done before the whole command line is read: an error leaves the
	directories and the configuration as they were.

	The directory: the one of the executable (portable, the default), the one --portable names, or with --shared
	the per-user ones of the operating system. The operating system layer says which those are (cl_SetHostDirs);
	this file makes the one that is chosen, checks that it can be written, and hands it to stdclass.cpp. A
	directory that cannot be written is an error, never a silent change of place.
*/
#include "license/bsd"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cfg/cfg.h"
#include "stdclass.h"

#if HOST_OS == OS_WINDOWS
	#include <io.h>
	#include <direct.h>
	#include <process.h>
#else
	#include <unistd.h>
#endif

// The blanks around str cut off, in place; 0 for nothing but blanks (ini.cpp reads emu.cfg with it)
wchar* trim_ws(wchar* str)
{
	if (str==0 || strlen(str)==0)
		return 0;

	while(*str)
	{
		if (!isspace(*str))
			break;
		str++;
	}

	size_t l=strlen(str);

	if (l==0)
		return 0;

	while(l>0)
	{
		if (!isspace(str[l-1]))
			break;
		str[l-1]=0;
		l--;
	}

	if (l==0)
		return 0;

	return str;
}

namespace {

HostDirs host_dirs;
string error_text;

// What the command line asks for, in its order: a later value of a key wins over an earlier one, and a game sets
// keys too
struct Item
{
	enum Kind { Config, Image, Program, NoDisk, Unknown } kind;
	string section, key, value;	// Config
	string word;			// the others
};

struct CommandLine
{
	bool help = false;
	bool portable = false;
	string portable_dir;		// as given
	bool shared = false;
	bool log = false;		// Windows: --log
	vector<Item> items;
};

// The switches of this build, without their dashes
const char* const switches[] = {
	"config", "portable", "shared", "help",
#if HOST_OS == OS_WINDOWS
	"console", "log",
#endif
};

bool is_switch(const string& name)
{
	for (const char* s : switches)
	{
		if (name == s)
			return true;
	}
	return false;
}

bool fail(const string& message)
{
	error_text = "nulldc3: " + message;
	return false;
}

string trim(const string& text)
{
	size_t first = 0, last = text.size();
	while (first < last && isspace((unsigned char)text[first]))
		first++;
	while (last > first && isspace((unsigned char)text[last - 1]))
		last--;
	return text.substr(first, last - first);
}

// section:key=value, blanks around the three parts are dropped, the value may be empty
bool config_item(const string& word, Item& item)
{
	if (!word.empty() && word[0] == '-')
		return fail("--config needs section:key=value in front of " + word);
	size_t colon = word.find(':');
	size_t equals = colon == string::npos ? string::npos : word.find('=', colon + 1);
	if (equals == string::npos)
		return fail("--config: '" + word + "' is not section:key=value");
	item.kind = Item::Config;
	item.section = trim(word.substr(0, colon));
	item.key = trim(word.substr(colon + 1, equals - colon - 1));
	item.value = trim(word.substr(equals + 1));
	if (item.section.empty() || item.key.empty())
		return fail("--config: '" + word + "' is not section:key=value");
	return true;
}

// A game, as the command line has always taken one: by the extension of its name
Item game(const string& word)
{
	Item item;
	item.word = word;
	size_t dot = word.find_last_of('.');
	string extension = dot == string::npos ? "" : word.substr(dot);
	for (auto& c : extension)
		c = tolower((unsigned char)c);

	if (extension == ".cdi" || extension == ".chd" || extension == ".gdi" || extension == ".lst"
		|| extension == ".cue" || extension == ".iso")
		item.kind = Item::Image;
	else if (extension == ".elf" || extension == ".bin")
		item.kind = Item::Program;
	else if (stricmp(word.c_str(), "nodisk") == 0)
		item.kind = Item::NoDisk;
	else
		item.kind = Item::Unknown;
	return item;
}

bool parse(int argc, wchar* argv[], CommandLine& cl)
{
	bool switches_end = false;
	for (int i = 1; i < argc; i++)
	{
		string word = argv[i];
		if (switches_end || word.empty() || word[0] != '-')
		{
			cl.items.push_back(game(word));
			continue;
		}
		if (word == "--")
		{
			switches_end = true;
			continue;
		}
		if (word.compare(0, 2, "--") != 0)
		{
			if (is_switch(word.substr(1)))
				return fail(word + " is written with two dashes: -" + word);
			return fail("unknown switch " + word + " (switches have two dashes; --help lists them)");
		}

		string name = word.substr(2);
		if (name == "help")
		{
			// what follows is not looked at
			cl.help = true;
			return true;
		}
		else if (name == "config")
		{
			if (i + 1 >= argc)
				return fail("--config needs section:key=value [ , section:key=value ]...");
			for (;;)
			{
				Item item;
				if (!config_item(argv[++i], item))
					return false;
				cl.items.push_back(item);
				if (i + 1 < argc && strcmp(argv[i + 1], ",") == 0)
				{
					i++;
					if (i + 1 >= argc)
						return fail("--config: nothing after the last ','");
					continue;
				}
				break;
			}
		}
		else if (name == "portable")
		{
			if (cl.portable)
				return fail("--portable is given twice");
			if (i + 1 >= argc)
				return fail("--portable needs a directory: --portable <dir>");
			string dir = argv[++i];
			if (dir.empty())
				return fail("--portable needs a directory: --portable <dir>, the word after it is empty");
			if (dir[0] == '-')
				return fail("--portable needs a directory: --portable <dir>, not " + dir
					+ " (a directory whose name begins with a dash is written ./" + dir + ")");
			cl.portable = true;
			cl.portable_dir = dir;
		}
		else if (name == "shared")
		{
			if (cl.shared)
				return fail("--shared is given twice");
			cl.shared = true;
		}
#if HOST_OS == OS_WINDOWS
		else if (name == "console")
		{
			// winmain.cpp has opened the console already, so that it shows what comes from here too
		}
		else if (name == "log")
		{
			if (cl.log)
				return fail("--log is given twice");
			cl.log = true;
		}
#else
		else if (name == "console" || name == "log")
			return fail(word + " is a switch of the Windows build only");
#endif
		else
			return fail("unknown switch " + word + " (--help lists the switches)");
	}

	if (cl.portable && cl.shared)
		return fail("--portable <dir> and --shared cannot be given together");
	return true;
}

bool is_separator(char c)
{
#if HOST_OS == OS_WINDOWS
	return c == '/' || c == '\\';
#else
	return c == '/';
#endif
}

int stat_of(const string& path, struct stat* st)
{
	return stat(path.c_str(), st);
}

bool is_dir(const string& path)
{
	struct stat st;
	return stat_of(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

bool exists(const string& path)
{
	struct stat st;
	return stat_of(path, &st) == 0;
}

// The path from the working directory of the start, without the separators at its end
string absolute(const string& path)
{
	string result;
#if HOST_OS == OS_WINDOWS
	char* full = _fullpath(nullptr, path.c_str(), 0);
	result = full ? full : path;
	free(full);
#else
	if (!path.empty() && path[0] == '/')
		result = path;
	else
	{
		vector<char> cwd(4096);
		while (getcwd(cwd.data(), cwd.size()) == nullptr && errno == ERANGE)
			cwd.resize(cwd.size() * 2);
		result = string(cwd.data()) + "/" + path;
	}
#endif
	while (result.size() > 1 && is_separator(result.back()) && !(result.size() == 3 && result[1] == ':'))
		result.pop_back();
	return result;
}

int make_one(const string& path)
{
#if HOST_OS == OS_WINDOWS
	return _mkdir(path.c_str());
#else
	return mkdir(path.c_str(), 0755);
#endif
}

// mkdir -p; false with the reason in why
bool make_dirs(const string& path, string& why)
{
	for (size_t i = 1; i <= path.size(); i++)
	{
		if (i < path.size() && !is_separator(path[i]))
			continue;
		string part = path.substr(0, i);
		if (part.empty() || is_separator(part.back()) || (part.size() == 2 && part[1] == ':') || is_dir(part))
			continue;
		if (make_one(part) != 0 && !is_dir(part))
		{
			why = part + ": " + strerror(errno);
			return false;
		}
	}
	if (!is_dir(path))
	{
		why = path + " is not a directory";
		return false;
	}
	return true;
}

// A file is made in the directory and removed again; false with the reason in why
bool writable(const string& dir, string& why)
{
#if HOST_OS == OS_WINDOWS
	long pid = (long)_getpid();
#else
	long pid = (long)getpid();
#endif
	for (unsigned attempt = 0; attempt < 16; attempt++)
	{
		string probe = dir + "/.nulldc3-write-test." + std::to_string(pid) + "." + std::to_string(attempt);
#if HOST_OS == OS_WINDOWS
		int fd = _open(probe.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
#else
		int fd = open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
		if (fd >= 0)
		{
#if HOST_OS == OS_WINDOWS
			_close(fd);
			_unlink(probe.c_str());
#else
			close(fd);
			unlink(probe.c_str());
#endif
			return true;
		}
		if (errno != EEXIST)
		{
			why = strerror(errno);
			return false;
		}
	}
	why = "no name for a file of its own";
	return false;
}

const char* const choose_another =
	"name another directory with --portable <dir>, or use the per-user directories with --shared";

bool choose_dirs(const CommandLine& cl)
{
	string why;
	if (cl.shared)
	{
		if (host_dirs.shared_config.empty())
			return fail("--shared: there are no per-user directories (" + host_dirs.shared_error + ")");
		for (const string* dir : { &host_dirs.shared_config, &host_dirs.shared_data })
		{
			if (!make_dirs(*dir, why))
				return fail("--shared: the directory " + *dir + " cannot be made (" + why + ")");
			if (!writable(*dir, why))
				return fail("--shared: the directory " + *dir + " cannot be written (" + why + ")");
		}
		clear_dirs();
		set_user_config_dir(host_dirs.shared_config);
		set_user_data_dir(host_dirs.shared_data);
		for (const auto& dir : host_dirs.shared_config_ro)
			add_system_config_dir(dir);
		for (const auto& dir : host_dirs.shared_data_ro)
			add_system_data_dir(dir);
	}
	else
	{
		string dir;
		if (cl.portable)
		{
			dir = absolute(cl.portable_dir);
			if (exists(dir) && !is_dir(dir))
				return fail("--portable " + cl.portable_dir + ": " + dir + " is not a directory");
			if (!make_dirs(dir, why))
				return fail("--portable " + cl.portable_dir + ": the directory cannot be made (" + why + ")");
			if (!writable(dir, why))
				return fail("--portable " + cl.portable_dir + ": " + dir + " cannot be written (" + why + "); "
					+ choose_another);
		}
		else
		{
			if (host_dirs.exe_dir.empty())
				return fail("the directory of the executable is not known (" + host_dirs.exe_dir_error + "); "
					+ choose_another);
			dir = host_dirs.exe_dir;
			if (!writable(dir, why))
				return fail("the directory of the executable, " + dir + ", cannot be written (" + why + "); "
					+ choose_another);
		}
		clear_dirs();
		set_user_config_dir(dir);
		set_user_data_dir(dir);
	}
	for (const auto& dir : host_dirs.data_ro)
		add_system_data_dir(dir);
	return true;
}

}

void cl_SetHostDirs(const HostDirs& dirs)
{
	host_dirs = dirs;
}

const string& cl_ErrorText()
{
	return error_text;
}

static int showhelp()
{
	printf("\nUsage: nulldc3 [switches] [game]\n\n");
	printf("  game                  a disc image (.gdi .chd .cdi .cue .iso .lst), a program that runs under reios\n");
	printf("                        (.elf .bin), or nodisk. Without one the emulator starts at its menu\n\n");
	printf("Switches:\n");
	printf("  --config section:key=value [ , section:key=value ]...\n");
	printf("                        values of the configuration for this run; they are not written to emu.cfg.\n");
	printf("                        The comma between two of them is a word of its own: blanks around it\n");
	printf("                        Keys for runs without a person, see docs/nulldc3/automated-runs.md:\n");
	printf("                          config:Headless=yes  config:Exit.AfterVblanks=N  config:Exit.AfterSeconds=S\n");
	printf("                          config:Exit.OnGuestExit=yes  config:Exit.StateHash=yes  config:Dreamcast.RTC=N\n");
	printf("  --portable <dir>      keep the configuration, BIOS and flash, VMUs, mappings and everything else the\n");
	printf("                        emulator writes in <dir> (made when it is missing), not in the directory of the\n");
	printf("                        executable\n");
	printf("  --shared              keep them in the per-user directories (nulldc3) of the system instead:\n");
#if HOST_OS == OS_WINDOWS
	printf("                        %%APPDATA%%\\nulldc3\n");
#elif HOST_OS == OS_DARWIN
	printf("                        ~/Library/Application Support/nulldc3\n");
#else
	printf("                        $XDG_CONFIG_HOME/nulldc3 (~/.config/nulldc3) and $XDG_DATA_HOME/nulldc3\n");
	printf("                        (~/.local/share/nulldc3), with the XDG system directories behind them\n");
#endif
#if HOST_OS == OS_WINDOWS
	printf("  --console             open a console window for the output of the emulator\n");
	printf("  --log                 write the output of the emulator to nulldc3-log.txt in that directory\n");
#endif
	printf("  --help                show this text\n");
	printf("  --                    every word after it is a game, also one that begins with a dash\n\n");
	printf("Without --portable and --shared the emulator keeps its files in the directory of its executable.\n");
	printf("Every word that begins with a dash is a switch: an unknown one, or one written with one dash, is an\n");
	printf("error.\n\n");
	return 0;
}

int ParseCommandLine(int argc,wchar* argv[])
{
	error_text.clear();
	cfgSetVirtual("config", "image", "");

	CommandLine cl;
	if (!parse(argc, argv, cl))
	{
		fprintf(stderr, "%s\n", error_text.c_str());
		return rv_cli_error;
	}
	if (cl.help)
	{
		showhelp();
		return rv_cli_finish;
	}
	if (!choose_dirs(cl))
	{
		fprintf(stderr, "%s\n", error_text.c_str());
		return rv_cli_error;
	}

#if HOST_OS == OS_WINDOWS
	if (cl.log)
	{
		string logfile = get_writable_config_path("/nulldc3-log.txt");
		freopen(logfile.c_str(), "w", stdout);
		freopen(logfile.c_str(), "w", stderr);
	}
#endif

	for (const auto& item : cl.items)
	{
		switch (item.kind)
		{
		case Item::Config:
			printf("Virtual cfg %s:%s=%s\n", item.section.c_str(), item.key.c_str(), item.value.c_str());
			cfgSetVirtual(item.section.c_str(), item.key.c_str(), item.value.c_str());
			break;

		case Item::Image:
			printf("Using '%s' as cd image\n", item.word.c_str());
			cfgSetVirtual("config", "image", item.word.c_str());
			break;

		case Item::Program:
			printf("Using '%s' as reios bin/elf file\n", item.word.c_str());
			cfgSetVirtual("config", "bios.UseReios", "1");
			cfgSetVirtual("reios", "ElfFile", item.word.c_str());
			cfgSetVirtual("config", "image", "nodisk");
			break;

		case Item::NoDisk:
			printf("Starting without cd image\n");
			cfgSetVirtual("config", "image", "nodisk");
			break;

		case Item::Unknown:
#if DC_PLATFORM == DC_PLATFORM_NAOMI || DC_PLATFORM == DC_PLATFORM_ATOMISWAVE
			printf("Using '%s' as rom\n", item.word.c_str());
			cfgSetVirtual("config", "image", item.word.c_str());
#else
			printf("'%s' is no disc image (.gdi .chd .cdi .cue .iso .lst), program (.elf .bin) or nodisk: left out\n",
				item.word.c_str());
#endif
			break;
		}
	}
	printf("\n");
	return rv_ok;
}
