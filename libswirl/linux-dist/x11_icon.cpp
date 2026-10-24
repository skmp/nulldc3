/*
	This file is part of libswirl
*/
#include "license/bsd"

// The values of the X11 window's icon, _NET_WM_ICON, from the generated x11_icon.h (nulldc3/icon/make_icons.py).
// No X11 header here: the test icons.x11 calls x11_icon_cardinals() without a display or the X11 libraries.

#include "linux-dist/x11_icon.h"

std::vector<unsigned long> x11_icon_cardinals()
{
	// Xlib takes the data of a format 32 property as an array of C longs (8 bytes on 64-bit Linux) and sends the low
	// 32 bits of each. The header's uint32_t values are zero-extended into unsigned long: never through a signed
	// type, which would extend every pixel with an alpha of 128 or more with ones.
	return std::vector<unsigned long>(x11_icon::kValues, x11_icon::kValues + x11_icon::kValueCount);
}
