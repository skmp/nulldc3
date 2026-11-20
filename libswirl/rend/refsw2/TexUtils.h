/*
	This file is part of libswirl
*/
#include "license/bsd"

#include "types.h"

#include <algorithm>

extern u32 detwiddle[2][11][1024];
extern s8 BM_SIN90[256];
extern s8 BM_COS90[256];
extern s8 BM_COS360[256];

template<class pixel_type>
pixel_type cclamp(pixel_type minv, pixel_type maxv, pixel_type x) {
	return std::min(maxv, std::max(minv, x));
}

// Unpack to 32-bit word

#define ARGB1555_32( word )    ( ((word & 0x8000) ? 0xFF000000 : 0) | (((word>>0) & 0x1F)<<3)  | (((word>>5) & 0x1F)<<11)  | (((word>>10) & 0x1F)<<19) )

#define ARGB565_32( word )     ( (((word>>0)&0x1F)<<3) | (((word>>5)&0x3F)<<10) | (((word>>11)&0x1F)<<19) | 0xFF000000 )

#define ARGB4444_32( word ) ( (((word>>12)&0xF)<<28) | (((word>>0)&0xF)<<4) | (((word>>4)&0xF)<<12) | (((word>>8)&0xF)<<20) )

#define ARGB8888_32( word ) ( word )

static u32 packRGB(u8 R,u8 G,u8 B)
{
	return (B << 0) | (G << 8) | (R << 16) | 0xFF000000;   // Color is b, g, r, a, like the other formats here
}

static u32 YUV422(s32 Y,s32 Yu,s32 Yv)
{
	Yu-=128;
	Yv-=128;

	//s32 B = (76283*(Y - 16) + 132252*(Yu - 128))>>16;
	//s32 G = (76283*(Y - 16) - 53281 *(Yv - 128) - 25624*(Yu - 128))>>16;
	//s32 R = (76283*(Y - 16) + 104595*(Yv - 128))>>16;
	
	// Hardware multiplies by 11/8 as x + x/4 + x/8 with arithmetic shifts, and derives the other terms
	// from that by further shifts (fitted to a console, dreamcast-tests/pvr-tests/texfmt pages 1 and 2; refsw3-core).
	s32 Tu = Yu + (Yu >> 2) + (Yu >> 3);
	s32 Tv = Yv + (Yv >> 2) + (Yv >> 3);

	s32 R = Y + Tv;
	s32 G = Y - (Tu >> 2) - (Tv >> 1);
	s32 B = Y + Tu + (Tu >> 2);

	return packRGB(cclamp<s32>(0, 255, R),cclamp<s32>(0, 255, G),cclamp<s32>(0, 255, B));
}

#define twop(x,y,bcx,bcy) (detwiddle[0][bcy+3][x]+detwiddle[1][bcx+3][y])

#define twop2(x,y,bcx,bcy) (detwiddle[0][bcy][x]+detwiddle[1][bcx][y])
