/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "sh4_if.h"
#include <string.h>


#define r Sh4cntx.r
#define r_bank Sh4cntx.r_bank
#define gbr Sh4cntx.gbr
#define ssr Sh4cntx.ssr
#define spc Sh4cntx.spc
#define sgr Sh4cntx.sgr
#define dbr Sh4cntx.dbr
#define vbr Sh4cntx.vbr
#define mac Sh4cntx.mac
#define pr Sh4cntx._pr
#define fpul Sh4cntx.fpul
#define next_pc Sh4cntx.pc
#define curr_pc (next_pc-2)
#define sr Sh4cntx.sr
#define fpscr Sh4cntx.fpscr
#define old_sr Sh4cntx.old_sr
#define old_fpscr Sh4cntx.old_fpscr
#define fr (&Sh4cntx.xffr[16])
#define xf Sh4cntx.xffr
#define fr_hex ((u32*)fr)
#define xf_hex ((u32*)xf)
#define dr_hex ((u64*)fr)
#define xd_hex ((u64*)xf)
#define sh4_int_bCpuRun Sh4cntx.CpuRunning



void UpdateFPSCR();
bool UpdateSR();

union DoubleReg
{
	f64 dbl;
	f32 sgl[2];
};

static INLINE f64 GetDR(u32 n)
{
#ifdef TRACE
	if (n>7)
		printf("DR_r INDEX OVERRUN %d >7",n);
#endif
	DoubleReg t;

	t.sgl[1]=fr[(n<<1) + 0];
	t.sgl[0]=fr[(n<<1) + 1];

	return t.dbl;
}

static INLINE f64 GetXD(u32 n)
{
#ifdef TRACE
	if (n>7)
		printf("XD_r INDEX OVERRUN %d >7",n);
#endif
	DoubleReg t;

	t.sgl[1]=xf[(n<<1) + 0];
	t.sgl[0]=xf[(n<<1) + 1];

	return t.dbl;
}

static INLINE void SetDR(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		printf("DR_w INDEX OVERRUN %d >7",n);
#endif
	DoubleReg t;
	t.dbl=val;


	fr[(n<<1) | 1]=t.sgl[0];
	fr[(n<<1) | 0]=t.sgl[1];
}

static INLINE void SetXD(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		printf("XD_w INDEX OVERRUN %d >7",n);
#endif

	DoubleReg t;
	t.dbl=val;

	xf[(n<<1) | 1]=t.sgl[0];
	xf[(n<<1) | 0]=t.sgl[1];
}
//needs to be removed
u32* Sh4_int_GetRegisterPtr(Sh4RegType reg);
//needs to be made portable
void SetFloatStatusReg();


bool Do_Interrupt(u32 intEvn);
bool Do_Exception(u32 epc, u32 expEvn, u32 CallVect);

struct SH4ThrownException {
	u32 epc;
	u32 expEvn;
	u32 callVect;
};

static INLINE void RaiseFPUDisableException()
{
#if !defined(NO_MMU)
	if (settings.dreamcast.FullMMU)
	{
		SH4ThrownException ex = { next_pc - 2, 0x800, 0x100 };
		throw ex;
	}
#else
	msgboxf("Full MMU support needed", MBX_ICONERROR);
#endif
}

// SH7091: every NaN result is 0x7FBFFFFF, in double precision 0x7FF7FFFF FFFFFFFF (programming manual 6.2.2)
#define SH4_QNAN32 0x7FBFFFFF
#define SH4_QNAN64 0x7FF7FFFFFFFFFFFFull

// What fixNaN writes. For the host both are signalling NaNs, and MSVC folds such bits, copied into a float,
// into a float constant made quiet (0x7FFFFFFF, 0x7FFFFFFF FFFFFFFF): there the bits are read at run time
#if BUILD_COMPILER == COMPILER_VC
static const volatile u32 sh4_qnan32_bits = SH4_QNAN32;
static const volatile u64 sh4_qnan64_bits = SH4_QNAN64;
#define SH4_QNAN32_BITS sh4_qnan32_bits
#define SH4_QNAN64_BITS sh4_qnan64_bits
#else
#define SH4_QNAN32_BITS SH4_QNAN32
#define SH4_QNAN64_BITS SH4_QNAN64
#endif

static INLINE f32 fixNaN(f32 f)
{
	u32 hex;
	memcpy(&hex, &f, sizeof(hex));

	if ((hex & 0x7FFFFFFF) > 0x7F800000)
	{
		hex = SH4_QNAN32_BITS;
		memcpy(&f, &hex, sizeof(f));
	}

	return f;
}

static INLINE f64 fixNaN64(f64 f)
{
	u64 hex;
	memcpy(&hex, &f, sizeof(hex));

	if ((hex & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull)
	{
		hex = SH4_QNAN64_BITS;
		memcpy(&f, &hex, sizeof(f));
	}

	return f;
}

// FIPR and FTRV as the host computes them, hw/sh4/interpr/sh4_fpu.cpp. fd may be fn
f32 sh4_fipr(const f32* fn, const f32* fm);
void sh4_ftrv(f32* fd, const f32* fn, const f32* fm);

// SH7091 FTRC: 2^31 and above, +inf -> 0x7FFFFFFF; -2^31 and below, -inf, every NaN -> 0x80000000 (programming manual 10.46)
static INLINE u32 ftrc32(f32 f)
{
	u32 hex;
	memcpy(&hex, &f, sizeof(hex));

	u32 magnitude = hex & 0x7FFFFFFF;

	if (magnitude > 0x7F800000)
		return 0x80000000;

	if (magnitude >= 0x4F000000)
		return (hex >> 31) ? 0x80000000 : 0x7FFFFFFF;

	return (u32)(s32)f;
}

static INLINE u32 ftrc64(f64 f)
{
	u64 hex;
	memcpy(&hex, &f, sizeof(hex));

	u64 magnitude = hex & 0x7FFFFFFFFFFFFFFFull;

	if (magnitude > 0x7FF0000000000000ull)
		return 0x80000000;

	if (magnitude >= 0x41E0000000000000ull)
		return (hex >> 63) ? 0x80000000 : 0x7FFFFFFF;

	return (u32)(s32)f;
}
