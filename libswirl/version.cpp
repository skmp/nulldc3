/*
	This file is part of libswirl
	BSD 3-clause, see license/bsd

	The one translation unit that reads the generated version header (nulldc_version.h has why). It includes nothing
	else: the generated directory is on its include path alone.
*/
#include "nulldc_version.h"

#include "nulldc3_version.h"

const char* nulldc_version()
{
	return NULLDC_VERSION;
}

const char* nulldc_git_hash()
{
	return NULLDC_GIT_HASH;
}

bool nulldc_git_dirty()
{
	return NULLDC_GIT_DIRTY != 0;
}
