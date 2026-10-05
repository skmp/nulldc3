/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_sh4.h"
#include "dbg_mem.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/interpr/sh4_opcodes.h"

#include <algorithm>

/*
	The state is plain data with static storage: the emulator thread may still be inside a check while the
	process exits, so nothing here has a destructor.

	Three sets, all of keys (dbg::sh4::MaskAddr):
	  listed   the enabled listed breakpoints, as { key, id }, sorted by key and then id
	  temps    the temporary breakpoints, as { key, sp0 }
	  armed    the keys of both, sorted, each once: what the backends put a check in front of. A bit per 4 KiB
	           page of the key space stands in front of the search, so that the query of a decoder costs a
	           load for almost every instruction of a guest with breakpoints
*/

u32 dbg_sh4_armed;
u32 dbg_sh4_stepout;

namespace {

struct Listed {
	u32 key;
	u32 id;
};

struct Temp {
	u32 key;
	u32 sp0;		// R15 of the request: the breakpoint stops on this stack level or above it
};

const u32 kMaxTemps = 4;

Listed listed[dbg::kMaxBreakpoints];
u32 listed_count;

Temp temps[kMaxTemps];
u32 temp_count;

u32 armed[dbg::kMaxBreakpoints + kMaxTemps];

u8 armed_pages[1u << 17];		// 2^20 pages of 4 KiB

u32 active_mask;

struct {
	// a check or a flow report stopped and the park context has not been entered yet
	bool stop_pending;
	dbg::StopCause stop_cause;
	u32 stop_id;

	// the pc of the stop, as the park context found it
	bool stop_key_valid;
	u32 stop_key;

	// the first check behind a resume answers "continue" if it is for this key, and disarms either way
	bool exempt;
	u32 exempt_key;

	// a step is being made: its block is the one instruction, and a check that stops it owes nothing
	bool stepping;

	// the rest of the block that the last check interrupted
	bool rem_valid;
	u32 rem_pc;
	u32 rem_end;
	u32 rem_cycles;

	// step-out
	u32 so_sp;			// R15 of the request
	u32 so_calls;		// calls made on the level of the request that have not returned
	u32 so_level;		// interrupts and exceptions accepted since the request whose RTE has not run
} st;

Sh4Context& Ctx()
{
	return p_sh4rcb->cntx;
}

bool PageBit(u32 key)
{
	return (armed_pages[key >> 15] & (1u << ((key >> 12) & 7))) != 0;
}

bool IsArmedKey(u32 key)
{
	return PageBit(key) && std::binary_search(armed, armed + dbg_sh4_armed, key);
}

bool OnLevel(u32 sp, u32 sp0)
{
	return sp >= sp0 && sp - sp0 < dbg::sh4::kStackWindow;
}

// The backend whose code cache holds the checks. Without an emulator thread there is no cache to keep in step:
// every thread start builds a fresh one.
SuperH4Backend* CacheBackend()
{
	if (!dbg::emu::OnEmuThread() || sh4_cpu == nullptr)
		return nullptr;

	return sh4_cpu->DebugBackend();
}

// Makes the armed set that of listed and temps. Every key that entered or left it has the blocks of its page
// discarded: they are compiled again, with or without the check.
void Rearm()
{
	static u32 before[dbg::kMaxBreakpoints + kMaxTemps];
	u32 before_count = dbg_sh4_armed;

	std::copy(armed, armed + before_count, before);

	u32 count = 0;

	for (u32 i = 0; i < listed_count; i++)
		armed[count++] = listed[i].key;
	for (u32 i = 0; i < temp_count; i++)
		armed[count++] = temps[i].key;

	std::sort(armed, armed + count);
	count = (u32)(std::unique(armed, armed + count) - armed);

	for (u32 i = 0; i < before_count; i++)
		armed_pages[before[i] >> 15] = 0;
	for (u32 i = 0; i < count; i++)
		armed_pages[armed[i] >> 15] |= (u8)(1u << ((armed[i] >> 12) & 7));

	// the query of the decoders reads the count first
	dbg_sh4_armed = count;

	SuperH4Backend* backend = CacheBackend();

	if (backend == nullptr)
		return;

	u32 a = 0, b = 0;

	while (a < before_count || b < count)
	{
		if (b == count || (a < before_count && before[a] < armed[b]))
			backend->DebugDiscardPage(before[a++]);
		else if (a == before_count || armed[b] < before[a])
			backend->DebugDiscardPage(armed[b++]);
		else
			a++, b++;
	}
}

// Does a breakpoint on this key stop now? A listed one stops while its category is active and wins over a
// temporary one; the id is the lowest of the listed breakpoints on the key.
bool Evaluate(u32 key, dbg::StopCause* cause, u32* id)
{
	if (active_mask & dbg::CategoryBit(dbg::Category::Sh4))
	{
		const Listed* first = listed;
		const Listed* end = listed + listed_count;
		const Listed* it = std::lower_bound(first, end, key, [](const Listed& l, u32 k) { return l.key < k; });

		if (it != end && it->key == key)
		{
			*cause = dbg::StopCause::Breakpoint;
			*id = it->id;
			return true;
		}
	}

	for (u32 i = 0; i < temp_count; i++)
	{
		// a deeper activation of the same code, or another stack, is not the place the step asked for
		if (temps[i].key == key && OnLevel(Ctx().r[15], temps[i].sp0))
		{
			*cause = dbg::StopCause::Step;
			*id = 0;
			return true;
		}
	}

	return false;
}

bool ReadOpcode(u32 pc, u16* op)
{
	return dbg::mem::Read16(dbg::Target::Sh4, pc, op);
}

bool HasSlot(u16 op)
{
	return (OpDesc[op]->type & Delayslot) != 0;
}

bool IsUndefined(u16 op)
{
	return OpDesc[op]->oph == iNotImplemented;
}

// Will the instruction at the pc be checked by what executes it?
bool CheckWanted(u32 pc)
{
	if (dbg_sh4_armed == 0)
		return false;

	u16 op;
	bool has_slot = ReadOpcode(pc, &op) && HasSlot(op);

	return dbg_sh4_WantsCheck(pc, has_slot);
}

void ArmExemption(bool check_is_made)
{
	u32 pc = Ctx().pc;

	st.exempt = false;

	if (!check_is_made || !st.stop_key_valid || dbg::sh4::MaskAddr(pc) != st.stop_key)
		return;

	if (CheckWanted(pc))
	{
		st.exempt = true;
		st.exempt_key = dbg::sh4::MaskAddr(pc);
	}
}

u32 Stop(dbg::StopCause cause, u32 id)
{
	st.stop_pending = true;
	st.stop_cause = cause;
	st.stop_id = id;

	dbg::emu::LatchStop(cause, id);

	return 1;
}

}

