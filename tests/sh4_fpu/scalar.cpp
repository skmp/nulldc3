/*
	sh4.fpu.scalar: the scalar FPU instructions against what a Dreamcast answered.

	shrike4-rtl's tools/hw/fpdn has 225,036 records of single instructions run on the console: FR0-FR3, FPUL,
	FPSCR and T behind the instruction, and whether it raised the FPU exception. The operands are not in the
	files, fpdn_vec.h makes them from a record's position, as it did for the console program:
	  part 1   every instruction on every combination of a palette of 14 values (normal numbers, zeros,
	           infinities, both kinds of NaN, denormals, the smallest and the largest normal number), under 7
	           FPSCR settings: DN = 0 with no enable, with EN.V, EN.Z, EN.I and with every cause and flag set,
	           DN = 1, and DN = 0 with RM = round to zero
	  part 2   4,096 operand sets per instruction whose result is in or next to the denormal range, and
	           compares of denormals, with DN = 0 in both rounding modes and with DN = 1
	  part 3   the double precision instructions on a palette of 21 values that tells the two words of a
	           double apart, under 6 of the settings of part 1
	An instruction has its operands in FR0, FR1 (and FR2 for FMAC), DR0 and DR2, or FPUL, and T is 1 in front
	of it.

	The groups are <instruction>.<dn0|dn1>.<rn|rz>.<class>. The class of a record is the first of these that one
	of the operands the instruction reads is: nan, denormal, inf, zero, normal. Records in which the console
	raised the FPU exception are not in these classes but in a group of their own, <instruction>.<dn>.<rm>.trap.
*/
#include "fpu_suite.h"

#include "fpdn_vec.h"

#include <cstring>

#define RECORD_SIZE 32	//fpdn_rec_t: FR0-FR3, FPUL, FPSCR, T, exception code

enum
{
	CLASS_NORMAL,
	CLASS_ZERO,
	CLASS_INF,
	CLASS_DENORMAL,
	CLASS_NAN,
	CLASS_TRAP,

	CLASSES
};

static const char* const class_name[CLASSES] = { "normal", "zero", "inf", "denormal", "nan", "trap" };

//What a failing case is called in the report
struct Label
{
	const char* part;
	int op;
	u32 index;			//of the record in its file
	const char* names;	//of the operands, where they come from a palette
};

static u32 class_single(u32 v)
{
	u32 exponent = (v >> 23) & 0xFF;
	u32 fraction = v & 0x7FFFFF;

	if (exponent == 0xFF)
		return fraction ? CLASS_NAN : CLASS_INF;

	if (exponent == 0)
		return fraction ? CLASS_DENORMAL : CLASS_ZERO;

	return CLASS_NORMAL;
}

static u32 class_double(u32 high, u32 low)
{
	u32 exponent = (high >> 20) & 0x7FF;
	bool fraction = (high & 0xFFFFF) != 0 || low != 0;

	if (exponent == 0x7FF)
		return fraction ? CLASS_NAN : CLASS_INF;

	if (exponent == 0)
		return fraction ? CLASS_DENORMAL : CLASS_ZERO;

	return CLASS_NORMAL;
}

//The class of the operands an instruction of this kind reads. The enum is in the order that decides
static u32 class_operands(int kind, const u32 in[5])
{
	u32 a = CLASS_NORMAL;
	u32 b = CLASS_NORMAL;
	u32 c = CLASS_NORMAL;

	switch (kind)
	{
	case K_MAC:
		c = class_single(in[2]);
		//fall through
	case K_BIN_S:
		b = class_single(in[1]);
		//fall through
	case K_UN_S:
		a = class_single(in[0]);
		break;

	case K_BIN_D:
		b = class_double(in[2], in[3]);
		//fall through
	case K_UN_D:
		a = class_double(in[0], in[1]);
		break;

	case K_CNVSD:
		a = class_single(in[4]);
		break;
	}

	u32 rv = a > b ? a : b;

	return rv > c ? rv : c;
}

