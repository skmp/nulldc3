/*
	This file is part of libswirl
	
	Config file crap
	Supports various things, as virtual config entries and such crap
	Works surprisingly well considering how old it is ...
*/
#include "license/bsd"



#define _CRT_SECURE_NO_DEPRECATE (1)
#include <errno.h>
#include "cfg.h"
#include "ini.h"

static string cfgPath;
static bool save_config = true;

static emucfg::ConfigFile cfgdb;
static string game_id;
static bool has_game_specific_config = false;

void savecfgf()
{
	FILE* cfgfile = fopen(cfgPath.c_str(),"wt");
	if (!cfgfile)
	{
		printf("Error: Unable to open file '%s' for saving\n", cfgPath.c_str());
	}
	else
	{
		cfgdb.save(cfgfile);
		fclose(cfgfile);
	}
}
// A value given on the command line (cfgSetVirtual) belongs to this run only. SaveSettings writes every setting
// back: one that still has its command line value is not written, the file keeps what it has for it. Another
// value replaces the command line one, for the rest of the run and in the file.
// *same* is whether the value that is being saved is the one the command line gave.
static bool cfgKeepsVirtual(const std::string& section, const std::string& key, bool same)
{
	if (!cfgdb.has_virtual_entry(section, key))
		return false;
	if (same)
		return true;
	cfgdb.delete_virtual_entry(section, key);
	return false;
}

void  cfgSaveStr(const wchar * Section, const wchar * Key, const wchar * String)
{
	const std::string section(Section);
	const std::string key(Key);
	const std::string value(String);
	if (cfgKeepsVirtual(section, key, cfgdb.get(section, key, "") == value))
		return;
	if (cfgHasGameSpecificConfig())
	{
		if (cfgdb.get(section, key, "") == value)
			// Same value as main config: delete entry
			cfgdb.delete_entry(game_id, key);
		else
			cfgdb.set(game_id, key, value);
	}
	else
		cfgdb.set(section, key, value);
	if(save_config)
	{
		savecfgf();
	}
}
//New config code

/*
	I want config to be really flexible .. so , here is the new implementation :

	Functions :
	cfgLoadInt  : Load an int , if it does not exist save the default value to it and return it
	cfgSaveInt  : Save an int
	cfgLoadStr  : Load a str , if it does not exist save the default value to it and return it
	cfgSaveStr  : Save a str
	cfgExists   : Returns true if the Section:Key exists. If Key is null , it retuns true if Section exists

	Config parameters can be read from the config file , and can be given at the command line
	-cfg section:key=value -> defines a value at command line
	If a cfgSave* is made on a value defined by command line with another value, then the command line value is
	replaced by it. A cfgSave* of the value the command line gave is not written (cfgKeepsVirtual)

	cfg values set by command line are not written to the cfg file , unless a cfgSave* changes them
*/

///////////////////////////////
/*
**	This will verify there is a working file @ ./szIniFn
**	- if not present, it will write defaults
*/

bool cfgOpen()
{
	if (get_writable_config_path("").empty())
		// Config dir not set (android onboarding)
		return false;

	const char* filename = "/emu.cfg";
	string config_path_read = get_readonly_config_path(filename);
	cfgPath = get_writable_config_path(filename);

	FILE* cfgfile = fopen(config_path_read.c_str(),"r");
	if(cfgfile != NULL) {
		cfgdb.parse(cfgfile);
		fclose(cfgfile);
	}
	else
	{
		// Config file can't be opened
		int error_code = errno;
		printf("Warning: Unable to open the config file '%s' for reading (%s)\n", config_path_read.c_str(), strerror(error_code));
		if (error_code == ENOENT || cfgPath != config_path_read)
		{
			// Config file didn't exist
			printf("Creating new empty config file at '%s'\n", cfgPath.c_str());
			savecfgf();
		}
		else
		{
			// There was some other error (may be a permissions problem or something like that)
			save_config = false;
		}
	}

	return true;
}

//Implementations of the interface :)
//Section must be set
//If key is 0 , it looks for the section
//0 : not found
//1 : found section , key was 0
//2 : found section & key
s32  cfgExists(const wchar * Section, const wchar * Key)
{
	if(cfgdb.has_entry(string(Section), string(Key)))
	{
		return 2;
	}
	else
	{
		return (cfgdb.has_section(string(Section)) ? 1 : 0);
	}
}

void  cfgLoadStr(const wchar * Section, const wchar * Key, wchar * Return,const wchar* Default)
{
	string value = cfgdb.get(Section, Key, Default);
	// FIXME: Buffer overflow possible
	strcpy(Return, value.c_str());
}

string  cfgLoadStr(const wchar * Section, const wchar * Key, const wchar* Default)
{
	std::string v = cfgdb.get(string(Section), string(Key), string(Default));
	if (cfgHasGameSpecificConfig())
		v = cfgdb.get(game_id, string(Key), v);

	return v;
}

//These are helpers , mainly :)
void  cfgSaveInt(const wchar * Section, const wchar * Key, s32 Int)
{
	// compared as a number: the command line may have it in another spelling (0x10, 016)
	if (cfgKeepsVirtual(Section, Key, cfgdb.get_int(Section, Key, 0) == Int))
		return;
	char str[32];
	sprintf(str, "%d", Int);
	cfgSaveStr(Section, Key, str);
}

s32 cfgLoadInt(const wchar * Section, const wchar * Key,s32 Default)
{
	s32 v = cfgdb.get_int(string(Section), string(Key), Default);
	if (cfgHasGameSpecificConfig())
		v = cfgdb.get_int(game_id, string(Key), v);

    return v;
}

void  cfgSaveBool(const wchar * Section, const wchar * Key, bool BoolValue)
{
	// compared as a bool: the command line may have it as 1, true or on
	if (cfgKeepsVirtual(Section, Key, cfgdb.get_bool(Section, Key, false) == BoolValue))
		return;
	cfgSaveStr(Section, Key, BoolValue ? "yes" : "no");
}

bool  cfgLoadBool(const wchar * Section, const wchar * Key,bool Default)
{
	bool v = cfgdb.get_bool(string(Section), string(Key), Default);
	if (cfgHasGameSpecificConfig())
		v = cfgdb.get_bool(game_id, string(Key), v);

    return v;
}

void cfgSetVirtual(const wchar * Section, const wchar * Key, const wchar * String)
{
	cfgdb.set(string(Section), string(Key), string(String), true);
}

// From here on the config file is left as it is: what a cfgSave* sets is there for the run and never written
void cfgSetReadOnly()
{
	save_config = false;
}

void cfgSetGameId(const char *id)
{
	game_id = id;
}

const char *cfgGetGameId()
{
	return game_id.c_str();
}

bool cfgHasGameSpecificConfig()
{
	return has_game_specific_config || cfgdb.has_section(game_id);
}

void cfgMakeGameSpecificConfig()
{
	has_game_specific_config = true;
}

void cfgDeleteGameSpecificConfig()
{
	has_game_specific_config = false;
	cfgdb.delete_section(game_id);
}
