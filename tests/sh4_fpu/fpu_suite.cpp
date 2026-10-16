#include "fpu_suite.h"

#include "sh4_env.h"
#include "test_guard.h"
#include "dynarec_runner.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/mem/_vmem.h"

#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <map>
#include <xmmintrin.h>

#if defined(_WIN32)
#include <io.h>
#define NULL_DEVICE "NUL"
#define dup _dup
#define fdopen _fdopen
#define fileno _fileno
#else
#include <unistd.h>
#define NULL_DEVICE "/dev/null"
#endif

//hw/sh4/sh4_core.h declares these, along with macros for r, sr, fr, ... that are better kept out of here
void UpdateFPSCR();

//hw/sh4/sh4_core_regs.cpp: the FPSCR.RM and FPSCR.DN the host's control word was last loaded for
extern u32 old_rm;
extern u32 old_dn;

//A case is the instruction and a nop: the decoder ends a block in front of the instruction at which the block's
//cycles are used up, and an FPU instruction counts none
#define FPU_PC 0x8C010000
#define FPU_NOP 0x0009
#define FPU_INSTRUCTIONS 2

//A guarded call that takes this long is a hang
#define FPU_WATCHDOG_MS 2000

//MXCSR without the exception flags, which every operation may set
#define MXCSR_CONTROL 0xFFFFFFC0u

static const char* const engine_name[FPU_ENGINES] = { "interpreter", "dynarec" };

enum
{
	FACET_VALUE,							//+ engine
	FACET_FLAGS = FACET_VALUE + FPU_ENGINES,	//+ engine
	FACET_AGREE = FACET_FLAGS + FPU_ENGINES,

	FACETS
};

struct Counts
{
	u32 total;
	u32 trap;
	u32 value[FPU_ENGINES];
	u32 flags[FPU_ENGINES];
	u32 agree;
	u32 native;
	u32 crash;
};

//value and flags of each engine
#define WHYS (FACET_AGREE)

typedef std::map<std::string, u32> Reasons;

struct Group
{
	std::string name;
	bool selected;
	bool excepted;				//left out by --except
	Counts counts;
	std::string first[FACETS];	//the first case that is not as on the hardware, or in which the engines differ
	Reasons why[WHYS];			//--why: the reasons of the cases that are not as on the hardware
};

//What an engine made of a case
struct Outcome
{
	TestGuardResult guard;
	std::string message;	//how it ended, if it did not run through
	FpuRegs regs;
	u32 pc;
	bool native;
};

FILE* fpu_report;

static std::string test_name;
static std::string baseline_name;	//the test's own name: the baseline is its
static FpuOptions options;
static std::vector<Group> groups;
static std::map<std::string, u32> group_index;
static std::map<std::string, Counts> baseline;
static std::vector<std::string> baseline_order;
static std::vector<std::string> notes;
static std::vector<std::string> problems;
static std::chrono::steady_clock::time_point started;

static u32 host_control[2][2];	//[FPSCR.RM][FPSCR.DN]: what SetFloatStatusReg() loads into MXCSR
static u64 host_checks;
static u32 host_mismatches;
static std::string host_first;

static FpuWhyFP* test_why;

static u32 hangs;
static u32 notified;
static std::string first_notification;
static u32 unsupported;


//// guest memory: the instruction and nops, and nothing else

static u32 handler_ctx;
static u32 opcode_under_test;

static u32 opcode_at(u32 addr)
{
	return addr == FPU_PC ? opcode_under_test : FPU_NOP;
}

static void serve_opcodes(bool on)
{
}

static const DynarecCode code = { &opcode_at, &serve_opcodes };

static void check_ctx(void* ctx)
{
	if (ctx != &handler_ctx)
		test_guard_abort("a memory handler was called with a context that is not the one it is registered with");
}

static u32 data_access(void* ctx, const char* kind, u32 size, u32 addr)
{
	check_ctx(ctx);
	test_guard_abort("%s%u @%08X: the instructions of the FPU tests access no memory", kind, size * 8, addr);

	return 0;
}

static u8 DYNACALL read8(void* ctx, u32 addr) { return (u8)data_access(ctx, "read", 1, addr); }
static u16 DYNACALL read16(void* ctx, u32 addr) { check_ctx(ctx); return (u16)opcode_at(addr); }
static u32 DYNACALL read32(void* ctx, u32 addr) { return data_access(ctx, "read", 4, addr); }

static void DYNACALL write8(void* ctx, u32 addr, u8 data) { data_access(ctx, "write", 1, addr); }
static void DYNACALL write16(void* ctx, u32 addr, u16 data) { data_access(ctx, "write", 2, addr); }
static void DYNACALL write32(void* ctx, u32 addr, u32 data) { data_access(ctx, "write", 4, addr); }


//// the two engines

