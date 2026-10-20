/*
	This file is part of libswirl
	BSD 3-clause, see license/bsd

	The window title, VER_SHORTNAME and VER_FULLNAME of types.h. VER_EMUNAME depends on the platform of the build,
	so this is apart from version.cpp, and it never reads the generated header: a new commit does not compile it.
*/
#include "types.h"

#include <string>

const char* nulldc_title()
{
	static const std::string title = std::string(VER_EMUNAME) + " " + nulldc_version();
	return title.c_str();
}
