/*
	The generator of programs of aica_dsp_diff: dsp_gen.h
*/
#include "dsp_gen.h"

#include <cstring>

uint32_t dsp_gen_edge(DspRandom& rnd, int bits)
{
	uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1;
	uint32_t pow = 1u << rnd.Below(bits);

	switch (rnd.Below(8))
	{
	case 0: return 0;
	case 1: return mask;							//-1
	case 2: return mask >> 1;						//the largest
	case 3: return (mask >> 1) + 1;					//the smallest
	case 4: return pow & mask;
	case 5: return (pow - 1) & mask;
	case 6: return (0 - pow) & mask;
	default: return rnd.Bits(32) & mask;
	}
}

uint16_t dsp_gen_word(DspRandom& rnd, const DspGenProfile& p, int bits)
{
	return (uint16_t)(p.edges ? dsp_gen_edge(rnd, bits) : rnd.Bits(bits));
}

void dsp_gen_encode(const AicaDspInst& inst, uint32_t junk, uint16_t w[4])
{
	AicaDspEncode(&inst, w);

	w[0] |= junk & 0x0001;
	w[1] |= (junk >> 1) & 0x0001;
	w[3] |= (junk >> 2) & 0x007F;
}

void dsp_gen_step(DspRandom& rnd, const DspGenProfile& p, uint16_t w[4])
{
	if (!p.sparse)
	{
		for (int k = 0; k < 4; k++)
			w[k] = (uint16_t)rnd.Bits(16);

		return;
	}

	AicaDspInst inst;

	memset(&inst, 0, sizeof(inst));

	//about every third step of a sparse program is left empty
	if (!rnd.Chance(3))
	{
		inst.TRA = (uint8_t)rnd.Bits(7);
		inst.TWT = rnd.Chance(4);
		inst.TWA = (uint8_t)rnd.Bits(7);
		inst.XSEL = (uint8_t)rnd.Bits(1);
		inst.YSEL = (uint8_t)rnd.Bits(2);
		inst.IRA = (uint8_t)(rnd.Chance(8) ? rnd.Bits(6) : rnd.Below(0x32));
		inst.IWT = rnd.Chance(6);
		inst.IWA = (uint8_t)rnd.Bits(5);
		inst.TABLE = (uint8_t)rnd.Bits(1);
		inst.MWT = rnd.Chance(8);
		inst.MRD = rnd.Chance(5);
		inst.EWT = rnd.Chance(8);
		inst.EWA = (uint8_t)rnd.Bits(4);
		inst.ADRL = rnd.Chance(12);
		inst.FRCL = rnd.Chance(12);
		inst.SHIFT = (uint8_t)rnd.Bits(2);
		inst.YRL = rnd.Chance(12);
		inst.NEGB = rnd.Chance(6);
		inst.ZERO = rnd.Chance(3);
		inst.BSEL = rnd.Chance(3);
		inst.NOFL = rnd.Chance(3);
		inst.MASA = (uint8_t)rnd.Bits(6);
		inst.ADREB = rnd.Chance(4);
		inst.NXADR = rnd.Chance(4);
	}

	dsp_gen_encode(inst, rnd.Bits(16), w);
}