static void load(const FpuCase& c)
{
	Sh4Context& ctx = Sh4cntx;

	memset(&ctx, 0, sizeof(ctx));

	memcpy(&ctx.xffr[16], c.before.fr, sizeof(c.before.fr));
	memcpy(&ctx.xffr[0], c.before.xf, sizeof(c.before.xf));
	ctx.fpul = c.before.fpul;
	ctx.pc = FPU_PC;

	//privileged, FPU enabled
	sh4_sr_SetFull(0x40000000 | (c.before.t & 1));
	ctx.old_sr.status = ctx.sr.status;

	//LDS Rm,FPSCR
	ctx.fpscr.full = c.before.fpscr;
	UpdateFPSCR();

	ctx.CpuRunning = 1;
	ctx.sh4_sched_next = 0x7FFFFFFF;
}

static void save(Outcome& o)
{
	const Sh4Context& ctx = Sh4cntx;

	memcpy(o.regs.fr, &ctx.xffr[16], sizeof(o.regs.fr));
	memcpy(o.regs.xf, &ctx.xffr[0], sizeof(o.regs.xf));
	o.regs.fpul = ctx.fpul;
	o.regs.fpscr = ctx.fpscr.full;
	o.regs.t = ctx.sr.T;
	o.pc = ctx.pc;
}

//SH4IInterpreter::Loop, one instruction at a time
static void run_interpreter(void*)
{
	for (u32 i = 0; i < FPU_INSTRUCTIONS; i++)
	{
		u32 addr = Sh4cntx.pc;
		Sh4cntx.pc += 2;

		u32 op = IReadMem16(addr);

		ExecuteOpcode(op);
	}
}

static void run_dynarec(void*)
{
	u32 executed[FPU_INSTRUCTIONS];

	dynarec_run(code, FPU_INSTRUCTIONS, executed);

	if (executed[0] != FPU_PC)
		test_guard_abort("the instruction was run from %08X", executed[0]);
}

static u32 expected_control(u32 fpscr)
{
	return host_control[fpscr & 1][(fpscr & FPU_FPSCR_DN) ? 1 : 0];
}

static bool counted = true;		//the case that runs is one of the report

static void check_host(const FpuCase& c, u32 control, const char* when, u32 engine)
{
	if (!counted)
		return;

	host_checks++;

	if ((control & MXCSR_CONTROL) == expected_control(c.before.fpscr))
		return;

	if (host_mismatches++ == 0)
	{
		char text[256];

		snprintf(text, sizeof(text), "%s the %s runs %04X with FPSCR %08X the host's MXCSR is %04X, SetFloatStatusReg() loads %04X for it", when,
			engine_name[engine], c.opcode, c.before.fpscr, control & MXCSR_CONTROL, expected_control(c.before.fpscr));
		host_first = text;
	}
}

static void run_engine(const FpuCase& c, u32 engine, Outcome& o)
{
	load(c);

	u32 control = _mm_getcsr();

	check_host(c, control, "before", engine);

	o.guard = test_guard_run(engine == FPU_INTERPRETER ? &run_interpreter : &run_dynarec, 0);
	o.native = engine == FPU_DYNAREC && dynarec_native(0);

	if (o.guard == TGR_OK)
	{
		check_host(c, _mm_getcsr(), "after", engine);

		if (counted && test_guard_msgbox_count() != 0 && notified++ == 0)
			first_notification = test_guard_last_msgbox();
	}
	else
	{
		o.message = test_guard_message();

		//a signal handler runs on a control word of its own and the guard leaves it without returning
		_mm_setcsr(control);
	}

	save(o);
}


//// the host's FP state against FPSCR

//Loads the four FPSCR settings the way a case does, each from a cache that knows nothing, and keeps what
//SetFloatStatusReg() makes of them. The rounding the host then does has to be the one FPSCR.RM asks for
static bool check_host_modes()
{
	bool ok = true;

	for (u32 rm = 0; rm < 2; rm++)
	{
		for (u32 dn = 0; dn < 2; dn++)
		{
			FpuCase c;

			memset(&c, 0, sizeof(c));
			c.before.fpscr = rm | (dn ? FPU_FPSCR_DN : 0);

			old_rm = old_dn = 0xFF;
			load(c);

			u32 control = _mm_getcsr();
			host_control[rm][dn] = control & MXCSR_CONTROL;

			//1 / 3 is 0x3EAAAAAA.AAAA...: nearest rounds it up, toward zero cuts it
			volatile float one = 1.0f;
			volatile float three = 3.0f;
			volatile float quotient = one / three;
			float q = quotient;
			u32 bits;

			memcpy(&bits, &q, sizeof(bits));

			u32 want = rm == 1 ? 0x3EAAAAAA : 0x3EAAAAAB;

			if (((control >> 13) & 3) != (rm == 1 ? 3u : 0u) || bits != want)
			{
				fprintf(stderr, "FPSCR.RM = %u: the host rounds 1/3 to %08X (MXCSR %04X), an SH4 to %08X\n", rm, bits, control, want);
				ok = false;
			}

			//an unmasked host exception would be a signal in the middle of a case
			if ((control & 0x1F80) != 0x1F80)
			{
				fprintf(stderr, "FPSCR %08X: SetFloatStatusReg() leaves host FP exceptions unmasked (MXCSR %04X)\n", c.before.fpscr, control);
				ok = false;
			}
		}
	}

	return ok;
}


