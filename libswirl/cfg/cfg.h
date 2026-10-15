/*
	This file is part of libswirl
*/
#include "license/bsd"

#pragma once

#include "types.h"
/*
**	cfg* prototypes, if you pass NULL to a cfgSave* it will wipe out the section
**	} if you pass it to lpKey it will wipe out that particular entry
**	} if you add write to something it will create it if its not present
**	} ** Strings passed to LoadStr should be MAX_PATH in size ! **
*/

bool cfgOpen();
s32   cfgLoadInt(const wchar * lpSection, const wchar * lpKey,s32 Default);
void  cfgSaveInt(const wchar * lpSection, const wchar * lpKey, s32 Int);
void  cfgLoadStr(const wchar * lpSection, const wchar * lpKey, wchar * lpReturn,const wchar* lpDefault);
string  cfgLoadStr(const wchar * Section, const wchar * Key, const wchar* Default);
void  cfgSaveStr(const wchar * lpSection, const wchar * lpKey, const wchar * lpString);
void  cfgSaveBool(const wchar * Section, const wchar * Key, bool BoolValue);
bool  cfgLoadBool(const wchar * Section, const wchar * Key,bool Default);
s32  cfgExists(const wchar * Section, const wchar * Key);
void cfgSetVirtual(const wchar * lpSection, const wchar * lpKey, const wchar * lpString);
void cfgSetReadOnly();

// The command line (cfg/cl.cpp). The operating system layer hands it the directories a run can keep its files in
// (cl_SetHostDirs) before reicast_init(); ParseCommandLine() reads the switches and the game, and chooses one:
// the directory of the executable (portable, the default), the one of --portable <dir>, or with --shared the
// per-user ones. Each of them has the same layout, README.md has it.
struct HostDirs
{
	// the directory of the executable, from the operating system (never the working directory); empty when it
	// is not known, exe_dir_error then says why
	string exe_dir;
	string exe_dir_error;

	// --shared: the per-user directories, made when they are missing, and the read-only ones behind them;
	// shared_config empty when there are none, shared_error then says why
	string shared_config;
	string shared_data;
	vector<string> shared_config_ro;
	vector<string> shared_data_ro;
	string shared_error;

	// read behind the chosen directory in every mode (macOS: the resources of the application bundle)
	vector<string> data_ro;
};
void cl_SetHostDirs(const HostDirs& dirs);

// rv_ok: the run goes on. rv_cli_finish: --help was answered. rv_cli_error: the command line, or the directory it
// chose, cannot be used; the message is on stderr and in cl_ErrorText(), nothing else was done.
int ParseCommandLine(int argc,wchar* argv[]);
const string& cl_ErrorText();

void cfgSetGameId(const char *id);
const char *cfgGetGameId();
bool cfgHasGameSpecificConfig();
void cfgMakeGameSpecificConfig();
void cfgDeleteGameSpecificConfig();