static void label_case(const void* arg, const FpuCase& c, char* text, size_t size)
{
	const Label& l = *(const Label*)arg;

	snprintf(text, size, "%s %s #%u%s%s FR0-FR3 %08X %08X %08X %08X FPUL %08X", l.part, FPDN_OPS[l.op].name, l.index, l.names[0] ? " " : "",
		l.names, c.before.fr[0], c.before.fr[1], c.before.fr[2], c.before.fr[3], c.before.fpul);
}

//// why a value is not the hardware's (--why)

#define QNAN_SINGLE     0x7FBFFFFF	//what an SH4 makes when it makes a NaN
#define QNAN_HIGH       0x7FF7FFFF
#define QNAN_LOW        0xFFFFFFFF

//Where an instruction leaves its result
enum
{
	RESULT_FR0,		//single
	RESULT_FR2,		//single: FMAC
	RESULT_DR0,		//double: FR0 is the high word
	RESULT_FPUL,	//FTRC: integer, FCNVDS: single
	RESULT_T,		//FCMP
};

static int op_index(const FpuCase& c)
{
	for (int op = 0; op < P_NOPS; op++)
	{
		if (FPDN_OPS[op].opw == c.opcode && (FPDN_OPS[op].pr != 0) == ((c.before.fpscr & FPU_FPSCR_PR) != 0))
			return op;
	}

	return -1;
}

static u32 result_place(int op)
{
	switch (op)
	{
	case P_FCMPEQ_S: case P_FCMPGT_S: case P_FCMPEQ_D: case P_FCMPGT_D:
		return RESULT_T;

	case P_FTRC_S: case P_FTRC_D: case P_FCNVDS:
		return RESULT_FPUL;

	case P_FMAC_S:
		return RESULT_FR2;

	default:
		return FPDN_OPS[op].pr ? RESULT_DR0 : RESULT_FR0;
	}
}

static bool same_result(u32 result, const FpuRegs& a, const FpuRegs& b)
{
	switch (result)
	{
	case RESULT_FR0:  return a.fr[0] == b.fr[0];
	case RESULT_FR2:  return a.fr[2] == b.fr[2];
	case RESULT_DR0:  return a.fr[0] == b.fr[0] && a.fr[1] == b.fr[1];
	case RESULT_FPUL: return a.fpul == b.fpul;
	default:          return a.t == b.t;
	}
}

//The result is a floating point number, and a NaN
static bool nan_result(int op, u32 result, const FpuRegs& r)
{
	switch (result)
	{
	case RESULT_FR0:  return class_single(r.fr[0]) == CLASS_NAN;
	case RESULT_FR2:  return class_single(r.fr[2]) == CLASS_NAN;
	case RESULT_DR0:  return class_double(r.fr[0], r.fr[1]) == CLASS_NAN;
	case RESULT_FPUL: return op == P_FCNVDS && class_single(r.fpul) == CLASS_NAN;
	default:          return false;
	}
}

//The NaN is the one an SH4 makes
static bool sh4_nan_result(u32 result, const FpuRegs& r)
{
	switch (result)
	{
	case RESULT_FR0:  return r.fr[0] == QNAN_SINGLE;
	case RESULT_FR2:  return r.fr[2] == QNAN_SINGLE;
	case RESULT_DR0:  return r.fr[0] == QNAN_HIGH && r.fr[1] == QNAN_LOW;
	default:          return r.fpul == QNAN_SINGLE;
	}
}

//The result is the smallest normal number in one and the zero of its sign in the other
static bool smallest_normal_and_zero(u32 result, const FpuRegs& hardware, const FpuRegs& left)
{
	switch (result)
	{
	case RESULT_FR0:  return (hardware.fr[0] & 0x7FFFFFFF) == 0x00800000 && left.fr[0] == (hardware.fr[0] & 0x80000000);
	case RESULT_FR2:  return (hardware.fr[2] & 0x7FFFFFFF) == 0x00800000 && left.fr[2] == (hardware.fr[2] & 0x80000000);
	case RESULT_FPUL: return (hardware.fpul & 0x7FFFFFFF) == 0x00800000 && left.fpul == (hardware.fpul & 0x80000000);
	case RESULT_DR0:  return (hardware.fr[0] & 0x7FFFFFFF) == 0x00100000 && hardware.fr[1] == 0 && left.fr[0] == (hardware.fr[0] & 0x80000000) &&
		left.fr[1] == 0;
	default:          return false;
	}
}