//// baseline

static bool load_baseline(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "r");

	if (!f)
		return false;

	char line[512];

	while (fgets(line, sizeof(line), f))
	{
		char name[128];
		Counts c;

		if (line[0] == '#')
			continue;

		if (sscanf(line, "%127s %u %u %u %u %u %u %u %u %u", name, &c.total, &c.trap, &c.value[FPU_INTERPRETER], &c.value[FPU_DYNAREC],
				&c.flags[FPU_INTERPRETER], &c.flags[FPU_DYNAREC], &c.agree, &c.native, &c.crash) == 10)
		{
			baseline[name] = c;
			baseline_order.push_back(name);
		}
	}

	fclose(f);

	return true;
}

static bool save_baseline(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "w");

	if (!f)
		return false;

	fprintf(f, "# %s baseline: what nullDC's SH4 interpreter and dynarec leave of the hardware's results, per group.\n", baseline_name.c_str());
	fprintf(f, "#   total     cases, of which\n");
	fprintf(f, "#   trap      the hardware raises the FPU exception and writes no result\n");
	fprintf(f, "#   value-i   the interpreter leaves the hardware's FR, XF, FPUL and T        value-d   the dynarec does\n");
	fprintf(f, "#   flags-i   the interpreter leaves the hardware's FPSCR                     flags-d   the dynarec does\n");
	fprintf(f, "#   agree     interpreter and dynarec leave the same\n");
	fprintf(f, "#   native    the dynarec compiled the instruction to native code\n");
	fprintf(f, "#   crash     an engine ends in die(), verify() or a signal\n");
	fprintf(f, "# The test fails when a group has fewer cases than listed here in value, flags, agree or native, or more in\n");
	fprintf(f, "# crash. KNOWN_FAILURES.md has the reason for every count that is below its total.\n");
	fprintf(f, "# Written by the test's --update-baseline, see README.md.\n");
	fprintf(f, "#\n");
	fprintf(f, "# %-34s %7s %6s %7s %7s %7s %7s %7s %7s %5s\n", "group", "total", "trap", "value-i", "value-d", "flags-i", "flags-d", "agree", "native",
		"crash");

	for (size_t i = 0; i < groups.size(); i++)
	{
		const Counts& c = groups[i].counts;

		fprintf(f, "%-36s %7u %6u %7u %7u %7u %7u %7u %7u %5u\n", groups[i].name.c_str(), c.total, c.trap, c.value[FPU_INTERPRETER],
			c.value[FPU_DYNAREC], c.flags[FPU_INTERPRETER], c.flags[FPU_DYNAREC], c.agree, c.native, c.crash);
	}

	fclose(f);

	return true;
}


//// options, begin

bool fpu_suite_options(int argc, char** argv, int first, const char* usage_head, FpuOptions& opt)
{
	const FpuOptions defaults = opt;

	opt.update_baseline = false;
	opt.strict = false;
	opt.require_data = false;
	opt.verbose = false;
	opt.first = false;
	opt.why = false;
	opt.emu_output = false;

	bool ok = first <= argc;

	for (int i = first; i < argc && ok; i++)
	{
		std::string arg = argv[i];
		bool has_value = i + 1 < argc;

		if (arg == "--shrike4" && has_value)
			opt.data_dir = argv[++i];
		else if (arg == "--group" && has_value)
			opt.select = argv[++i];
		else if (arg == "--except" && has_value)
			opt.except = argv[++i];
		else if (arg == "--name" && has_value)
			opt.name = argv[++i];
		else if (arg == "--baseline" && has_value)
			opt.baseline = argv[++i];
		else if (arg == "--update-baseline")
			opt.update_baseline = true;
		else if (arg == "--strict")
			opt.strict = true;
		else if (arg == "--require-data")
			opt.require_data = true;
		else if (arg == "--verbose")
			opt.verbose = true;
		else if (arg == "--first")
			opt.first = true;
		else if (arg == "--why")
			opt.why = true;
		else if (arg == "--emu-output")
			opt.emu_output = true;
		else
			ok = false;
	}

	if (ok && opt.update_baseline && (!opt.select.empty() || !opt.except.empty()))
	{
		fprintf(stderr, "--update-baseline needs a run of every group\n\n");
		ok = false;
	}

	if (!ok)
	{
		fprintf(stderr, "%s\n\n", usage_head);
		fprintf(stderr,
			"  --shrike4 <dir>      the shrike4-rtl checkout with the console's records, default %s\n"
			"  --group <pattern>    only the groups that have <pattern> in their name, or match it if it has * or ?,\n"
			"                       no baseline check\n"
			"  --except <pattern>   not the groups that match <pattern>. The others are compared with the baseline\n"
			"  --name <name>        what the report calls the test, for another build of the dynarec\n"
			"  --baseline <file>    baseline file, default %s\n"
			"  --update-baseline    write the counts of this run into the baseline file\n"
			"  --strict             counts above the baseline fail as well: the baseline has to be the one of this\n"
			"                       build. ctest runs the test this way\n"
			"  --require-data       test data that is not there is an error. Without it the test says that it is\n"
			"                       skipped and exits with %d\n"
			"  --first              per group, the first case that is not as on the hardware in value and in flags,\n"
			"                       and the first one in which the engines differ\n"
			"  --verbose            every such case\n"
			"  --why                per group and over all of them, the reasons the cases that are not as on the\n"
			"                       hardware have, counted\n"
			"  --emu-output         do not hide what the emulator code prints\n",
			defaults.data_dir.empty() ? "(none)" : defaults.data_dir.c_str(), defaults.baseline.c_str(), FPU_EXIT_SKIPPED);
	}

	return ok;
}

