/*
	sh4.fpu.vector: FIPR, FTRV, FSCA and FSRRA against what the hardware computes.

	An SH4 does not compute these four the way IEEE arithmetic would: it has one block of four truncating
	multipliers, an adder and one rounding stage for all of them. shrike4-rtl's model of that block gives the
	console's answer for every capture that was taken (vector_cases.h), so the hardware's result of a case is the
	model's.

	  <instruction>.dn1.<rn|rz>.<class>            generated operands, in classes (vector_cases.cpp), with FPSCR =
	                                               0x00040000 (DN = 1, round to nearest) and 0x00040001 (round
	                                               to zero). The class "angle" of FSCA is every angle there is
	  <instruction>.console.values.<rn|rz>         the console's records that are results alone
	  <instruction>.console.specials.<setting>     the console's records with FPSCR and the exception
	                                               (vector_anchors.h), by FPSCR in front of the instruction: dn1,
	                                               dn0, or pr1 where PR = 1 makes the instruction do nothing. The
	                                               ones in which the console raised the FPU exception are in
	                                               <setting>.trap

	The model is only as good as the way it is called: which of FIPR's vectors is which, how the matrix is laid
	out, which register a result goes to. Before anything is run the model has to give the console's answer for
	every console record: the text files, and model/fipr/kos_fpu_dump/fsca_rnrz.bin, which has sine and cosine of
	all 65,536 angles in both rounding modes. A record it does not answer as the console did fails the test
	whatever the baseline says.
*/
#include "fpu_suite.h"
#include "vector_cases.h"
#include "vector_anchors.h"

#include <cstring>

#define FSCA_DUMP "model/fipr/kos_fpu_dump/fsca_rnrz.bin"
#define FSCA_ANGLES 65536

//DN = 1, as games run, in both rounding modes
static const u32 generated_fpscr[2] = { 0x00040000, 0x00040001 };
static const char* const rounding_name[2] = { "rn", "rz" };

struct Origin
{
	const char* name;	//a class, or a file
	u32 number;			//the case's index, or the line
	bool line;
};

static void label_case(const void* arg, const FpuCase& c, char* text, size_t size)
{
	const Origin& o = *(const Origin*)arg;

	int used = snprintf(text, size, o.line ? "%s:%u " : "%s #%u ", o.name, o.number);

	if (used > 0 && (size_t)used < size)
		vector_describe(c, text + used, size - used);
}

static bool same_values(const FpuRegs& a, const FpuRegs& b)
{
	return memcmp(a.fr, b.fr, sizeof(a.fr)) == 0 && memcmp(a.xf, b.xf, sizeof(a.xf)) == 0 && a.fpul == b.fpul && a.t == b.t;
}

//// why a value is not the hardware's (--why)

#define QNAN 0x7FBFFFFF		//what an SH4 makes when it makes a NaN

static bool is_nan(u32 x)
{
	return (x & 0x7F800000) == 0x7F800000 && (x & 0x007FFFFF) != 0;
}

static bool is_inf(u32 x)
{
	return (x & 0x7FFFFFFF) == 0x7F800000;
}

static bool is_denormal(u32 x)
{
	return (x & 0x7F800000) == 0 && (x & 0x007FFFFF) != 0;
}

//The registers with every denormal replaced by the zero of its sign: the registers no instruction of the case
//reads hold normal numbers. False: there is none
static bool flush_denormals(FpuRegs& r)
{
	bool any = false;

	for (u32 i = 0; i < 16; i++)
	{
		if (is_denormal(r.fr[i]))
		{
			r.fr[i] &= 0x80000000;
			any = true;
		}

		if (is_denormal(r.xf[i]))
		{
			r.xf[i] &= 0x80000000;
			any = true;
		}
	}

	return any;
}