//The operands of the instruction with every denormal replaced by the zero of its sign. False: there is none
static bool flush_denormals(int kind, FpuRegs& r)
{
	bool any = false;

	switch (kind)
	{
	case K_BIN_S: case K_MAC: case K_UN_S:
		for (u32 i = 0; i < (kind == K_MAC ? 3u : kind == K_BIN_S ? 2u : 1u); i++)
		{
			if (class_single(r.fr[i]) == CLASS_DENORMAL)
			{
				r.fr[i] &= 0x80000000;
				any = true;
			}
		}
		break;

	case K_BIN_D: case K_UN_D:
		for (u32 i = 0; i < (kind == K_BIN_D ? 4u : 2u); i += 2)
		{
			if (class_double(r.fr[i], r.fr[i + 1]) == CLASS_DENORMAL)
			{
				r.fr[i] &= 0x80000000;
				r.fr[i + 1] = 0;
				any = true;
			}
		}
		break;

	default:
		if (class_single(r.fpul) == CLASS_DENORMAL)
		{
			r.fpul &= 0x80000000;
			any = true;
		}
		break;
	}

	return any;
}

//A double precision denormal that has all of its fraction in the low word
static bool low_word_denormal(int kind, const FpuRegs& r)
{
	if (kind != K_BIN_D && kind != K_UN_D)
		return false;

	for (u32 i = 0; i < (kind == K_BIN_D ? 4u : 2u); i += 2)
	{
		if ((r.fr[i] & 0x7FFFFFFF) == 0 && r.fr[i + 1] != 0)
			return true;
	}

	return false;
}

static const char* why_value(const FpuCase& c, const FpuRegs& left, u32 engine)
{
	const int op = op_index(c);

	if (op < 0)
		return 0;

	const int kind = FPDN_OPS[op].kind;
	const u32 result = result_place(op);
	const FpuRegs& hardware = c.after;
	const bool dn = (c.before.fpscr & FPU_FPSCR_DN) != 0;

	if (op == P_FTRC_S || op == P_FTRC_D)
	{
		bool nan = (op == P_FTRC_S ? class_single(c.before.fr[0]) : class_double(c.before.fr[0], c.before.fr[1])) == CLASS_NAN;

		if (nan && hardware.fpul == 0x80000000 && left.fpul == 0x7FFFFFFF)
			return "FTRC of a NaN is 7FFFFFFF, the hardware's is 80000000";

		if (hardware.fpul == 0x7FFFFFFF && left.fpul == 0x7FFFFF80)
			return "FTRC of a number above the range (+inf too) is 7FFFFF80, the hardware's is 7FFFFFFF";

		return 0;
	}

	if (!same_result(result, left, hardware) && nan_result(op, result, left) && sh4_nan_result(result, hardware))
		return "NaN result: the host's bit pattern, the hardware's is always 7FBFFFFF / 7FF7FFFF FFFFFFFF";

	if (dn)
	{
		//what the engine makes of the operands when the denormals among them are zeros
		FpuCase flushed = c;
		FpuRegs again;

		if (flush_denormals(kind, flushed.before) && fpu_try(flushed, engine, again))
		{
			if (same_result(result, again, hardware))
				return "DN = 1: a denormal operand is not taken as the zero of its sign";

			if (nan_result(op, result, again) && sh4_nan_result(result, hardware))
				return "DN = 1: a denormal operand is not taken as the zero of its sign (the NaN it then makes has the host's bit pattern too)";
		}

		if (smallest_normal_and_zero(result, hardware, left))
			return "DN = 1: a result below the smallest normal number that rounds up to it is that number on the hardware, the host flushes it to zero";
	}
	else if (low_word_denormal(kind, c.before))
	{
		return "DN = 0, double: a denormal with nothing in its high word, which the hardware tells by the high word alone (FDIV: x / 0 and "
			"x / x of such an x are the zero of the sign, no exception)";
	}

	if (op == P_FSRRA_S && class_single(c.before.fr[0]) == CLASS_NORMAL && !(c.before.fr[0] & 0x80000000))
		return "FSRRA: 1 / sqrtf(x), the hardware's result is that of its approximation (sh4.fpu.vector)";

	return 0;
}

