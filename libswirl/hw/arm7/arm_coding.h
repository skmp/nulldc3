/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
 * coding.h,	arm binary instruction coding
 *
 */
#pragma once

namespace ARM
{
	

	/*
	 *	Encoding Hi-Order 4bits, ConditionCode
	 *
	 */

	enum ConditionCode
	{
		EQ = 0x00,	Equal		= EQ,	// 0000		Equal								Z set
		NE = 0x01,	NotEqual	= NE,   // 0001		Not equal							Z clear
		CS = 0x02,	CarrySet	= CS,   // 0010		Carry set/unsigned higher or same	C set
		CC = 0x03,	CarryClr	= CC,   // 0011		Carry clear/unsigned lowe			C clear
		MI = 0x04,	Minus		= MI,   // 0100		Minus/negative						N set
		PL = 0x05,	Plus		= PL,   // 0101		Plus/positive or zero				N clear
		VS = 0x06,	Overflow	= VS,   // 0110		Overflow							V set
		VC = 0x07,	NoOverflow	= VC,   // 0111		No overflow							V clear

		HI = 0x08,	UnHigher	= HI,   // 1000		Unsigned higher						C set and Z clear
		LS = 0x09,	UnLower		= LS,   // 1001		Unsigned lower or same				C clear or Z set

		GE = 0x0A,	GrOrEqual	= GE,   // 1010		Signed greater than or equal		N set and V set, or N clear and V clear (N == V)
		LT = 0x0B,	Less		= LT,   // 1011		Signed less than					N set and V clear, or N clear and V set (N != V)
		GT = 0x0C,	Greater		= GT,   // 1100		Signed greater than					Z clear, and either N set and V set, or N clear and V clear (Z == 0,N == V)
		LE = 0x0D,	LessOrEqual	= LE,   // 1101		Signed less than or equal			Z set, or N set and V clear, or N clear and V set (Z == 1 or N != V)

		AL = 0x0E,	Always		= AL,   // 1110		Always (unconditional) -


		UC = 0x0F, Unconditional= UC,	// 1111		Unconditional Special Instruction for ARMv5? and above

#define _ARM_COMPAT
#if defined(_ARM_COMPAT)
		CC_EQ=EQ, CC_NE=NE, CC_CS=CS, CC_CC=CC, CC_MI=MI, CC_PL=PL, CC_VS=VS, CC_VC=VC,
		CC_HI=HI, CC_LS=LS, CC_GE=GE, CC_LT=LT, CC_GT=GT, CC_LE=LE, CC_AL=AL, CC_UC=UC,

		CC_HS=CS, CC_LO=CC,
#endif

		ConditionCode_Size
	};

	
	
	/*
	 *	Data-processing OPCODE 4bits, DPOP
	 *
	 */

	enum DPOP
	{
		DP_AND,	// 0000		Logical AND             				Rd := Rn AND shifter_operand
		DP_EOR,	// 0001		Logical Exclusive OR    				Rd := Rn EOR shifter_operand
		DP_SUB,	// 0010		Subtract                				Rd := Rn - shifter_operand
		DP_RSB,	// 0011		Reverse Subtract        				Rd := shifter_operand - Rn
		DP_ADD,	// 0100		Add                     				Rd := Rn + shifter_operand
		DP_ADC,	// 0101		Add with Carry          				Rd := Rn + shifter_operand + Carry Flag
		DP_SBC,	// 0110		Subtract with Carry     				Rd := Rn - shifter_operand - NOT(Carry Flag)
		DP_RSC,	// 0111		Reverse Subtract with Carry				Rd := shifter_operand - Rn - NOT(Carry Flag)
		DP_TST,	// 1000		Test Update flags after					Rn AND shifter_operand
		DP_TEQ,	// 1001		Test Equivalence Update flags after		Rn EOR shifter_operand
		DP_CMP,	// 1010		Compare Update flags after				Rn - shifter_operand
		DP_CMN,	// 1011		Compare Negated Update flags after		Rn + shifter_operand
		DP_ORR,	// 1100		Logical (inclusive) OR					Rd := Rn OR shifter_operand
		DP_MOV,	// 1101		Move									Rd := shifter_operand (no first operand)
		DP_BIC,	// 1110		Bit Clear               				Rd := Rn AND NOT(shifter_operand)
		DP_MVN	// 1111		Move Not                				Rd := NOT shifter_operand (no first operand)
	};



	enum ShiftOp {
		S_LSL,
		S_LSR,
		S_ASR,
		S_ROR,
		S_RRX=S_ROR
	};

	
	/*
	 *	eReg:		ARM Register ID
	 *
	 */
	enum eReg
	{
		r0=0,r1,  r2,  r3,
		r4,  r5,  r6,  r7,
		r8,  r9,  r10, r11,
		r12, r13, r14, r15,

		R0=0,R1,  R2,  R3,
		R4,  R5,  R6,  R7,
		R8,  R9,  R10, R11,
		R12, R13, R14, R15,

		// Aliases

		a1 = r0,    a2 = r1,    a3 = r2,    a4 = r3,
		A1 = R0,    A2 = R1,    A3 = R2,    A4 = R3,

		v1 = r4,    v2 = r5,    v3 = r6,    v4 = r7,	v5 = r8,    v6 = r9,
		V1 = R4,    V2 = R5,    V3 = R6,    V4 = R7,	V5 = R8,    V6 = R9,
		
		rfp = r9,	sl = r10,	fp = r11,	ip = r12,	sp = r13,	lr = r14,	pc = r15,
		RFP = R9,	SL = R10,	FP = R11,	IP = R12,	SP = R13,	LR = R14,	PC = R15,
	};


	static u32 ARMImmid8r4_enc(u32 imm32)
	{
		for (int i = 0; i <= 30; i += 2)
		{
			u32 immv = (imm32 << i) | (imm32 >> (32 - i));
			if (i == 0)
				immv = imm32;
			if (immv < 256)
			{
				return ((i / 2) << 8) | immv;
			}
		}

		return -1;
	}

	static inline u32 ARMImmid8r4(u32 imm8r4)
	{
		u32 rv = ARMImmid8r4_enc(imm8r4);

		verify(rv != -1);
		return rv;
	}

	static inline bool is_i8r4(u32 i32)
	{
		return ARMImmid8r4_enc(i32) != -1;
	}
};
