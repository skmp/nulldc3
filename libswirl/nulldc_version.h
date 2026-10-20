/*
	This file is part of libswirl
	BSD 3-clause, see license/bsd

	The version of nullDC (nulldc3/cmake/version.cmake makes it at every build; nulldc3/README.md says how), for
	every reader of it. The generated header with the values, nulldc3_version.h, is read by one translation unit,
	version.cpp, so that a new commit compiles that one again and nothing else. This header includes nothing: the
	projects that compile version.cpp without the emulator's definitions (tests/debugger/net) include it too.
*/
#pragma once

// The version: 3.0.0-pre+g<h> until a tag is reachable, 3.0.0-pre on the tag v3.0.0-pre (version.cpp)
const char* nulldc_version();

// The window title: VER_EMUNAME ("nullDC" on a Dreamcast build), a blank and nulldc_version() (version_title.cpp)
const char* nulldc_title();

// The commit the build was made from, git rev-parse HEAD; "" without git or outside a checkout (version.cpp)
const char* nulldc_git_hash();

// A tracked file differed from that commit when the build was made (version.cpp)
bool nulldc_git_dirty();