bool dbg_sh4_IsArmed(u32 pc)
{
	return IsArmedKey(dbg::sh4::MaskAddr(pc));
}

bool dbg_sh4_AnyArmed(u32 pc, u32 bytes)
{
	if (dbg_sh4_armed == 0)
		return false;

	// the keys of a run of addresses are a run
	u32 key = dbg::sh4::MaskAddr(pc);
	const u32* first = armed;
	const u32* end = armed + dbg_sh4_armed;
	const u32* it = std::lower_bound(first, end, key);

	return it != end && *it - key < bytes;
}

extern "C" u32 DYNACALL dbg_sh4_Check(u32 pc, u32 refund, u32 block_end)
{
	u32 key = dbg::sh4::MaskAddr(pc);

	if (st.exempt)
	{
		st.exempt = false;

		if (st.exempt_key == key)
			return 0;
	}

	// Nothing stops while the category of the listed breakpoints is not active and no temporary breakpoint
	// exists: the arrivals of a muted session cost a call and a test
	if (temp_count == 0 && !(active_mask & dbg::CategoryBit(dbg::Category::Sh4)))
		return 0;

	dbg::StopCause cause;
	u32 id;

	if (!Evaluate(key, &cause, &id))
	{
		// A delayed branch is checked for its slot as well
		if (!Evaluate(dbg::sh4::MaskAddr(pc + 2), &cause, &id))
			return 0;

		// When the pc is not armed itself the check is there for the slot. When it is, the opcode says
		// whether the instruction has one.
		if (IsArmedKey(key))
		{
			u16 op;

			if (!ReadOpcode(pc, &op) || !HasSlot(op))
				return 0;
		}
	}

	Ctx().pc = pc;

	st.rem_valid = block_end != 0 && !st.stepping;
	st.rem_pc = pc;
	st.rem_end = block_end;
	st.rem_cycles = refund;

	return Stop(cause, id);
}

void dbg_sh4_SlotRuns()
{
	// The exemption is for the instruction that runs first behind a resume. A slot that runs without a check
	// call is that instruction when the stop was between it and its branch
	st.exempt = false;
}

/*
	sp is R15 behind the call or return and its slot.

	Handlers entered since the request are skipped whole. Below the level of the request nothing counts. A
	return that lands above it has left the activation. One that lands exactly on it is the return of the
	activation itself if that keeps nothing on the stack, or the return of something it called: calls made on
	the level of the request are counted, and their returns taken off.
*/
extern "C" u32 DYNACALL dbg_sh4_CheckFlow(u32 flow)
{
	if (!dbg_sh4_stepout)
		return 0;

	u32 sp = Ctx().r[15];

	if (st.so_level != 0)
	{
		if (flow == DBG_SH4_FLOW_RTE)
			st.so_level--;

		return 0;
	}

	if (flow == DBG_SH4_FLOW_CALL)
	{
		if (sp == st.so_sp)
			st.so_calls++;

		return 0;
	}

	if (!OnLevel(sp, st.so_sp))
		return 0;

	if (sp == st.so_sp && st.so_calls != 0)
	{
		if (flow == DBG_SH4_FLOW_RTS)
			st.so_calls--;

		return 0;
	}

	// the block was charged in full
	st.rem_valid = false;

	return Stop(dbg::StopCause::Step, 0);
}