static u32 groups[P_NOPS][2][2][CLASSES];	//[instruction][DN][RM][class]
static u32 group_cases[P_NOPS][2][2][CLASSES];
static bool counting;	//the first pass over the records: which groups there are

static void run_record(const char* part, int op, u32 cfg, const u32 in[5], const std::vector<u8>& records, u32 index, const char* names)
{
	const fpdn_op_t& o = FPDN_OPS[op];
	const size_t at = (size_t)index * RECORD_SIZE;

	u32 exception = fpu_le32(records, at + 28);
	u32 dn = (cfg & FPU_FPSCR_DN) ? 1 : 0;
	u32 rm = cfg & FPU_FPSCR_RM;
	u32 cls = exception ? CLASS_TRAP : class_operands(o.kind, in);

	if (counting)
	{
		if (exception != 0 && exception != 0x120)
			fpu_problem("%s record %u: exception code %X, the tests know the FPU exception (120) only", part, index, exception);

		if (rm > 1)
			fpu_problem("%s record %u: FPSCR %08X has a reserved rounding mode", part, index, cfg);
		else
			group_cases[op][dn][rm][cls]++;

		return;
	}

	if (rm > 1)
		return;

	FpuCase c;

	c.opcode = o.opw;

	fpu_fill(c.before, cfg | (o.pr ? FPU_FPSCR_PR : 0));
	for (u32 i = 0; i < 4; i++)
		c.before.fr[i] = in[i];
	c.before.fpul = in[4];
	c.before.t = 1;

	c.after = c.before;
	for (u32 i = 0; i < 4; i++)
		c.after.fr[i] = fpu_le32(records, at + i * 4);
	c.after.fpul = fpu_le32(records, at + 16);
	c.after.fpscr = fpu_le32(records, at + 20);
	c.after.t = fpu_le32(records, at + 24);
	c.trap = exception != 0;

	Label label = { part, op, index, names };

	fpu_run(groups[op][dn][rm][cls], c, &label_case, &label);
}

//The groups in the order of the report: by instruction, then DN = 0 in both rounding modes and DN = 1, then by class
static void make_groups()
{
	static const u32 settings[3][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 } };		//DN, RM

	for (int op = 0; op < P_NOPS; op++)
	{
		for (u32 s = 0; s < 3; s++)
		{
			u32 dn = settings[s][0];
			u32 rm = settings[s][1];

			for (u32 cls = 0; cls < CLASSES; cls++)
			{
				char name[96];

				if (group_cases[op][dn][rm][cls] == 0)
					continue;

				snprintf(name, sizeof(name), "%s.dn%u.%s.%s", FPDN_OPS[op].name, dn, rm ? "rz" : "rn", class_name[cls]);
				groups[op][dn][rm][cls] = fpu_group(name);
			}
		}
	}

	//no record is of DN = 1 with round to zero
	for (int op = 0; op < P_NOPS; op++)
	{
		for (u32 cls = 0; cls < CLASSES; cls++)
		{
			if (group_cases[op][1][1][cls] != 0)
				fpu_problem("%s has records with DN = 1 and round to zero, the test has no group for them", FPDN_OPS[op].name);
		}
	}
}

//The records of a file, which has to have as many as fpdn_vec.h says. False: it has not
static bool read_records(const std::string& dir, const char* file, u32 count, std::vector<u8>& records, std::string& error)
{
	std::string path = dir + "/" + file;

	if (!fpu_read_file(path, records))
	{
		error = "cannot read " + path;
		return false;
	}

	if (records.size() != (size_t)count * RECORD_SIZE)
	{
		char text[160];

		snprintf(text, sizeof(text), " has %u bytes, fpdn_vec.h describes %u records of %u bytes", (u32)records.size(), count, RECORD_SIZE);
		error = path + text;
		return false;
	}

	return true;
}

