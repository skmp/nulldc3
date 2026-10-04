/*
	SingleStepTests/sh4 vector files (<opcode pattern>_sz<N>_pr<N>.json.bin)

	A file is 500 fixed size records with no header, everything is little endian and packed. A record is the
	state before, the state after, the bus activity of the 4 instructions that are run and the opcodes to serve:

	opcodes[0..3] are fetched from PC, PC+2, PC+4 and PC+6, opcodes[4] from anywhere else. They are always
	nop / the instruction under test / add r1,r1 / nop and add r2,r2 everywhere else, so a taken delayed branch
	doubles r1 in its delay slot and whatever is run at its target doubles r2.
*/
#pragma once
#include "types.h"

#include <string>
#include <vector>

#pragma pack(push, 1)

struct SsState
{
	u32 R[16];		//the active R0-R15
	u32 R_[8];		//the inactive bank of R0-R7
	u32 FP0[16];	//the active FP bank (FR0-FR15)
	u32 FP1[16];	//the inactive FP bank (XF0-XF15)
	u32 PC;
	u32 GBR;
	u32 SR;
	u32 SSR;
	u32 SPC;
	u32 VBR;
	u32 SGR;
	u32 DBR;
	u32 MACL;
	u32 MACH;
	u32 PR;
	u32 FPSCR;
	u32 FPUL;
};

#define SS_STATE_WORDS (sizeof(SsState) / sizeof(u32))
#define SS_STATE_FP_FIRST 24	//FP0[0]
#define SS_STATE_FP_LAST 55		//FP1[15]

struct SsStateBlock
{
	s32 size;
	u32 tag;
	SsState s;
};

#define SS_ACTION_READ 1
#define SS_ACTION_WRITE 2
#define SS_ACTION_FETCH 4

//One instruction. Values are zero extended to 64 bits. A 64 bit value has the word at addr in its low half and
//the word at addr+4 in its high half. The width of an access is not recorded
struct SsCycle
{
	u32 actions;
	u32 fetch_addr;
	u32 fetch_val;
	u32 write_addr;
	u64 write_val;
	u32 read_addr;
	u64 read_val;
};

struct SsCyclesBlock
{
	s32 size;
	u32 tag;
	u32 count;
	SsCycle c[4];
};

struct SsOpcodesBlock
{
	s32 size;
	u32 tag;
	u32 op[5];
};

struct SsTest
{
	s32 size;
	SsStateBlock initial;
	SsStateBlock final_;
	SsCyclesBlock cycles;
	SsOpcodesBlock opcodes;
};

#pragma pack(pop)

static_assert(sizeof(SsState) == 276, "SsState layout");
static_assert(sizeof(SsStateBlock) == 284, "SsStateBlock layout");
static_assert(sizeof(SsCycle) == 36, "SsCycle layout");
static_assert(sizeof(SsCyclesBlock) == 156, "SsCyclesBlock layout");
static_assert(sizeof(SsOpcodesBlock) == 28, "SsOpcodesBlock layout");
static_assert(sizeof(SsTest) == 756, "SsTest layout");

#define SS_FILE_SUFFIX ".json.bin"

//The opcode under test
static inline u16 ss_opcode(const SsTest& test) { return (u16)test.opcodes.op[1]; }

//Reads a vector file and checks the framing of every record. On failure error says what is wrong
bool ss_load_file(const std::string& path, std::vector<SsTest>& tests, std::string& error);

//Names of the vector files in dir without the directory and without SS_FILE_SUFFIX, sorted
bool ss_list_files(const std::string& dir, std::vector<std::string>& names);

//Name of a state word: "R0" .. "FPUL", index is into the SS_STATE_WORDS words of SsState
const char* ss_state_word_name(u32 index);