static const char* why_value(const FpuCase& c, const FpuRegs& left, u32 engine)
{
	VectorInsn insn;

	if (!vector_decode(c.opcode, insn))
		return 0;

	//the first result register that differs
	u32 w = 0;

	while (w < 16 && left.fr[w] == c.after.fr[w])
		w++;

	if (w == 16)
		return 0;

	const u32 hardware = c.after.fr[w];
	const u32 is = left.fr[w];
	const bool rz = (c.before.fpscr & FPU_FPSCR_RM) == 1;

	if (insn == VI_FSCA)
	{
		//what the hardware computes for the angle when it rounds to nearest
		FpuCase nearest = c;

		nearest.before.fpscr &= ~FPU_FPSCR_RM;
		vector_reference(nearest);

		//the two things the table is known for: a -0 where the hardware has +0, and that it is the one table
		bool only_zeros = true;
		bool is_nearest = true;

		for (u32 i = 0; i < 16; i++)
		{
			bool minus_zero = left.fr[i] == 0x80000000 && nearest.after.fr[i] == 0;

			only_zeros = only_zeros && (left.fr[i] == c.after.fr[i] || (minus_zero && c.after.fr[i] == 0));
			is_nearest = is_nearest && (left.fr[i] == nearest.after.fr[i] || minus_zero);
		}

		if (only_zeros)
			return "FSCA: the sine of 0x8000 and the cosine of 0x4000 are -0 in the table, +0 on the hardware";

		if (rz && is_nearest)
			return "FSCA: the table has the hardware's round to nearest results, FPSCR.RM is not looked at";

		return 0;
	}

	if (hardware == QNAN && is_nan(is))
		return "NaN result: the host's bit pattern, the hardware's is always 7FBFFFFF";

	if (c.before.fpscr & FPU_FPSCR_DN)
	{
		//what the engine makes of the operands when the denormals among them are zeros
		FpuCase flushed = c;
		FpuRegs again;

		if (flush_denormals(flushed.before) && fpu_try(flushed, engine, again))
		{
			if (again.fr[w] == hardware)
				return "DN = 1: a denormal operand is not taken as the zero of its sign";

			if (hardware == QNAN && is_nan(again.fr[w]))
				return "DN = 1: a denormal operand is not taken as the zero of its sign (the NaN it then makes has the host's bit pattern too)";
		}
	}

	if (insn == VI_FSRRA)
		return "FSRRA: 1 / sqrtf(x) in two roundings, the hardware's result is that of its table and cubic";

	//FIPR and FTRV
	if (is_nan(is))
		return "sum of products: products that overflow to infinities of both signs add up to a NaN, the hardware adds the products before it rounds";

	if (hardware == QNAN)
		return "sum of products: the hardware's result is a NaN, the host's is not";

	if (is_inf(hardware) && is_inf(is))
		return "sum of products: infinities of one sign add up to an infinity with the sign of the first product on the hardware";

	if ((hardware & 0x7FFFFFFF) == 0 && (is & 0x7FFFFFFF) == 0)
		return "sum of products: the zero has the other sign (an exact zero is +0 on the hardware, an underflow keeps the sign of the sum)";

	if ((hardware >> 31) == (is >> 31) && !is_inf(hardware) && !is_inf(is))
	{
		u32 distance = hardware > is ? hardware - is : is - hardware;

		if (distance == 1)
			return "sum of products: host float arithmetic, one unit in the last place off the hardware's";

		return "sum of products: host float arithmetic, more than one unit in the last place off the hardware's";
	}

	return "sum of products: host float arithmetic, a result of another sign or an overflow on one side only";
}

//The console's records of a setting, in the order the groups are reported
enum
{
	AG_VALUES_RN,
	AG_VALUES_RZ,
	AG_DN1,
	AG_DN1_TRAP,
	AG_DN0,
	AG_DN0_TRAP,
	AG_PR1,
	AG_PR1_TRAP,

	ANCHOR_GROUPS
};

static const char* const anchor_group_name[ANCHOR_GROUPS] =
{
	"console.values.rn", "console.values.rz", "console.specials.dn1", "console.specials.dn1.trap", "console.specials.dn0",
	"console.specials.dn0.trap", "console.specials.pr1", "console.specials.pr1.trap",
};