int fpu_scalar_main(const FpuOptions& opt)
{
	const char* name = "sh4.fpu.scalar";
	const std::string dir = opt.data_dir + "/tools/hw/fpdn";

	u32 p1_count = 0;
	u32 p3_count = 0;

	for (int op = 0; op < P_NOPS; op++)
		p1_count += FPDN_P1_NCFG * fpdn_p1_rows(op);

	for (int k = 0; k < FPDN_P3_NOPS; k++)
		p3_count += FPDN_P3_NCFG * fpdn_p3_rows(FPDN_P3_OP[k]);

	const u32 p2_count = V_NOPS * FPDN_P2_NCFG * FPDN_P2_N;

	std::vector<u8> p1, p2, p3;
	std::string error;

	if (!read_records(dir, "fpdn_p1.bin", p1_count, p1, error) || !read_records(dir, "fpdn_p2.bin", p2_count, p2, error) ||
		!read_records(dir, "fpdn_p3.bin", p3_count, p3, error))
	{
		return fpu_suite_skip(name, opt, error + " (the console's records, shrike4-rtl tools/hw/fpdn)");
	}

	if (!fpu_suite_begin(name, opt))
		return FPU_EXIT_ERROR;

	fpu_suite_why(&why_value);

	fpu_note("console records of %s: part 1 %u, part 2 %u, part 3 %u, %u in all", dir.c_str(), p1_count, p2_count, p3_count,
		p1_count + p2_count + p3_count);

	//twice: for the groups there are, and then to run them
	for (u32 pass = 0; pass < 2; pass++)
	{
		counting = pass == 0;

		u32 in[5];
		u32 index = 0;
		char names[64];

		//the order of the records is the order of the console program's loops
		for (int op = 0; op < P_NOPS; op++)
		{
			for (int cfg = 0; cfg < FPDN_P1_NCFG; cfg++)
			{
				for (int row = 0; row < fpdn_p1_rows(op); row++, index++)
				{
					int kind = FPDN_OPS[op].kind;
					bool two = kind == K_BIN_S || kind == K_BIN_D || kind == K_MAC;

					fpdn_p1_inputs(op, row, in);

					snprintf(names, sizeof(names), "[%s%s%s%s%s]", FPDN_PAL_NAME[row % FPDN_NPAL], two ? " " : "",
						two ? FPDN_PAL_NAME[(row / FPDN_NPAL) % FPDN_NPAL] : "", kind == K_MAC ? " " : "",
						kind == K_MAC ? FPDN_PAL_NAME[row / (FPDN_NPAL * FPDN_NPAL)] : "");

					run_record("part 1", op, FPDN_P1_CFG[cfg], in, p1, index, names);
				}
			}
		}

		index = 0;

		for (int v = 0; v < V_NOPS; v++)
		{
			for (int cfg = 0; cfg < FPDN_P2_NCFG; cfg++)
			{
				for (u32 i = 0; i < FPDN_P2_N; i++, index++)
				{
					fpdn_p2_inputs(v, i, in);
					run_record("part 2", FPDN_VOP[v], FPDN_P2_CFG[cfg], in, p2, index, "");
				}
			}
		}

		index = 0;

		for (int k = 0; k < FPDN_P3_NOPS; k++)
		{
			const int op = FPDN_P3_OP[k];

			for (int cfg = 0; cfg < FPDN_P3_NCFG; cfg++)
			{
				for (int row = 0; row < fpdn_p3_rows(op); row++, index++)
				{
					bool two = FPDN_OPS[op].kind == K_BIN_D;

					fpdn_p3_inputs(op, row, in);

					snprintf(names, sizeof(names), "[%s%s%s]", FPDN_PAL3_NAME[row % FPDN_NPAL3], two ? " " : "",
						two ? FPDN_PAL3_NAME[row / FPDN_NPAL3] : "");

					run_record("part 3", op, FPDN_P3_CFG[cfg], in, p3, index, names);
				}
			}
		}

		if (counting)
			make_groups();
	}

	return fpu_suite_end();
}
