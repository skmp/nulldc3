/*
	The harness on itself. A harness that cannot fail proves nothing: every way a vector can go wrong is provoked
	here and has to be caught, and the two properties of the memory map the suites lean on are checked on the
	real _vmem code.
*/
#include "test_guard.h"
#include "sh4_env.h"
#include "ss_format.h"
#include "ss_memory.h"
#include "ss_policy.h"
#include "ss_state.h"

#include "hw/sh4/sh4_if.h"
#include "hw/mem/_vmem.h"

#include <cstdio>
#include <cstring>

#if !defined(_WIN32)
#include <signal.h>
#endif

void* _vmem_page_info(u32 addr, bool& ismem, u32 sz, u32& page_sz, bool rw);

static u32 checks;
static u32 failures;

#define CHECK(cond) \
	do \
	{ \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

//// test_guard

static void do_nothing(void*) { }
static void do_die(void*) { die("selftest die"); }
static void do_verify(void*) { verify(1 + 1 == 3); }
static void do_abort(void*) { test_guard_abort("selftest abort %d", 42); }
static void do_msgbox(void*) { msgboxf("just a %s", MBX_ICONERROR, "notification"); }

static volatile u32 spin;
static void do_hang(void*) { for (;;) spin = spin + 1; }

static void test_guard()
{
	CHECK(test_guard_run(&do_nothing, 0) == TGR_OK);

	CHECK(test_guard_run(&do_die, 0) == TGR_DIE);
	CHECK(strstr(test_guard_message(), "selftest die") != 0);

	CHECK(test_guard_run(&do_verify, 0) == TGR_DIE);
	CHECK(strstr(test_guard_message(), "1 + 1 == 3") != 0);

	CHECK(test_guard_run(&do_abort, 0) == TGR_ABORT);
	CHECK(strcmp(test_guard_message(), "selftest abort 42") == 0);

	CHECK(test_guard_run(&do_msgbox, 0) == TGR_OK);
	CHECK(test_guard_msgbox_count() == 1);
	CHECK(strcmp(test_guard_last_msgbox(), "just a notification") == 0);

	//the next call starts clean
	CHECK(test_guard_run(&do_nothing, 0) == TGR_OK);
	CHECK(test_guard_msgbox_count() == 0);
	CHECK(test_guard_message()[0] == 0);

#if !defined(_WIN32)
	struct Raise { static void segv(void*) { raise(SIGSEGV); } };

	CHECK(test_guard_run(&Raise::segv, 0) == TGR_SIGNAL);
	CHECK(strstr(test_guard_message(), "SIGSEGV") != 0);

	CHECK(test_guard_run(&do_hang, 0) == TGR_TIMEOUT);
	CHECK(test_guard_run(&do_nothing, 0) == TGR_OK);
#endif
}

//// _vmem: handler lookup for constant addresses

#define HANDLERS 6

template<u32 id> static u8 DYNACALL h_read8(void*, u32) { return (u8)id; }
template<u32 id> static u16 DYNACALL h_read16(void*, u32) { return (u16)id; }
template<u32 id> static u32 DYNACALL h_read32(void*, u32) { return id; }
template<u32 id> static void DYNACALL h_write8(void*, u32, u8) { }
template<u32 id> static void DYNACALL h_write16(void*, u32, u16) { }
template<u32 id> static void DYNACALL h_write32(void*, u32, u32) { }

#define REGISTER(id) _vmem_register_handler(0, h_read8<id>, h_read16<id>, h_read32<id>, h_write8<id>, h_write16<id>, h_write32<id>)

//_vmem_read_const / _vmem_page_info are how the recompilers resolve accesses to constant addresses. For a page
//that is served by handlers they have to hand out the handlers that _vmem_ReadMem* / _vmem_WriteMem* call
static void test_vmem_handlers()
{
	_vmem_reset();

	_vmem_handler ids[HANDLERS];

	ids[0] = REGISTER(1);
	ids[1] = REGISTER(2);
	ids[2] = REGISTER(3);
	ids[3] = REGISTER(4);
	ids[4] = REGISTER(5);
	ids[5] = REGISTER(6);

	for (u32 i = 0; i < HANDLERS; i++)
	{
		u32 page = 0x10 + i;
		u32 addr = (page << 24) | 0x1230;
		u32 want = i + 1;

		_vmem_map_handler(ids[i], page, page);

		CHECK(_vmem_ReadMem8(addr) == want);
		CHECK(_vmem_ReadMem16(addr) == want);
		CHECK(_vmem_ReadMem32(addr) == want);

		bool ismem = true;

		typedef u8 DYNACALL Read8(void*, u32);
		typedef u16 DYNACALL Read16(void*, u32);
		typedef u32 DYNACALL Read32(void*, u32);

		Read8* r8 = (Read8*)_vmem_read_const(addr, ismem, 1);
		CHECK(!ismem && r8(0, addr) == want);

		Read16* r16 = (Read16*)_vmem_read_const(addr, ismem, 2);
		CHECK(!ismem && r16(0, addr) == want);

		Read32* r32 = (Read32*)_vmem_read_const(addr, ismem, 4);
		CHECK(!ismem && r32(0, addr) == want);

		u32 page_sz = 0;

		CHECK(_vmem_page_info(addr, ismem, 1, page_sz, true) == (void*)_vmem_RF8[ids[i]]);
		CHECK(_vmem_page_info(addr, ismem, 2, page_sz, true) == (void*)_vmem_RF16[ids[i]]);
		CHECK(_vmem_page_info(addr, ismem, 4, page_sz, true) == (void*)_vmem_RF32[ids[i]]);
		CHECK(_vmem_page_info(addr, ismem, 1, page_sz, false) == (void*)_vmem_WF8[ids[i]]);
		CHECK(_vmem_page_info(addr, ismem, 2, page_sz, false) == (void*)_vmem_WF16[ids[i]]);
		CHECK(_vmem_page_info(addr, ismem, 4, page_sz, false) == (void*)_vmem_WF32[ids[i]]);
		CHECK(!ismem && page_sz == 24);
	}

	//a page that is backed by memory
	static u8 DECL_ALIGN(256) block[256];

	block[0x10] = 0x78;
	block[0x11] = 0x56;
	block[0x12] = 0x34;
	block[0x13] = 0x12;

	_vmem_map_block(block, 0x20, 0x20, 255);

	bool ismem = false;
	void* ptr = _vmem_read_const(0x20000010, ismem, 4);

	CHECK(ismem && ptr == &block[0x10]);
	CHECK(_vmem_ReadMem32(0x20000010) == 0x12345678);

	_vmem_reset();
}

//// _vmem: unaligned accesses

static u32 unaligned_addr;
static u64 unaligned_data;

static void do_read16(void*) { unaligned_data = _vmem_ReadMem16(unaligned_addr); }
static void do_read16sx(void*) { unaligned_data = _vmem_ReadMem16SX32(unaligned_addr); }
static void do_read32(void*) { unaligned_data = _vmem_ReadMem32(unaligned_addr); }
static void do_read64(void*) { unaligned_data = _vmem_ReadMem64(unaligned_addr); }
static void do_write16(void*) { _vmem_WriteMem16(unaligned_addr, 0x1234); }
static void do_write32(void*) { _vmem_WriteMem32(unaligned_addr, 0x12345678); }
static void do_write64(void*) { _vmem_WriteMem64(unaligned_addr, 0x1122334455667788ull); }

//An unaligned access must end in the handler (here: the one of sh4_env, which ends the guarded call) and must
//not reach memory. An aligned one must go through
static void test_vmem_unaligned()
{
	static const struct { TestGuardFP* fn; u32 size; const char* what; } access[] =
	{
		{ &do_read16, 2, "16bit read" },
		{ &do_read16sx, 2, "16bit read" },
		{ &do_read32, 4, "32bit read" },
		{ &do_read64, 8, "64bit read" },
		{ &do_write16, 2, "16bit write" },
		{ &do_write32, 4, "32bit write" },
		{ &do_write64, 8, "64bit write" },
	};

	for (u32 i = 0; i < sizeof(access) / sizeof(access[0]); i++)
	{
		for (u32 offset = 0; offset < 8; offset++)
		{
			unaligned_addr = 0x8C001000 + offset;

			SsTest none;
			memset(&none, 0, sizeof(none));
			ss_mem_begin(none, 0, 0, true);

			TestGuardResult rv = test_guard_run(access[i].fn, 0);

			if (offset & (access[i].size - 1))
			{
				CHECK(rv == TGR_ABORT);
				CHECK(strstr(test_guard_message(), "Unaligned") != 0 && strstr(test_guard_message(), access[i].what) != 0);
				CHECK(ss_mem_log().empty());
			}
			else
			{
				CHECK(rv == TGR_OK);
				CHECK(!ss_mem_log().empty());
			}
		}
	}
}

//// ss_memory

static SsTest make_test()
{
	SsTest t;

	memset(&t, 0, sizeof(t));

	t.initial.s.PC = 0x8C001000;

	t.opcodes.op[0] = 0x0009;
	t.opcodes.op[1] = 0x6122;
	t.opcodes.op[2] = 0x311C;
	t.opcodes.op[3] = 0x0009;
	t.opcodes.op[4] = 0x322C;

	for (u32 i = 0; i < 4; i++)
	{
		t.cycles.c[i].actions = SS_ACTION_FETCH;
		t.cycles.c[i].fetch_addr = t.initial.s.PC + i * 2;
		t.cycles.c[i].fetch_val = t.opcodes.op[i];
		t.cycles.c[i].read_addr = 0xFFFFFFFF;
		t.cycles.c[i].write_addr = 0xFFFFFFFF;
	}

	return t;
}

static void test_memory()
{
	u32 pc = 0x8C001000;

	//the expected order, all of it
	SsTest t = make_test();

	t.cycles.c[1].actions |= SS_ACTION_READ | SS_ACTION_WRITE;
	t.cycles.c[1].read_addr = 0x0C000100;
	t.cycles.c[1].read_val = 0xCAFE;
	t.cycles.c[1].write_addr = 0x0C000200;
	t.cycles.c[1].write_val = 0x11223344;

	ss_mem_begin(t, 2, 4, true);
	CHECK(_vmem_ReadMem16(pc) == 0x0009);
	CHECK(_vmem_ReadMem16(pc + 2) == 0x6122);
	CHECK(ss_mem_fetches() == 2);
	CHECK(_vmem_ReadMem16(0x0C000100) == 0xCAFE);
	CHECK(ss_mem_fetches() == 2);
	_vmem_WriteMem32(0x0C000200, 0x11223344);
	CHECK(_vmem_ReadMem16(pc + 4) == 0x311C);
	CHECK(_vmem_ReadMem16(pc + 6) == 0x0009);
	CHECK(ss_mem_fetches() == 4);
	CHECK(ss_mem_end());
	CHECK(ss_mem_error()[0] == 0);
	CHECK(ss_mem_expected().size() == 6 && ss_mem_log().size() == 6);

	//an expected access that is not made
	ss_mem_begin(t, 2, 4, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	_vmem_ReadMem16(0x0C000100);
	CHECK(ss_mem_error()[0] == 0);
	CHECK(!ss_mem_end());
	CHECK(strstr(ss_mem_error(), "was not made") != 0);

	//out of order
	ss_mem_begin(t, 2, 4, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	_vmem_WriteMem32(0x0C000200, 0x11223344);
	CHECK(strstr(ss_mem_error(), "expected read16 @0C000100, got write32 @0C000200") != 0);
	CHECK(!ss_mem_end());

	//wrong address
	ss_mem_begin(t, 2, 4, true);
	_vmem_ReadMem16(pc + 2);
	CHECK(strstr(ss_mem_error(), "expected fetch16 @8C001000, got read16 @8C001002") != 0);

	//wrong width
	ss_mem_begin(t, 2, 4, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	_vmem_ReadMem8(0x0C000100);
	CHECK(strstr(ss_mem_error(), "expected read16 @0C000100, got read8 @0C000100") != 0);

	//wrong value written
	ss_mem_begin(t, 2, 4, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	_vmem_ReadMem16(0x0C000100);
	_vmem_WriteMem32(0x0C000200, 0x11223345);
	CHECK(strstr(ss_mem_error(), "value 11223345, expected 11223344") != 0);

	//more than expected
	ss_mem_begin(t, 2, 4, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	_vmem_ReadMem16(0x0C000100);
	_vmem_WriteMem32(0x0C000200, 0x11223344);
	_vmem_ReadMem16(pc + 4);
	_vmem_ReadMem16(pc + 6);
	CHECK(ss_mem_error()[0] == 0);
	_vmem_ReadMem16(pc + 8);
	CHECK(strstr(ss_mem_error(), "no bus access is expected any more") != 0);

	//a data access the file's policy has no width for
	ss_mem_begin(t, 0, 4, true);
	CHECK(strstr(ss_mem_error(), "no read width") != 0);

	//64 bit accesses: two 32 bit ones, low word first
	SsTest w = make_test();

	w.cycles.c[1].actions |= SS_ACTION_READ;
	w.cycles.c[1].read_addr = 0x0C000100;
	w.cycles.c[1].read_val = 0xAE3A8272D98B001Aull;

	ss_mem_begin(w, 8, 0, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	CHECK(_vmem_ReadMem64(0x0C000100) == 0xAE3A8272D98B001Aull);
	_vmem_ReadMem16(pc + 4);
	_vmem_ReadMem16(pc + 6);
	CHECK(ss_mem_end());

	ss_mem_begin(w, 8, 0, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	CHECK(_vmem_ReadMem32(0x0C000100) == 0xD98B001A);
	CHECK(!ss_mem_end());
	CHECK(strstr(ss_mem_error(), "high word") != 0);

	w.cycles.c[1].actions = SS_ACTION_FETCH | SS_ACTION_WRITE;
	w.cycles.c[1].write_addr = 0x0C000200;
	w.cycles.c[1].write_val = 0x1122334455667788ull;

	ss_mem_begin(w, 0, 8, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	_vmem_WriteMem64(0x0C000200, 0x1122334455667788ull);
	_vmem_ReadMem16(pc + 4);
	_vmem_ReadMem16(pc + 6);
	CHECK(ss_mem_end());

	ss_mem_begin(w, 0, 8, true);
	_vmem_ReadMem16(pc);
	_vmem_ReadMem16(pc + 2);
	_vmem_WriteMem64(0x0C000200, 0x1122334555667788ull);
	CHECK(strstr(ss_mem_error(), "write32 @0C000204: value 11223345, expected 11223344") != 0);

	//opcodes by address: the data accesses stay ordered, fetches are free
	ss_mem_begin(t, 2, 4, false);
	CHECK(ss_mem_expected().size() == 2);
	ss_mem_serve_opcodes(true);
	CHECK(_vmem_ReadMem16(pc + 6) == 0x0009);
	CHECK(_vmem_ReadMem16(pc + 2) == 0x6122);
	CHECK(_vmem_ReadMem16(pc + 8) == 0x322C);
	CHECK(_vmem_ReadMem16(pc - 2) == 0x322C);
	CHECK(_vmem_ReadMem16(0x12345678) == 0x322C);
	ss_mem_serve_opcodes(false);
	CHECK(_vmem_ReadMem16(0x0C000100) == 0xCAFE);
	_vmem_WriteMem32(0x0C000200, 0x11223344);
	CHECK(ss_mem_end());
	CHECK(ss_mem_fetches() == 0);

	//the same through the constant address lookup the recompilers use
	ss_mem_begin(t, 2, 4, false);

	bool ismem = true;
	typedef u16 DYNACALL Read16(void*, u32);
	Read16* handler = (Read16*)_vmem_read_const(0x0C000100, ismem, 2);

	CHECK(!ismem && handler(0, 0x0C000100) == 0xCAFE);
	_vmem_WriteMem32(0x0C000200, 0x11223344);
	CHECK(ss_mem_end());
}

//// ss_policy

static void test_policy()
{
	CHECK(ss_policy_count() == 233);
	CHECK(ss_policy_find("no such file") == 0);

	const SsFilePolicy* mov_l = ss_policy_find("0110nnnnmmmm0010_sz0_pr0");
	const SsFilePolicy* jmp = ss_policy_find("0100mmmm00101011_sz0_pr0");
	const SsFilePolicy* ldc_sr = ss_policy_find("0100mmmm00001110_sz0_pr0");
	const SsFilePolicy* trapa = ss_policy_find("11000011iiiiiiii_sz0_pr0");
	const SsFilePolicy* fadd = ss_policy_find("1111nnnnmmmm0000_sz0_pr0");
	const SsFilePolicy* fadd_d = ss_policy_find("1111nnn0mmm00000_sz0_pr1");
	const SsFilePolicy* ftrc = ss_policy_find("1111mmmm00111101_sz0_pr0");
	const SsFilePolicy* fmov = ss_policy_find("1111nnnnmmmm1100_sz0_pr0");

	CHECK(mov_l && jmp && ldc_sr && trapa && fadd && fadd_d && ftrc && fmov);
	if (!(mov_l && jmp && ldc_sr && trapa && fadd && fadd_d && ftrc && fmov))
		return;

	SsTest t = make_test();

	t.initial.s.SR = 0x40000000;
	CHECK(ss_policy_filter(*mov_l, t) == SSF_NONE);

	//data address
	t.cycles.c[1].actions |= SS_ACTION_READ;
	t.cycles.c[1].read_addr = 0x0C000100;
	CHECK(ss_policy_filter(*mov_l, t) == SSF_NONE);
	t.cycles.c[1].read_addr = 0x0C000102;
	CHECK(ss_policy_filter(*mov_l, t) == SSF_UNALIGNED);
	t.cycles.c[1].actions = SS_ACTION_FETCH;

	//jump to an odd address
	t.cycles.c[3].fetch_addr = 0x0C000101;
	CHECK(ss_policy_filter(*jmp, t) == SSF_ODD_PC);
	t.cycles.c[3].fetch_addr = t.initial.s.PC + 6;

	//user mode
	CHECK(ss_policy_filter(*ldc_sr, t) == SSF_NONE);
	t.initial.s.SR = 0;
	CHECK(ss_policy_filter(*ldc_sr, t) == SSF_PRIVILEGED);
	CHECK(ss_policy_filter(*mov_l, t) == SSF_NONE);

	CHECK(ss_policy_filter(*trapa, t) == SSF_TRAPA_NOP);

	//FPU disabled
	CHECK(ss_policy_filter(*fadd, t) == SSF_NONE);
	t.initial.s.SR = 0x8000;
	CHECK(ss_policy_filter(*fadd, t) == SSF_FPU_DISABLED);
	CHECK(ss_policy_filter(*mov_l, t) == SSF_NONE);
	t.initial.s.SR = 0;

	//a NaN result
	t.initial.s.FP0[3] = 0x3F800000;
	t.final_.s.FP0[3] = 0x7FC00000;
	CHECK(ss_policy_filter(*fadd, t) == SSF_HOST_NAN);
	CHECK(ss_policy_filter(*fmov, t) == SSF_NONE);	//a NaN that is moved is not a result
	t.final_.s.FP0[3] = 0x7F800000;
	CHECK(ss_policy_filter(*fadd, t) == SSF_NONE);	//infinity
	t.final_.s.FP0[3] = 0x3F800000;

	t.initial.s.FP0[4] = 0x3FF00000;
	t.final_.s.FP0[4] = 0x7FF80000;
	CHECK(ss_policy_filter(*fadd_d, t) == SSF_HOST_NAN);
	t.final_.s.FP0[4] = 0x7FF00000;
	CHECK(ss_policy_filter(*fadd_d, t) == SSF_NONE);
	t.initial.s.FP0[4] = t.final_.s.FP0[4] = 0;

	//ftrc fr5,fpul
	t.opcodes.op[1] = 0xF53D;
	t.initial.s.FP0[5] = 0x7FC12345;
	CHECK(ss_policy_filter(*ftrc, t) == SSF_HOST_NAN);
	t.initial.s.FP0[5] = 0x4F000000;
	CHECK(ss_policy_filter(*ftrc, t) == SSF_NONE);
}

//// ss_state

static void test_state()
{
	SsState in, out;
	u32* words = (u32*)&in;

	for (u32 i = 0; i < SS_STATE_WORDS; i++)
		words[i] = 0x01010101 * (i + 1);

	in.SR = 0x700083F3;		//all the bits the register has, bank 1
	in.FPSCR = 0x003C0001;	//FR, SZ, PR, DN, round to zero

	ss_state_load(in);
	ss_state_save(out);

	CHECK(memcmp(&in, &out, sizeof(in)) == 0);

	//the active banks are where the CPU uses them
	CHECK(Sh4cntx.r[5] == in.R[5]);
	CHECK(Sh4cntx.r_bank[5] == in.R_[5]);
	CHECK(memcmp(&Sh4cntx.xffr[16 + 7], &in.FP0[7], 4) == 0);
	CHECK(memcmp(&Sh4cntx.xffr[7], &in.FP1[7], 4) == 0);

	SsCompare same = ss_state_compare(in, out);
	CHECK(same.exact && same.lenient && same.first_exact == -1);

	//one unit in the last place of an FP register
	in.FP0[2] = out.FP0[2] = 0x40000000;
	out.FP0[2] = in.FP0[2] + 1;
	SsCompare ulp = ss_state_compare(in, out);
	CHECK(!ulp.exact && ulp.lenient && ulp.first_exact == SS_STATE_FP_FIRST + 2 && ulp.first_lenient == -1);

	//an FP register that is off
	in.FP0[2] = 0x40000000;
	out.FP0[2] = 0x40400000;
	SsCompare off = ss_state_compare(in, out);
	CHECK(!off.exact && !off.lenient && off.first_lenient == SS_STATE_FP_FIRST + 2);
	out.FP0[2] = in.FP0[2];

	//anything else has to be exact
	out.R[3] = in.R[3] + 1;
	SsCompare reg = ss_state_compare(in, out);
	CHECK(!reg.exact && !reg.lenient && reg.first_exact == 3 && reg.first_lenient == 3);

	CHECK(ss_float_lenient(0x3F800000, 0x3F800000));
	CHECK(ss_float_lenient(0x00000000, 0x80000000));	//+0 and -0
	CHECK(ss_float_lenient(0x7FC00000, 0xFFC12345));	//two NaNs
	CHECK(ss_float_lenient(0x3F800004, 0x3F800000));
	CHECK(!ss_float_lenient(0x3F800005, 0x3F800000));
	CHECK(!ss_float_lenient(0x7FC00000, 0x7F800000));	//NaN and infinity
	CHECK(!ss_float_lenient(0x40000000, 0x3F800000));	//2.0 is not 1.0, whichever is the expected one
	CHECK(!ss_float_lenient(0x3F800000, 0x40000000));

	CHECK(strcmp(ss_state_word_name(0), "R0") == 0);
	CHECK(strcmp(ss_state_word_name(SS_STATE_FP_FIRST), "FR0") == 0);
	CHECK(strcmp(ss_state_word_name(SS_STATE_FP_LAST), "XF15") == 0);
	CHECK(strcmp(ss_state_word_name(SS_STATE_WORDS - 1), "FPUL") == 0);
}

int main()
{
	sh4_env_init();
	test_guard_init(400);

	test_guard();
	test_vmem_handlers();

	ss_mem_install();

	test_vmem_unaligned();
	test_memory();
	test_policy();
	test_state();

	printf("common.selftest: %u checks, %u failed\n", checks, failures);

	return failures ? 1 : 0;
}