bool fpu_suite_begin(const char* name, const FpuOptions& opt)
{
	test_name = opt.name.empty() ? name : opt.name;
	baseline_name = name;
	options = opt;

	if (!load_baseline(opt.baseline) && !opt.update_baseline && opt.select.empty())
	{
		fprintf(stderr, "cannot read the baseline %s\n", opt.baseline.c_str());
		return false;
	}

	//The emulator code prints as it goes. The report keeps the real stdout
	fpu_report = stdout;

	if (!opt.emu_output)
	{
		fflush(stdout);
		fpu_report = fdopen(dup(fileno(stdout)), "w");

		if (!fpu_report || !freopen(NULL_DEVICE, "w", stdout))
		{
			fprintf(stderr, "cannot redirect the emulator output\n");
			return false;
		}
	}

	test_guard_echo_msgbox(opt.emu_output);

	sh4_env_init();

	_vmem_handler handler = _vmem_register_handler(&handler_ctx, read8, read16, read32, write8, write16, write32);
	_vmem_map_handler(handler, 0x00, 0xFF);

	dynarec_init();

	if (!check_host_modes())
		return false;

	test_guard_init(FPU_WATCHDOG_MS);

	started = std::chrono::steady_clock::now();

	fprintf(fpu_report, "%s\n", test_name.c_str());
	fprintf(fpu_report, "host MXCSR by FPSCR, as SetFloatStatusReg() loads it: RN %04X, RZ %04X, DN+RN %04X, DN+RZ %04X\n", host_control[0][0],
		host_control[1][0], host_control[0][1], host_control[1][1]);

#if defined(__GNUC__)
	//the x64 backend has other code for FIPR on a CPU without SSE3, and calls a helper for FMAC on one without FMA
	fprintf(fpu_report, "host CPU: FMA %s, SSE3 %s. They pick the dynarec's code for FMAC and FIPR, the results are the same\n",
		__builtin_cpu_supports("fma") ? "yes" : "NO", __builtin_cpu_supports("sse3") ? "yes" : "NO");
#endif

	return true;
}


//// groups and cases

//* and ? wildcards
static bool glob(const char* pattern, const char* text)
{
	if (*pattern == 0)
		return *text == 0;

	if (*pattern == '*')
		return glob(pattern + 1, text) || (*text != 0 && glob(pattern, text + 1));

	return *text != 0 && (*pattern == '?' || *pattern == *text) && glob(pattern + 1, text + 1);
}

//A pattern without * or ? matches anywhere in the name
static bool matches(const std::string& pattern, const std::string& name)
{
	if (pattern.find_first_of("*?") == std::string::npos)
		return name.find(pattern) != std::string::npos;

	return glob(pattern.c_str(), name.c_str());
}

u32 fpu_group(const std::string& name)
{
	std::map<std::string, u32>::const_iterator it = group_index.find(name);

	if (it != group_index.end())
		return it->second;

	Group g;

	g.name = name;
	memset(&g.counts, 0, sizeof(g.counts));

	g.excepted = !options.except.empty() && matches(options.except, name);
	g.selected = !g.excepted && (options.select.empty() || matches(options.select, name));

	groups.push_back(g);
	group_index[name] = (u32)groups.size() - 1;

	return (u32)groups.size() - 1;
}

bool fpu_group_selected(u32 group)
{
	return groups[group].selected;
}

void fpu_fill(FpuRegs& regs, u32 fpscr)
{
	//normal numbers with a full mantissa, different in every register
	for (u32 i = 0; i < 16; i++)
	{
		regs.fr[i] = 0x40A5C3E1 + i * 0x01010101;
		regs.xf[i] = 0xC15A3C1E + i * 0x01010101;
	}

	regs.fpul = 0x5A5A5A5A;
	regs.fpscr = fpscr;
	regs.t = 1;
}

static std::string strf(const char* fmt, ...)
{
	char text[1536];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	return text;
}

