/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once

// Either Desktop 3+ or GLES2/GLES3

#include "types.h"

#if HOST_OS == OS_DARWIN
	#include <OpenGL/gl3.h>
#else
	#include "khronos/GLES32/gl32.h"
	#include "khronos/GLES32/gl2ext.h"

	#include "gl3w.h"
#endif