static u32 anchor_group(const VectorAnchor& a)
{
	u32 fpscr = a.c.before.fpscr;

	if (!a.has_fpscr)
		return (fpscr & FPU_FPSCR_RM) ? AG_VALUES_RZ : AG_VALUES_RN;

	u32 setting = (fpscr & FPU_FPSCR_PR) ? AG_PR1 : (fpscr & FPU_FPSCR_DN) ? AG_DN1 : AG_DN0;

	return setting + (a.c.trap ? 1 : 0);
}

//The model on every console record. What it answers replaces the record's: it is the same, and has the registers
//and the FPSCR a record does not have. Returns the number of records it does not answer as the console did
static u32 check_anchors(std::vector<VectorAnchor>& anchors)
{
	std::vector<u32> records(vector_anchor_files() * VI_COUNT);
	std::vector<u32> agree(vector_anchor_files() * VI_COUNT);
	u32 wrong = 0;

	for (size_t i = 0; i < anchors.size(); i++)
	{
		VectorAnchor& a = anchors[i];
		FpuCase model = a.c;

		vector_reference(model);

		bool same = same_values(model.after, a.c.after);

		if (a.has_fpscr)
			same = same && model.after.fpscr == a.c.after.fpscr && model.trap == a.c.trap;

		records[a.file * VI_COUNT + a.insn]++;
		agree[a.file * VI_COUNT + a.insn] += same;

		if (!same && wrong++ == 0)
		{
			char text[512];

			vector_describe(a.c, text, sizeof(text));
			fpu_problem("the model does not answer as the console did, first %s:%u: %04X FPSCR %08X %s: console FPSCR %08X%s, model FPSCR %08X%s, "
				"or the registers differ", vector_anchor_file(a.file), a.line, a.c.opcode, a.c.before.fpscr, text, a.c.after.fpscr,
				a.c.trap ? " with the exception" : "", model.after.fpscr, model.trap ? " with the exception" : "");
		}

		if (same)
			a.c = model;
	}

	for (u32 file = 0; file < vector_anchor_files(); file++)
	{
		for (u32 insn = 0; insn < VI_COUNT; insn++)
		{
			u32 expected = vector_anchor_records(file, (VectorInsn)insn);
			u32 read = records[file * VI_COUNT + insn];

			if (read != expected)
			{
				fpu_problem("%s has %u records of %s, %u were read", vector_anchor_file(file), expected, vector_insn_name[insn], read);
				wrong++;
			}

			if (expected)
			{
				fpu_note("console %-42s %-5s the model answers as the console did in %6u of %6u records", vector_anchor_file(file),
					vector_insn_name[insn], agree[file * VI_COUNT + insn], read);
			}
		}
	}

	return wrong;
}

//The model on every angle against the console's sine and cosine: angle * 16 bytes of sine RN, cosine RN, sine
//RZ, cosine RZ. The cases are the class of FSCA that has every angle
static u32 check_fsca(const std::vector<u8>& dump)
{
	u32 agree = 0;
	u32 wrong = 0;

	for (u32 angle = 0; angle < FSCA_ANGLES; angle++)
	{
		for (u32 rz = 0; rz < 2; rz++)
		{
			FpuCase c;

			vector_generate(VI_FSCA, VECTOR_FSCA_EVERY_ANGLE, angle, generated_fpscr[rz], c);
			vector_reference(c);

			//FSCA FPUL,DRn
			u32 n = (c.opcode >> 8) & 0xE;
			u32 sine = fpu_le32(dump, (size_t)angle * 16 + rz * 8);
			u32 cosine = fpu_le32(dump, (size_t)angle * 16 + rz * 8 + 4);

			if (c.before.fpul == angle && c.after.fr[n] == sine && c.after.fr[n + 1] == cosine)
			{
				agree++;
			}
			else if (wrong++ == 0)
			{
				fpu_problem("the model does not answer as the console did, first " FSCA_DUMP ": angle %04X %s: console sine %08X cosine %08X, "
					"model %08X %08X", angle, rounding_name[rz], sine, cosine, c.after.fr[n], c.after.fr[n + 1]);
			}
		}
	}

	fpu_note("console %-42s %-5s the model answers as the console did in %6u of %6u results", FSCA_DUMP, "FSCA", agree, FSCA_ANGLES * 2);

	return wrong;
}

