#include "ss_format.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>

static std::string format(const char* fmt, u32 a, u32 b = 0, u32 c = 0)
{
	char text[256];

	snprintf(text, sizeof(text), fmt, a, b, c);
	return text;
}

static bool check_record(const SsTest& t, u32 index, std::string& error)
{
#define FRAME(what, value, expected) \
	if ((u32)(value) != (u32)(expected)) \
	{ \
		error = format("record %u: " what " is %u, expected %u", index, (u32)(value), (u32)(expected)); \
		return false; \
	}

	FRAME("record size", t.size, sizeof(SsTest));
	FRAME("initial state size", t.initial.size, sizeof(SsStateBlock));
	FRAME("initial state tag", t.initial.tag, 1);
	FRAME("final state size", t.final_.size, sizeof(SsStateBlock));
	FRAME("final state tag", t.final_.tag, 2);
	FRAME("cycles size", t.cycles.size, sizeof(SsCyclesBlock));
	FRAME("cycles tag", t.cycles.tag, 3);
	FRAME("cycle count", t.cycles.count, 4);
	FRAME("opcodes size", t.opcodes.size, sizeof(SsOpcodesBlock));
	FRAME("opcodes tag", t.opcodes.tag, 4);

#undef FRAME

	return true;
}

bool ss_load_file(const std::string& path, std::vector<SsTest>& tests, std::string& error)
{
	tests.clear();

	FILE* f = fopen(path.c_str(), "rb");

	if (!f)
	{
		error = "cannot open " + path;
		return false;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || (size % sizeof(SsTest)) != 0)
	{
		fclose(f);
		error = format("file size %u is not a multiple of the record size %u", (u32)size, (u32)sizeof(SsTest));
		return false;
	}

	tests.resize(size / sizeof(SsTest));

	size_t got = fread(&tests[0], sizeof(SsTest), tests.size(), f);
	fclose(f);

	if (got != tests.size())
	{
		error = "short read on " + path;
		tests.clear();
		return false;
	}

	for (size_t i = 0; i < tests.size(); i++)
	{
		if (!check_record(tests[i], (u32)i, error))
		{
			tests.clear();
			return false;
		}
	}

	return true;
}

bool ss_list_files(const std::string& dir, std::vector<std::string>& names)
{
	names.clear();

	DIR* d = opendir(dir.c_str());

	if (!d)
		return false;

	const size_t suffix = strlen(SS_FILE_SUFFIX);

	while (dirent* e = readdir(d))
	{
		std::string name = e->d_name;

		if (name.size() > suffix && name.compare(name.size() - suffix, suffix, SS_FILE_SUFFIX) == 0)
			names.push_back(name.substr(0, name.size() - suffix));
	}

	closedir(d);

	std::sort(names.begin(), names.end());

	return true;
}

const char* ss_state_word_name(u32 index)
{
	static const char* const names[SS_STATE_WORDS] =
	{
		"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
		"R0_BANK", "R1_BANK", "R2_BANK", "R3_BANK", "R4_BANK", "R5_BANK", "R6_BANK", "R7_BANK",
		"FR0", "FR1", "FR2", "FR3", "FR4", "FR5", "FR6", "FR7", "FR8", "FR9", "FR10", "FR11", "FR12", "FR13", "FR14", "FR15",
		"XF0", "XF1", "XF2", "XF3", "XF4", "XF5", "XF6", "XF7", "XF8", "XF9", "XF10", "XF11", "XF12", "XF13", "XF14", "XF15",
		"PC", "GBR", "SR", "SSR", "SPC", "VBR", "SGR", "DBR", "MACL", "MACH", "PR", "FPSCR", "FPUL",
	};

	return index < SS_STATE_WORDS ? names[index] : "?";
}