//FR, XF, FPUL and T
#define VALUE_WORDS 34

static u32 value_word(const FpuRegs& r, u32 w)
{
	return w < 16 ? r.fr[w] : w < 32 ? r.xf[w - 16] : w == 32 ? r.fpul : r.t;
}

static std::string value_word_name(u32 w)
{
	return w < 16 ? strf("FR%u", w) : w < 32 ? strf("XF%u", w - 16) : w == 32 ? "FPUL" : "T";
}

static bool same_values(const FpuRegs& a, const FpuRegs& b)
{
	return memcmp(a.fr, b.fr, sizeof(a.fr)) == 0 && memcmp(a.xf, b.xf, sizeof(a.xf)) == 0 && a.fpul == b.fpul && a.t == b.t;
}

//"FR0 <a_name> 7FC00000, <b_name> 7FBFFFFF"
static std::string value_difference(const FpuRegs& a, const char* a_name, const FpuRegs& b, const char* b_name)
{
	std::string text;
	u32 words = 0;

	for (u32 w = 0; w < VALUE_WORDS; w++)
	{
		u32 x = value_word(a, w);
		u32 y = value_word(b, w);

		if (x == y)
			continue;

		if (words < 4)
			text += strf("%s%s %s %08X, %s %08X", words ? "; " : "", value_word_name(w).c_str(), a_name, x, b_name, y);

		words++;
	}

	if (words > 4)
		text += strf(" and %u more registers", words - 4);

	return text;
}

static std::string ended(const Outcome& o)
{
	std::string message = o.message;

	//verify() and die() name the source file with the path of the checkout
	size_t dir = message.find("/libswirl/");
	size_t path = dir == std::string::npos ? dir : message.rfind(' ', dir);

	if (path != std::string::npos)
		message.erase(path + 1, dir - path);

	return strf("ends in %s (%s)", test_guard_result_name(o.guard), message.c_str());
}

static bool went_on(const Outcome& o)
{
	return o.pc == FPU_PC + FPU_INSTRUCTIONS * 2;
}

void fpu_suite_why(FpuWhyFP* why)
{
	test_why = why;
}

bool fpu_try(const FpuCase& c, u32 engine, FpuRegs& left)
{
	Outcome o;

	const u32 opcode = opcode_under_test;

	opcode_under_test = c.opcode;
	counted = false;

	run_engine(c, engine, o);

	counted = true;
	opcode_under_test = opcode;

	left = o.regs;

	return o.guard == TGR_OK && went_on(o);
}

//Why the values an engine left are not the hardware's
static std::string why_value(const FpuCase& c, const Outcome& o, u32 engine)
{
	if (o.guard != TGR_OK)
		return ended(o);

	if (c.trap && went_on(o))
		return "the hardware raises the FPU exception and writes no result, the engine has no FPU exception and carries the instruction out";

	if (!went_on(o))
		return c.trap ? "the engine leaves the instruction's path as the hardware does, the registers are not the hardware's"
			: "the engine leaves the instruction's path, the hardware raises no exception";

	const char* why = test_why ? test_why(c, o.regs, engine) : 0;

	return why ? why : "NOT EXPLAINED";
}

//Why the FPSCR an engine left is not the hardware's
static std::string why_flags(const FpuCase& c, const Outcome& o)
{
	if (o.guard != TGR_OK)
		return ended(o);

	if (o.regs.fpscr != c.before.fpscr)
		return "the engine writes FPSCR, and not what the hardware writes";

	u32 cause = (c.after.fpscr >> 12) & 0x3F;

	if (cause == 0)
		return "FPSCR is not written: the hardware clears the cause field, nothing is raised";

	std::string raised;

	for (u32 bit = 0; bit < 6; bit++)
	{
		if (cause & (0x20 >> bit))
			raised += "EVZOUI"[bit];
	}

	return "FPSCR is not written: the hardware raises " + raised;
}

