/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"

/*
	The multiplier of the ARM7DI (data sheet 4.6, 9.3): 2 bit Booth. A step takes two bits of Rs and adds Rm,
	shifted left by the barrel shifter, 0, 1 or 2 times to Rd, or takes it away. What the Dreamcast answers that
	a plain product does not: wren7-rtl model/tests/hw/mulsem.
*/

//Steps of a multiply: the first one that leaves no bit of Rs is the last, 16 at most (ARM7DI 4.6.3)
static inline u32 arm_MulSteps(u32 rs)
{
	u32 steps = 1;

	while (steps < 16 && (rs >> (2 * steps - 1)) != 0)
		steps++;

	return steps;
}

//Booth digit of step k, -2 to 2: bits 2k+1 and 2k of Rs and the borrow of the step before, which is bit 2k-1
static inline int arm_MulDigit(u32 rs, u32 k)
{
	u32 bits = k ? (rs >> (2 * k - 1)) & 7 : (rs << 1) & 7;

	return (int)(bits & 1) + (int)((bits >> 1) & 1) - 2 * (int)(bits >> 2);
}

//How far step k shifts Rm: 2k, one more for a digit of 2 or -2
static inline u32 arm_MulShift(int digit, u32 k)
{
	return 2 * k + ((digit == 2 || digit == -2) ? 1 : 0);
}

//C after MULS / MLAS (console: mulsem): the carry out of the barrel shifter in the last step. LSL #0 passes C on
static inline bool arm_MulCarry(u32 rm, u32 rs, bool c)
{
	u32 last = arm_MulSteps(rs) - 1;
	u32 shift = arm_MulShift(arm_MulDigit(rs, last), last);

	return shift ? ((rm >> (32 - shift)) & 1) != 0 : c;
}