int fpu_vector_main(const FpuOptions& opt)
{
	const char* name = "sh4.fpu.vector";

	std::vector<VectorAnchor> anchors;
	std::vector<std::string> complaints;
	std::vector<u8> dump;
	std::string error;

	if (!vector_anchors_load(opt.data_dir, anchors, error, complaints))
		return fpu_suite_skip(name, opt, error + " (the console's records, shrike4-rtl model)");

	if (!fpu_read_file(opt.data_dir + "/" FSCA_DUMP, dump))
		return fpu_suite_skip(name, opt, "cannot read " + opt.data_dir + "/" FSCA_DUMP " (the console's records, shrike4-rtl model)");

	if (!fpu_suite_begin(name, opt))
		return FPU_EXIT_ERROR;

	for (size_t i = 0; i < complaints.size(); i++)
		fpu_problem("%s", complaints[i].c_str());

	if (dump.size() != FSCA_ANGLES * 16)
		fpu_problem("%s has %u bytes, sine and cosine of every angle in both rounding modes are %u", FSCA_DUMP, (u32)dump.size(), FSCA_ANGLES * 16);

	//the model against the console, before it is what the emulator is held against
	u32 wrong = check_anchors(anchors);

	wrong += check_fsca(dump);

	if (wrong)
		fpu_note("THE MODEL IS NOT CALLED THE WAY THE HARDWARE WORKS: the counts say nothing");

	fpu_suite_why(&why_value);

	u32 generated = 0;
	u32 unexpected_traps = 0;

	for (u32 insn = 0; insn < VI_COUNT; insn++)
	{
		const VectorInsn vi = (VectorInsn)insn;
		const std::string prefix = std::string(vector_insn_name[insn]) + ".";

		for (u32 rz = 0; rz < 2; rz++)
		{
			for (u32 cls = 0; cls < vector_classes(vi); cls++)
			{
				u32 group = fpu_group(prefix + "dn1." + rounding_name[rz] + "." + vector_class_name(vi, cls));
				u32 cases = vector_class_cases(vi, cls);

				generated += cases;

				if (!fpu_group_selected(group))
					continue;

				for (u32 index = 0; index < cases; index++)
				{
					FpuCase c;

					vector_generate(vi, cls, index, generated_fpscr[rz], c);
					vector_reference(c);

					//no enable is set
					if (c.trap && unexpected_traps++ == 0)
						fpu_problem("%s %s #%u: the model raises the FPU exception with FPSCR %08X", vector_insn_name[insn], vector_class_name(vi, cls),
							index, c.before.fpscr);

					Origin origin = { vector_class_name(vi, cls), index, false };

					fpu_run(group, c, &label_case, &origin);
				}
			}
		}

		//the console's records of the instruction
		u32 groups[ANCHOR_GROUPS];
		bool there[ANCHOR_GROUPS];

		memset(there, 0, sizeof(there));

		for (size_t i = 0; i < anchors.size(); i++)
		{
			if (anchors[i].insn == vi)
				there[anchor_group(anchors[i])] = true;
		}

		for (u32 g = 0; g < ANCHOR_GROUPS; g++)
		{
			if (there[g])
				groups[g] = fpu_group(prefix + anchor_group_name[g]);
		}

		for (size_t i = 0; i < anchors.size(); i++)
		{
			const VectorAnchor& a = anchors[i];

			if (a.insn != vi)
				continue;

			Origin origin = { vector_anchor_file(a.file), a.line, true };

			fpu_run(groups[anchor_group(a)], a.c, &label_case, &origin);
		}
	}

	fpu_note("generated cases: %u, console records: %u", generated, (u32)anchors.size());

	return fpu_suite_end();
}