void fpu_run(u32 group, const FpuCase& c, FpuLabelFP* label, const void* label_arg)
{
	Group& g = groups[group];

	if (!g.selected)
		return;

	//FR swaps the banks when it is loaded and the decoder cannot be entered with RM = 2 or 3. No case needs them
	if (c.before.fpscr & (FPU_FPSCR_FR | 2))
	{
		unsupported++;
		return;
	}

	opcode_under_test = c.opcode;

	Outcome o[FPU_ENGINES];
	bool ran = true;

	for (u32 e = 0; e < FPU_ENGINES; e++)
	{
		run_engine(c, e, o[e]);

		if (o[e].guard == TGR_TIMEOUT)
			hangs++;

		ran = ran && o[e].guard == TGR_OK;
	}

	const Outcome& i = o[FPU_INTERPRETER];
	const Outcome& d = o[FPU_DYNAREC];

	bool ok[FACETS];
	std::string why[FACETS];

	for (u32 e = 0; e < FPU_ENGINES; e++)
	{
		bool run_through = o[e].guard == TGR_OK;
		bool on_path = run_through && went_on(o[e]) == !c.trap;

		ok[FACET_VALUE + e] = on_path && same_values(o[e].regs, c.after);
		ok[FACET_FLAGS + e] = run_through && o[e].regs.fpscr == c.after.fpscr;
	}

	if (ran)
		ok[FACET_AGREE] = same_values(i.regs, d.regs) && i.regs.fpscr == d.regs.fpscr && i.pc == d.pc;
	else
		ok[FACET_AGREE] = i.guard == d.guard && i.message == d.message;

	g.counts.total++;
	g.counts.trap += c.trap;
	g.counts.agree += ok[FACET_AGREE];
	g.counts.native += d.native;
	g.counts.crash += !ran;

	bool as_hardware = true;

	for (u32 e = 0; e < FPU_ENGINES; e++)
	{
		g.counts.value[e] += ok[FACET_VALUE + e];
		g.counts.flags[e] += ok[FACET_FLAGS + e];
		as_hardware = as_hardware && ok[FACET_VALUE + e] && ok[FACET_FLAGS + e];
	}

	if (options.why)
	{
		for (u32 e = 0; e < FPU_ENGINES; e++)
		{
			if (!ok[FACET_VALUE + e])
				g.why[FACET_VALUE + e][why_value(c, o[e], e)]++;

			if (!ok[FACET_FLAGS + e])
				g.why[FACET_FLAGS + e][why_flags(c, o[e])]++;
		}
	}

	if (as_hardware && ok[FACET_AGREE])
		return;

	//the report's lines: only for the first case of the group that needs them, or for all of them
	bool wanted = options.verbose;

	for (u32 f = 0; f < FACETS; f++)
		wanted = wanted || (!ok[f] && g.first[f].empty());

	if (!wanted)
		return;

	char text[512];

	text[0] = 0;
	if (label)
		label(label_arg, c, text, sizeof(text));

	std::string head = strf("%04X FPSCR %08X %s", c.opcode, c.before.fpscr, text);

	for (u32 e = 0; e < FPU_ENGINES; e++)
	{
		if (!ok[FACET_VALUE + e])
		{
			std::string& w = why[FACET_VALUE + e];

			if (o[e].guard != TGR_OK)
				w = ended(o[e]);
			else if (c.trap && went_on(o[e]))
				w = "the hardware raises the FPU exception and writes nothing, the engine goes on to the next instruction";
			else if (!c.trap && !went_on(o[e]))
				w = strf("the engine leaves the instruction's path (PC %08X), the hardware raises no exception", o[e].pc);

			std::string values = value_difference(o[e].regs, "is", c.after, "hardware");

			if (o[e].guard == TGR_OK && !values.empty())
				w += (w.empty() ? "" : "; ") + values;
		}

		if (!ok[FACET_FLAGS + e])
		{
			if (o[e].guard != TGR_OK)
				why[FACET_FLAGS + e] = ended(o[e]);
			else
				why[FACET_FLAGS + e] = strf("FPSCR is %08X, hardware %08X", o[e].regs.fpscr, c.after.fpscr);
		}
	}

	if (!ok[FACET_AGREE])
	{
		std::string& w = why[FACET_AGREE];

		if (ran)
		{
			w = value_difference(i.regs, "interpreter", d.regs, "dynarec");

			if (i.regs.fpscr != d.regs.fpscr)
				w += strf("%sFPSCR interpreter %08X, dynarec %08X", w.empty() ? "" : "; ", i.regs.fpscr, d.regs.fpscr);

			if (i.pc != d.pc)
				w += strf("%sPC interpreter %08X, dynarec %08X", w.empty() ? "" : "; ", i.pc, d.pc);
		}
		else
		{
			w = strf("the interpreter %s, the dynarec %s", i.guard == TGR_OK ? "runs through" : ended(i).c_str(),
				d.guard == TGR_OK ? "runs through" : ended(d).c_str());
		}
	}

	static const char* const facet_name[FACETS] = { "value interpreter", "value dynarec", "flags interpreter", "flags dynarec", "agree" };

	for (u32 f = 0; f < FACETS; f++)
	{
		if (ok[f])
			continue;

		std::string line = strf("%-17s %s: %s", facet_name[f], head.c_str(), why[f].c_str());

		if (g.first[f].empty())
			g.first[f] = line;

		if (options.verbose)
			fprintf(fpu_report, "%s  %s\n", g.name.c_str(), line.c_str());
	}
}

void fpu_note(const char* fmt, ...)
{
	char text[1536];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	notes.push_back(text);
}

void fpu_problem(const char* fmt, ...)
{
	char text[1536];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	problems.push_back(text);
}


//// the end

static void print_counts(const char* name, const Counts& c)
{
	fprintf(fpu_report, "%-36s %7u %6u %7u %7u %7u %7u %7u %7u %5u\n", name, c.total, c.trap, c.value[FPU_INTERPRETER], c.value[FPU_DYNAREC],
		c.flags[FPU_INTERPRETER], c.flags[FPU_DYNAREC], c.agree, c.native, c.crash);
}

