#include "ss_state.h"

#include "hw/sh4/sh4_if.h"

#include <cmath>
#include <cstring>

//hw/sh4/sh4_core.h declares these, along with macros for r, sr, pr, ... that are better kept out of here
void SetFloatStatusReg();

extern u8 OIX_CACHE[256][32];
extern u32 OIX_ADDR[256];
extern bool OIX_DIRTY[256];

void ss_state_load(const SsState& s)
{
	Sh4Context& ctx = Sh4cntx;

	memset(&ctx, 0, sizeof(ctx));

	for (u32 i = 0; i < 16; i++)
	{
		ctx.r[i] = s.R[i];
		memcpy(&ctx.xffr[16 + i], &s.FP0[i], 4);	//fr
		memcpy(&ctx.xffr[i], &s.FP1[i], 4);			//xf
	}

	for (u32 i = 0; i < 8; i++)
		ctx.r_bank[i] = s.R_[i];

	ctx.pc = s.PC;
	ctx.gbr = s.GBR;
	ctx.ssr = s.SSR;
	ctx.spc = s.SPC;
	ctx.vbr = s.VBR;
	ctx.sgr = s.SGR;
	ctx.dbr = s.DBR;
	ctx.mac.l = s.MACL;
	ctx.mac.h = s.MACH;
	ctx._pr = s.PR;
	ctx.fpul = s.FPUL;

	//old_sr / old_fpscr are what UpdateSR / UpdateFPSCR compare against to swap banks
	sh4_sr_SetFull(s.SR);
	ctx.old_sr.status = ctx.sr.status;

	ctx.fpscr.full = s.FPSCR;
	ctx.old_fpscr = ctx.fpscr;
	SetFloatStatusReg();

	ctx.CpuRunning = 1;
	ctx.sh4_sched_next = 0x7FFFFFFF;

	memset(OIX_CACHE, 0, sizeof(OIX_CACHE));
	memset(OIX_ADDR, 0, sizeof(OIX_ADDR));
	memset(OIX_DIRTY, 0, sizeof(OIX_DIRTY));
}

void ss_state_save(SsState& s)
{
	const Sh4Context& ctx = Sh4cntx;

	for (u32 i = 0; i < 16; i++)
	{
		s.R[i] = ctx.r[i];
		memcpy(&s.FP0[i], &ctx.xffr[16 + i], 4);
		memcpy(&s.FP1[i], &ctx.xffr[i], 4);
	}

	for (u32 i = 0; i < 8; i++)
		s.R_[i] = ctx.r_bank[i];

	s.PC = ctx.pc;
	s.GBR = ctx.gbr;
	s.SR = sh4_sr_GetFull();
	s.SSR = ctx.ssr;
	s.SPC = ctx.spc;
	s.VBR = ctx.vbr;
	s.SGR = ctx.sgr;
	s.DBR = ctx.dbr;
	s.MACL = ctx.mac.l;
	s.MACH = ctx.mac.h;
	s.PR = ctx._pr;
	s.FPSCR = ctx.fpscr.full;
	s.FPUL = ctx.fpul;
}

bool ss_float_lenient(u32 mydata, u32 theirdata)
{
	if (mydata == theirdata)
		return true;

	float mine, theirs;

	memcpy(&mine, &mydata, 4);
	memcpy(&theirs, &theirdata, 4);

	if (mine == theirs)
		return true;

	if ((mine != mine) && (theirs != theirs))
		return true;

	u32 a = mydata - theirdata;

	if ((a < 5) || (a > 0xFFFFFFFD))
		return true;

	if (fabsf(theirs - mine) < 0.0000001f)
		return true;

	return false;
}

SsCompare ss_state_compare(const SsState& expected, const SsState& actual)
{
	SsCompare rv;

	rv.exact = true;
	rv.lenient = true;
	rv.first_exact = -1;
	rv.first_lenient = -1;

	const u32* e = (const u32*)&expected;
	const u32* a = (const u32*)&actual;

	for (u32 i = 0; i < SS_STATE_WORDS; i++)
	{
		if (e[i] == a[i])
			continue;

		if (rv.exact)
		{
			rv.exact = false;
			rv.first_exact = (int)i;
		}

		bool fp = i >= SS_STATE_FP_FIRST && i <= SS_STATE_FP_LAST;

		if (rv.lenient && !(fp && ss_float_lenient(a[i], e[i])))
		{
			rv.lenient = false;
			rv.first_lenient = (int)i;
		}
	}

	return rv;
}

void ss_state_print(FILE* out, const SsState& initial, const SsState& expected, const SsState& actual)
{
	const u32* i = (const u32*)&initial;
	const u32* e = (const u32*)&expected;
	const u32* a = (const u32*)&actual;

	fprintf(out, "    %-8s %-8s %-8s %-8s\n", "", "initial", "expected", "actual");

	for (u32 w = 0; w < SS_STATE_WORDS; w++)
	{
		bool fp = w >= SS_STATE_FP_FIRST && w <= SS_STATE_FP_LAST;
		const char* mark = "";

		if (e[w] != a[w])
			mark = (fp && ss_float_lenient(a[w], e[w])) ? "  <-- differs (lenient: ok)" : "  <-- differs";

		fprintf(out, "    %-8s %08X %08X %08X%s\n", ss_state_word_name(w), i[w], e[w], a[w], mark);
	}
}
