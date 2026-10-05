// Stub: the device tree tables, implemented by package E3.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_regs.h"

// The stub has no nodes.
namespace dbg { namespace regs {

const Node* Nodes(uint32_t* count)
{
	*count = 0;
	return nullptr;
}

const Node* Find(const char*) { return nullptr; }
int FindRegister(const Node&, const char*) { return -1; }

void FormatValue(uint32_t width, uint64_t value, char* out)
{
	static const char digits[] = "0123456789ABCDEF";

	if (width > 64)
		width = 64;
	if (width < 64)
		value &= ((uint64_t)1 << width) - 1;

	uint32_t count = (width + 3) / 4;

	*out++ = '0';
	*out++ = 'x';
	for (uint32_t i = count; i-- > 0; )
		*out++ = digits[(value >> (i * 4)) & 15];
	*out = 0;
}

} }