static const char* const why_name[WHYS] = { "value interpreter", "value dynarec", "flags interpreter", "flags dynarec" };

static void print_reasons(const char* indent, const char* what, const Reasons& reasons)
{
	for (Reasons::const_iterator it = reasons.begin(); it != reasons.end(); ++it)
		fprintf(fpu_report, "%s%-17s %7u  %s\n", indent, what, it->second, it->first.c_str());
}

//The reasons of a group, or of all of them. What both engines have the same counts of is printed once
static void print_why(const char* indent, const Reasons* why)
{
	for (u32 f = 0; f < WHYS; f += FPU_ENGINES)
	{
		if (why[f] == why[f + 1])
		{
			print_reasons(indent, f == FACET_VALUE ? "value" : "flags", why[f]);
			continue;
		}

		for (u32 e = 0; e < FPU_ENGINES; e++)
			print_reasons(indent, why_name[f + e], why[f + e]);
	}
}

static void print_first(const Group& g)
{
	for (u32 f = 0; f < FACETS; f++)
	{
		if (!g.first[f].empty())
			fprintf(fpu_report, "    %s\n", g.first[f].c_str());
	}
}

int fpu_suite_end()
{
	//the emulator code is done: the report is not written in its rounding mode
	_mm_setcsr(0x1F80);

	bool gated = options.select.empty() && !options.update_baseline;
	u32 improvements = 0;
	std::vector<std::string> gate;
	Counts sum;
	Reasons all_why[WHYS];

	memset(&sum, 0, sizeof(sum));

	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

	fprintf(fpu_report, "\n%-36s %7s %6s %7s %7s %7s %7s %7s %7s %5s\n", "group", "total", "trap", "value-i", "value-d", "flags-i", "flags-d", "agree",
		"native", "crash");

	u32 run = 0;
	u32 excepted = 0;

	for (size_t n = 0; n < groups.size(); n++)
	{
		const Group& g = groups[n];
		const Counts& c = g.counts;

		excepted += g.excepted;

		if (!g.selected)
			continue;

		run++;

		print_counts(g.name.c_str(), c);

		sum.total += c.total;
		sum.trap += c.trap;
		sum.agree += c.agree;
		sum.native += c.native;
		sum.crash += c.crash;

		for (u32 e = 0; e < FPU_ENGINES; e++)
		{
			sum.value[e] += c.value[e];
			sum.flags[e] += c.flags[e];
		}

		bool worse = false;

		if (gated)
		{
			std::map<std::string, Counts>::const_iterator it = baseline.find(g.name);

			if (it == baseline.end())
			{
				gate.push_back(strf("FAIL %s: not in the baseline", g.name.c_str()));
				worse = true;
			}
			else if (c.total != it->second.total || c.trap != it->second.trap)
			{
				gate.push_back(strf("FAIL %s: has %u cases, %u of them traps, the baseline was made with %u and %u", g.name.c_str(), c.total, c.trap,
					it->second.total, it->second.trap));
				worse = true;
			}
			else
			{
				const Counts& b = it->second;

//A count that is worse than its baseline fails. One that is better says that the baseline is behind, which
//fails with --strict
#define GATE(what, now, was, more_is_worse) \
				if ((more_is_worse) ? (now) > (was) : (now) < (was)) \
				{ \
					gate.push_back(strf("FAIL %s: " what " %u, baseline %u", g.name.c_str(), (now), (was))); \
					worse = true; \
				} \
				else if ((now) != (was)) \
				{ \
					gate.push_back(strf("%s%s the baseline: %s " what " %u, baseline %u", options.strict ? "FAIL " : "", \
						(now) > (was) ? "above" : "below", g.name.c_str(), (now), (was))); \
					improvements++; \
				}

				GATE("value-i", c.value[FPU_INTERPRETER], b.value[FPU_INTERPRETER], false);
				GATE("value-d", c.value[FPU_DYNAREC], b.value[FPU_DYNAREC], false);
				GATE("flags-i", c.flags[FPU_INTERPRETER], b.flags[FPU_INTERPRETER], false);
				GATE("flags-d", c.flags[FPU_DYNAREC], b.flags[FPU_DYNAREC], false);
				GATE("agree", c.agree, b.agree, false);
				GATE("native", c.native, b.native, false);
				GATE("crash", c.crash, b.crash, true);

#undef GATE
			}
		}

		if (options.first || worse)
			print_first(g);

		if (options.why)
		{
			print_why("    why ", g.why);

			for (u32 f = 0; f < WHYS; f++)
			{
				for (Reasons::const_iterator it = g.why[f].begin(); it != g.why[f].end(); ++it)
					all_why[f][it->first] += it->second;
			}
		}

		if (c.total == 0)
			fpu_problem("%s: the group has no cases", g.name.c_str());
	}

	if (run == 0 && problems.empty())
	{
		if (options.select.empty())
			fprintf(fpu_report, "\n--except %s leaves no group\n", options.except.c_str());
		else
			fprintf(fpu_report, "\nno group matches %s\n", options.select.c_str());

		return FPU_EXIT_ERROR;
	}

	if (gated)
	{
		for (size_t n = 0; n < baseline_order.size(); n++)
		{
			if (group_index.find(baseline_order[n]) == group_index.end())
				gate.push_back(strf("FAIL %s: in the baseline, but the run has no such group", baseline_order[n].c_str()));
		}
	}

	fprintf(fpu_report, "\n");
	print_counts(strf("%u groups, %.2f s", run, seconds).c_str(), sum);

	if (!options.except.empty())
	{
		fprintf(fpu_report, "\n%u groups are not run: --except %s\n", excepted, options.except.c_str());

		//an exception that is not needed any more must not stay
		if (!excepted)
			fpu_problem("--except %s matches no group", options.except.c_str());
	}

	if (options.why)
	{
		fprintf(fpu_report, "\nwhy cases are not as on the hardware, over all groups:\n");
		print_why("  ", all_why);
	}

	fprintf(fpu_report, "\nhost MXCSR read back %llu times, around every case of each engine: %u are not the emulator's for the case's FPSCR\n",
		(unsigned long long)host_checks, host_mismatches);

	if (!notes.empty())
		fprintf(fpu_report, "\n");

	for (size_t n = 0; n < notes.size(); n++)
		fprintf(fpu_report, "%s\n", notes[n].c_str());

	if (host_mismatches)
		fpu_problem("the host's control word is not the emulator's: %s", host_first.c_str());

	if (hangs)
		fpu_problem("%u cases hang", hangs);

	if (notified)
		fpu_problem("the emulator code raises a notification (msgboxf) and carries on in %u cases, first: %s", notified, first_notification.c_str());

	if (unsupported)
		fpu_problem("%u cases set FPSCR.FR or a reserved rounding mode, which the tests do not run", unsupported);

	if (!gate.empty() || !problems.empty())
		fprintf(fpu_report, "\n");

	for (size_t n = 0; n < gate.size(); n++)
		fprintf(fpu_report, "%s\n", gate[n].c_str());

	for (size_t n = 0; n < problems.size(); n++)
		fprintf(fpu_report, "FAIL %s\n", problems[n].c_str());

	u32 failures = (u32)problems.size();

	for (size_t n = 0; n < gate.size(); n++)
		failures += gate[n].compare(0, 5, "FAIL ") == 0;

	if (options.update_baseline)
	{
		if (!problems.empty())
		{
			fprintf(fpu_report, "\nthe baseline is not written: the run has problems that no baseline covers\n");
		}
		else if (!save_baseline(options.baseline))
		{
			fprintf(fpu_report, "\ncannot write %s\n", options.baseline.c_str());
			return FPU_EXIT_ERROR;
		}
		else
		{
			fprintf(fpu_report, "\nbaseline written to %s\n", options.baseline.c_str());
		}
	}
	else if (improvements)
	{
		fprintf(fpu_report, "\n%u counts are better than the baseline%s: run with --update-baseline and commit %s\n", improvements,
			options.strict ? ", which fails with --strict" : "", options.baseline.c_str());
	}

	//Only a run that was compared with the baseline passes
	const char* verdict = "PASS";

	if (failures)
		verdict = "FAIL";
	else if (options.update_baseline)
		verdict = "not gated (the baseline was written)";
	else if (!gated)
		verdict = "not gated (not compared with the baseline)";

	fprintf(fpu_report, "\n%s: %s\n", test_name.c_str(), verdict);
	fflush(fpu_report);

	return failures ? FPU_EXIT_FAIL : FPU_EXIT_PASS;
}

int fpu_suite_skip(const char* name, const FpuOptions& opt, const std::string& reason)
{
	if (!opt.name.empty())
		name = opt.name.c_str();

	if (opt.require_data)
	{
		fprintf(stderr, "MISSING: %s: %s\n", name, reason.c_str());
		return FPU_EXIT_ERROR;
	}

	printf("SKIPPED: %s: %s\n", name, reason.c_str());

	return FPU_EXIT_SKIPPED;
}


//// helpers

bool fpu_read_file(const std::string& path, std::vector<u8>& data)
{
	data.clear();

	FILE* f = fopen(path.c_str(), "rb");

	if (!f)
		return false;

	u8 buffer[65536];
	size_t count;

	while ((count = fread(buffer, 1, sizeof(buffer), f)) > 0)
		data.insert(data.end(), buffer, buffer + count);

	bool ok = !ferror(f);

	fclose(f);

	return ok;
}

u32 fpu_le32(const std::vector<u8>& data, size_t offset)
{
	if (offset + 4 > data.size())
		return 0;

	return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | ((u32)data[offset + 3] << 24);
}

u64 fpu_mix(u64 x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;

	return x ^ (x >> 31);
}