void dbg_sh4_OnException()
{
	if (dbg_sh4_stepout)
		st.so_level++;

	// The handler runs in front of the instruction that a resume was made for: when it returns there, the
	// instruction has not executed and its check stops
	st.exempt = false;
}

bool dbg_sh4_StopPending()
{
	return st.stop_pending;
}

bool dbg_sh4_TakeRemainder(u32 pc, u32* end_pc, u32* cycles)
{
	if (!st.rem_valid)
		return false;

	st.rem_valid = false;

	if (st.rem_pc != pc)
		return false;

	*end_pc = st.rem_end;
	*cycles = st.rem_cycles;

	return true;
}

namespace dbg { namespace sh4 {

void SetBreakpoints(const u32* ids, const u32* addrs, u32 n)
{
	if (n > kMaxBreakpoints)
		n = kMaxBreakpoints;

	for (u32 i = 0; i < n; i++)
	{
		listed[i].key = MaskAddr(addrs[i]);
		listed[i].id = ids[i];
	}

	listed_count = n;

	std::sort(listed, listed + n, [](const Listed& a, const Listed& b) { return a.key != b.key ? a.key < b.key : a.id < b.id; });

	Rearm();
}

void SetActiveMask(u32 categories)
{
	active_mask = categories;
}

int StepCheck(const char** why)
{
	const char* reason = nullptr;
	u32 pc = Ctx().pc;
	u16 op, slot;

	if (pc & 1)
		reason = "pc is odd";
	else if (!ReadOpcode(pc, &op))
		reason = "pc not readable";
	else if (IsUndefined(op))
		reason = "undefined opcode";
	else if (HasSlot(op) && (!ReadOpcode(pc + 2, &slot) || IsUndefined(slot) || OpDesc[slot]->SetPC()))
		reason = "illegal slot instruction";

	if (reason == nullptr)
		return 0;

	if (why)
		*why = reason;

	return kErrCannotStep;
}

void PrepareResume()
{
	if (st.rem_valid && st.rem_pc != Ctx().pc)
		st.rem_valid = false;

	// The exemption is consumed by the first check that is made. On a backend without native checks none is
	// made unless the fallback loop runs: an exemption armed there would wait for a later arrival.
	SuperH4Backend* backend = sh4_cpu ? sh4_cpu->DebugBackend() : nullptr;
	bool native = backend && (backend->DebugCaps() & SH4DBG_NATIVE);

	ArmExemption(native || FallbackWanted());
}

void PrepareStep()
{
	st.rem_valid = false;
	st.stepping = true;

	// a step makes the check of its instruction on every backend
	ArmExemption(true);
}

bool PrepareStepOver()
{
	u32 pc = Ctx().pc;
	u16 op;

	if (!ReadOpcode(pc, &op) || temp_count == kMaxTemps)
		return false;

	u32 target;

	if ((op & 0xF000) == 0xB000 || (op & 0xF0FF) == 0x0003 || (op & 0xF0FF) == 0x400B)	// bsr, bsrf, jsr
		target = pc + 4;
	else if ((op & 0xFF00) == 0xC300)	// trapa
		target = pc + 2;
	else
		return false;

	temps[temp_count].key = MaskAddr(target);
	temps[temp_count].sp0 = Ctx().r[15];
	temp_count++;

	Rearm();

	return true;
}

void PrepareStepOut()
{
	st.so_sp = Ctx().r[15];
	st.so_calls = 0;
	st.so_level = 0;

	dbg_sh4_stepout = 1;

	// the blocks that end with a call or a return are compiled again, with the report
	if (SuperH4Backend* backend = CacheBackend())
		backend->DebugDiscardFlow(false);
}

void OnStop(StopInfo* out)
{
	if (out)
	{
		out->cause = st.stop_pending ? st.stop_cause : StopCause::NoCause;
		out->breakpointId = st.stop_pending ? st.stop_id : 0;
	}

	if (temp_count != 0)
	{
		temp_count = 0;
		Rearm();
	}

	if (dbg_sh4_stepout)
	{
		dbg_sh4_stepout = 0;

		if (SuperH4Backend* backend = CacheBackend())
			backend->DebugDiscardFlow(true);
	}

	st.exempt = false;
	st.stop_pending = false;
	st.stepping = false;

	st.stop_key_valid = true;
	st.stop_key = MaskAddr(Ctx().pc);
}

void OnPcWritten()
{
	st.rem_valid = false;
	st.exempt = false;
}

bool FallbackWanted()
{
	if (listed_count != 0 && (active_mask & CategoryBit(Category::Sh4)))
		return true;

	return temp_count != 0 || dbg_sh4_stepout != 0;
}

} }
