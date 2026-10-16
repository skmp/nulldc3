#include "vector_cases.h"

#include "sh4_fpu_approx.h"

#include <cstring>

const char* const vector_insn_name[VI_COUNT] = { "FIPR", "FTRV", "FSCA", "FSRRA" };

//// opcodes

//FIPR FVm,FVn: 1111nnmm11101101
u32 vector_fipr(u32 m, u32 n)
{
	return 0xF0ED | ((n / 4) << 10) | ((m / 4) << 8);
}

//FTRV XMTRX,FVn: 1111nn0111111101
u32 vector_ftrv(u32 n)
{
	return 0xF1FD | ((n / 4) << 10);
}

//FSCA FPUL,DRn: 1111nnn011111101
u32 vector_fsca(u32 n)
{
	return 0xF0FD | ((n / 2) << 9);
}

//FSRRA FRn: 1111nnnn01111101
u32 vector_fsrra(u32 n)
{
	return 0xF07D | (n << 8);
}

bool vector_decode(u32 opcode, VectorInsn& insn)
{
	if ((opcode & 0xF0FF) == 0xF0ED)
		insn = VI_FIPR;
	else if ((opcode & 0xF3FF) == 0xF1FD)
		insn = VI_FTRV;
	else if ((opcode & 0xF1FF) == 0xF0FD)
		insn = VI_FSCA;
	else if ((opcode & 0xF0FF) == 0xF07D)
		insn = VI_FSRRA;
	else
		return false;

	return true;
}

//The first register of FVn, DRn or FRn, and of FVm
static u32 reg_n(u32 opcode, VectorInsn insn)
{
	u32 n = (opcode >> 8) & 0xF;

	return insn == VI_FSRRA ? n : insn == VI_FSCA ? (n & 0xE) : (n & 0xC);
}

static u32 reg_m(u32 opcode)
{
	return ((opcode >> 8) & 3) * 4;
}


//// the model

void vector_reference(FpuCase& c)
{
	VectorInsn insn;

	c.after = c.before;
	c.trap = false;

	if (!vector_decode(c.opcode, insn))
		return;

	u32 n = reg_n(c.opcode, insn);
	u32 fpscr = c.before.fpscr;

	switch (insn)
	{
	case VI_FIPR:
		c.trap = sh4_fipr_ex(&c.before.fr[reg_m(c.opcode)], &c.before.fr[n], &fpscr, &c.after.fr[n + 3]) != 0;
		break;

	case VI_FTRV:
		c.trap = sh4_ftrv_ex(c.before.xf, &c.after.fr[n], &fpscr) != 0;
		break;

	case VI_FSCA:
		c.trap = sh4_fsca_ex(c.before.fpul, &fpscr, &c.after.fr[n], &c.after.fr[n + 1]) != 0;
		break;

	case VI_FSRRA:
		c.trap = sh4_fsrra_ex(c.before.fr[n], &fpscr, &c.after.fr[n]) != 0;
		break;

	default:
		break;
	}

	c.after.fpscr = fpscr;
}

void vector_describe(const FpuCase& c, char* text, size_t size)
{
	VectorInsn insn;

	text[0] = 0;

	if (!vector_decode(c.opcode, insn))
		return;

	u32 n = reg_n(c.opcode, insn);
	u32 m = reg_m(c.opcode);
	const u32* fr = c.before.fr;
	const u32* xf = c.before.xf;

	switch (insn)
	{
	case VI_FIPR:
		snprintf(text, size, "FIPR FV%u,FV%u: FV%u %08X %08X %08X %08X, FV%u %08X %08X %08X %08X", m, n, m, fr[m], fr[m + 1], fr[m + 2], fr[m + 3],
			n, fr[n], fr[n + 1], fr[n + 2], fr[n + 3]);
		break;

	case VI_FTRV:
		snprintf(text, size, "FTRV XMTRX,FV%u: XF0-XF15 %08X %08X %08X %08X  %08X %08X %08X %08X  %08X %08X %08X %08X  %08X %08X %08X %08X, FV%u "
			"%08X %08X %08X %08X", n, xf[0], xf[1], xf[2], xf[3], xf[4], xf[5], xf[6], xf[7], xf[8], xf[9], xf[10], xf[11], xf[12], xf[13],
			xf[14], xf[15], n, fr[n], fr[n + 1], fr[n + 2], fr[n + 3]);
		break;

	case VI_FSCA:
		snprintf(text, size, "FSCA FPUL,DR%u: FPUL %08X", n, c.before.fpul);
		break;

	case VI_FSRRA:
		snprintf(text, size, "FSRRA FR%u: FR%u %08X", n, n, fr[n]);
		break;

	default:
		break;
	}
}


//// generated operands

struct Rng
{
	u64 state;

	explicit Rng(u64 seed) : state(fpu_mix(seed)) { }

	u64 next() { state = fpu_mix(state); return state; }
	u32 below(u32 n) { return (u32)((next() >> 16) % n); }
	u32 range(u32 low, u32 high) { return low + below(high - low + 1); }
	u32 bit() { return (u32)(next() >> 40) & 1; }
};

static u32 single(u32 sign, u32 exponent, u32 fraction)
{
	return (sign << 31) | ((exponent & 0xFF) << 23) | (fraction & 0x7FFFFF);
}

//One bit, low ones, a small number, high ones, nothing
static u32 shaped_fraction(Rng& r)
{
	switch (r.below(5))
	{
	case 0:  return 1u << r.below(23);
	case 1:  return 0x7FFFFFu >> r.below(23);
	case 2:  return (u32)r.next() & 0xFF;
	case 3:  return (0x7FFFFFu << r.below(23)) & 0x7FFFFF;
	default: return 0;
	}
}

//Any 23 bits, one time in 8 a shaped one
static u32 fraction(Rng& r)
{
	return r.below(8) == 0 ? shaped_fraction(r) : (u32)(r.next() >> 20) & 0x7FFFFF;
}

//A number of either sign with this exponent. The draws are statements of their own all through this file: the
//order in which a call's arguments are evaluated is the compiler's choice
static u32 number(Rng& r, u32 exponent)
{
	u32 sign = r.bit();
	u32 f = fraction(r);

	return single(sign, exponent, f);
}

static u32 normal(Rng& r, u32 low, u32 high)
{
	u32 exponent = r.range(low, high);

	return number(r, exponent);
}

//<bits> significant bits behind the leading one
static u32 short_number(Rng& r, u32 low, u32 high, u32 bits)
{
	u32 sign = r.bit();
	u32 exponent = r.range(low, high);
	u32 f = (u32)r.next() & ((1u << bits) - 1);

	return single(sign, exponent, f << (23 - bits));
}

static u32 zero(Rng& r)
{
	return r.bit() << 31;
}

static u32 infinity(Rng& r)
{
	return single(r.bit(), 0xFF, 0);
}

static u32 denormal(Rng& r)
{
	u32 f = fraction(r);
	u32 sign = r.bit();

	return single(sign, 0, f ? f : 1);
}

//An SH4 NaN is signalling when bit 22 of the fraction is set
static u32 nan(Rng& r)
{
	u32 f = (u32)(r.next() >> 20) & 0x3FFFFF;

	u32 signalling = r.bit();
	u32 sign = r.bit();

	if (signalling)
		return single(sign, 0xFF, 0x400000 | f);

	return single(sign, 0xFF, f ? f : 1);
}

//The magnitude next to x, some units in the last place away
static u32 neighbour(Rng& r, u32 x)
{
	u32 step = r.below(5);

	return single(x >> 31, (x >> 23) & 0xFF, (x & 0x7FFFFF) + step - 2);
}

//Two exponents that add up to sum, both of normal numbers
static void split_exponent(Rng& r, u32 sum, u32& a, u32& b)
{
	u32 low = sum > 254 + 1 ? sum - 254 : 1;
	u32 high = sum - 1 < 254 ? sum - 1 : 254;

	a = r.range(low, high);
	b = sum - a;
}

//How one of the two factors of a product is made
enum Operands
{
	OP_NORMAL,		//numbers of about the same size: every product takes part in the sum
	OP_EXACT,		//short numbers: products and sum are exact in 24 bits
	OP_WIDE,		//sizes far apart: products are shifted out against each other
	OP_OVERFLOW,	//products at the top of the exponent range
	OP_UNDERFLOW,	//products at and below its bottom. The hardware drops a product whose exponents add up to 75 or less
	OP_ZERO,		//half of the factors are zeros of either sign
	OP_DENORMAL,	//denormals among the factors
	OP_INF,			//infinities among them, and zeros: inf * 0, inf - inf, sums of infinities
	OP_NAN,			//NaNs of both kinds among them
};

//One product's two factors
static void factors(Rng& r, Operands how, u32& a, u32& b)
{
	u32 ea, eb;

	switch (how)
	{
	case OP_NORMAL:
		a = normal(r, 119, 135);
		b = normal(r, 119, 135);
		break;

	case OP_EXACT:
		a = short_number(r, 124, 130, 6);
		b = short_number(r, 124, 130, 6);
		break;

	case OP_WIDE:
		a = normal(r, 67, 187);
		b = normal(r, 67, 187);
		break;

	case OP_OVERFLOW:
	case OP_UNDERFLOW:
		split_exponent(r, how == OP_OVERFLOW ? r.range(378, 386) : r.range(60, 135), ea, eb);
		a = number(r, ea);
		b = number(r, eb);
		break;

	case OP_ZERO:
	case OP_DENORMAL:
		for (u32 i = 0; i < 2; i++)
		{
			u32& x = i ? b : a;

			if (how == OP_ZERO)
				x = r.bit() ? zero(r) : normal(r, 119, 135);
			else
				x = r.below(4) == 0 ? denormal(r) : normal(r, 100, 200);
		}
		break;

	case OP_INF:
	case OP_NAN:
	default:
		for (u32 i = 0; i < 2; i++)
		{
			u32& x = i ? b : a;
			u32 k = r.below(8);

			if (k == 0)
				x = how == OP_NAN ? nan(r) : infinity(r);
			else if (k == 1)
				x = infinity(r);
			else if (k == 2)
				x = zero(r);
			else
				x = normal(r, 119, 135);
		}
		break;
	}
}

//The special value a class is about, for the cases that came out without one
static u32 special(Rng& r, Operands how)
{
	return how == OP_DENORMAL ? denormal(r) : how == OP_INF ? infinity(r) : nan(r);
}

static bool has_special(Operands how, u32 x)
{
	u32 exponent = (x >> 23) & 0xFF;
	u32 f = x & 0x7FFFFF;

	if (how == OP_DENORMAL)
		return exponent == 0 && f != 0;

	if (how == OP_INF)
		return exponent == 0xFF && f == 0;

	return exponent == 0xFF && f != 0;
}


//// FIPR

enum
{
	FIPR_NORMAL,
	FIPR_VEC3,		//the 4th product is zero: a dot product of 3 component vectors
	FIPR_SINGLE,	//one product
	FIPR_SELF,		//FIPR FVn,FVn: the square of a length
	FIPR_EXACT,
	FIPR_CANCEL,	//two products that cancel but for a few units in the last place
	FIPR_WIDE,
	FIPR_OVERFLOW,
	FIPR_UNDERFLOW,
	FIPR_ZERO,
	FIPR_DENORMAL,
	FIPR_INF,
	FIPR_NAN,

	FIPR_CLASSES
};

struct ClassInfo
{
	const char* name;
	u32 cases;
};

static const ClassInfo fipr_class[FIPR_CLASSES] =
{
	{ "normal", 64000 },
	{ "vec3", 24000 },
	{ "single", 24000 },
	{ "self", 16000 },
	{ "exact", 16000 },
	{ "cancel", 16000 },
	{ "wide", 16000 },
	{ "overflow", 8000 },
	{ "underflow", 8000 },
	{ "zero", 4000 },
	{ "denormal", 4000 },
	{ "inf", 4000 },
	{ "nan", 4000 },
};

static void generate_fipr(u32 cls, u32 index, Rng& r, FpuCase& c)
{
	//every pair of different vectors in turn
	u32 m = (index % 4) * 4;
	u32 n = ((index % 4 + 1 + (index / 4) % 3) % 4) * 4;

	if (cls == FIPR_SELF)
		n = m;

	u32 a[4], b[4];

	static const Operands how_of[FIPR_CLASSES] = { OP_NORMAL, OP_NORMAL, OP_WIDE, OP_NORMAL, OP_EXACT, OP_NORMAL, OP_WIDE, OP_OVERFLOW,
		OP_UNDERFLOW, OP_ZERO, OP_DENORMAL, OP_INF, OP_NAN };
	const Operands how = how_of[cls];

	for (u32 i = 0; i < 4; i++)
		factors(r, how, a[i], b[i]);

	switch (cls)
	{
	case FIPR_VEC3:
		if (r.below(3) != 0)
			a[3] = 0;
		else
			b[3] = 0;
		break;

	case FIPR_SINGLE:
	{
		u32 slot = r.below(4);

		for (u32 i = 0; i < 4; i++)
		{
			if (i != slot)
				a[i] = b[i] = 0;
		}
		break;
	}

	case FIPR_CANCEL:
		a[1] = neighbour(r, a[0]);
		b[1] = neighbour(r, b[0]) ^ 0x80000000;

		//what is left of the sum: nothing, or products that are small against the two
		for (u32 i = 2; i < 4; i++)
		{
			if (r.bit())
				a[i] = b[i] = 0;
			else
				a[i] = normal(r, 100, 118);
		}
		break;

	case FIPR_ZERO:
		//the first cases: nothing but zeros, in every combination of signs of two products
		if (index < 16)
		{
			for (u32 i = 0; i < 4; i++)
				a[i] = b[i] = 0;

			a[0] = (index & 1) << 31;
			b[0] = ((index >> 1) & 1) << 31;
			a[1] = ((index >> 2) & 1) << 31;
			b[1] = ((index >> 3) & 1) << 31;
		}
		break;

	case FIPR_DENORMAL:
	case FIPR_INF:
	case FIPR_NAN:
	{
		bool there = false;

		for (u32 i = 0; i < 4; i++)
			there = there || has_special(how, a[i]) || has_special(how, b[i]);

		if (!there)
		{
			u32* side = r.bit() ? a : b;
			u32 slot = r.below(4);

			side[slot] = special(r, how);
		}
		break;
	}

	default:
		break;
	}

	c.opcode = vector_fipr(m, n);

	for (u32 i = 0; i < 4; i++)
	{
		c.before.fr[n + i] = b[i];
		c.before.fr[m + i] = a[i];	//FIPR FVn,FVn: both operands are a
	}
}


//// FTRV

enum
{
	FTRV_NORMAL,
	FTRV_AFFINE,	//a transform as games set it up: a 3x3 part of numbers below 1, a translation, (x, y, z, 1)
	FTRV_DIAGONAL,	//a scaling: one product per row
	FTRV_EXACT,
	FTRV_CANCEL,
	FTRV_WIDE,
	FTRV_OVERFLOW,
	FTRV_UNDERFLOW,
	FTRV_ZERO,
	FTRV_DENORMAL,
	FTRV_INF,
	FTRV_NAN,

	FTRV_CLASSES
};

static const ClassInfo ftrv_class[FTRV_CLASSES] =
{
	{ "normal", 64000 },
	{ "affine", 48000 },
	{ "diagonal", 16000 },
	{ "exact", 16000 },
	{ "cancel", 16000 },
	{ "wide", 16000 },
	{ "overflow", 8000 },
	{ "underflow", 8000 },
	{ "zero", 4000 },
	{ "denormal", 4000 },
	{ "inf", 4000 },
	{ "nan", 4000 },
};

static void generate_ftrv(u32 cls, u32 index, Rng& r, FpuCase& c)
{
	u32 n = (index % 4) * 4;
	u32 matrix[16];		//XF0-XF15: matrix[4 * column + row]
	u32 v[4];

	static const Operands how_of[FTRV_CLASSES] = { OP_NORMAL, OP_NORMAL, OP_WIDE, OP_EXACT, OP_NORMAL, OP_WIDE, OP_OVERFLOW, OP_UNDERFLOW, OP_ZERO,
		OP_DENORMAL, OP_INF, OP_NAN };
	const Operands how = how_of[cls];

	//a column's products share the vector element: the element comes with the column's first product
	for (u32 column = 0; column < 4; column++)
	{
		factors(r, how, v[column], matrix[4 * column]);

		for (u32 row = 1; row < 4; row++)
		{
			u32 unused;

			factors(r, how, unused, matrix[4 * column + row]);
		}
	}

	switch (cls)
	{
	case FTRV_AFFINE:
		for (u32 column = 0; column < 4; column++)
		{
			for (u32 row = 0; row < 3; row++)
				matrix[4 * column + row] = column < 3 ? normal(r, 115, 126) : normal(r, 127, 137);

			matrix[4 * column + 3] = column < 3 ? 0 : 0x3F800000;
			v[column] = column < 3 ? normal(r, 120, 137) : 0x3F800000;
		}
		break;

	case FTRV_DIAGONAL:
		for (u32 i = 0; i < 16; i++)
		{
			if (i % 5 != 0)
				matrix[i] = 0;
		}
		break;

	case FTRV_CANCEL:
		v[1] = neighbour(r, v[0]);

		for (u32 row = 0; row < 4; row++)
		{
			matrix[4 + row] = neighbour(r, matrix[row]) ^ 0x80000000;

			for (u32 column = 2; column < 4; column++)
			{
				if (r.bit())
					matrix[4 * column + row] = 0;
				else
					matrix[4 * column + row] = normal(r, 100, 118);
			}
		}
		break;

	case FTRV_OVERFLOW:
	case FTRV_UNDERFLOW:
		//the exponents of a column's products follow the vector element
		for (u32 column = 0; column < 4; column++)
		{
			u32 ev = (v[column] >> 23) & 0xFF;

			for (u32 row = 0; row < 4; row++)
			{
				u32 sum = cls == FTRV_OVERFLOW ? r.range(378, 386) : r.range(60, 135);
				u32 em = sum > ev ? sum - ev : 1;

				if (em > 254)
					em = 254;

				matrix[4 * column + row] = number(r, em);
			}
		}
		break;

	case FTRV_DENORMAL:
	case FTRV_INF:
	case FTRV_NAN:
	{
		//of 20 numbers a few are special, and one at least
		u32 specials = 0;

		for (u32 i = 0; i < 20; i++)
		{
			u32& x = i < 16 ? matrix[i] : v[i - 16];

			if (has_special(how, x) && r.below(4) != 0)
				x = normal(r, 119, 135);

			specials += has_special(how, x);
		}

		if (specials == 0)
		{
			u32 i = r.below(20);

			(i < 16 ? matrix[i] : v[i - 16]) = special(r, how);
		}
		break;
	}

	default:
		break;
	}

	c.opcode = vector_ftrv(n);

	memcpy(c.before.xf, matrix, sizeof(matrix));
	memcpy(&c.before.fr[n], v, sizeof(v));
}


//// FSCA

enum
{
	FSCA_ANGLE = VECTOR_FSCA_EVERY_ANGLE,
	FSCA_UPPER,		//FPUL[31:16] is not part of the angle

	FSCA_CLASSES
};

static const ClassInfo fsca_class[FSCA_CLASSES] =
{
	{ "angle", 65536 },
	{ "upper", 8192 },
};

static void generate_fsca(u32 cls, u32 index, Rng& r, FpuCase& c)
{
	c.opcode = vector_fsca((index % 8) * 2);

	if (cls == FSCA_ANGLE)
	{
		c.before.fpul = index;
	}
	else
	{
		u32 upper = (u32)(r.next() >> 16) & 0xFFFF;

		u32 step = r.below(8);

		c.before.fpul = ((upper ? upper : 0x8000) << 16) | (index * 8 + step);
	}
}


//// FSRRA

enum
{
	FSRRA_NORMAL,
	FSRRA_ONE_TO_FOUR,	//[1, 4): the range every result of is on record
	FSRRA_SIMPLE,		//short fractions, powers of 2 among them
	FSRRA_ZERO,
	FSRRA_DENORMAL,
	FSRRA_NEGATIVE,
	FSRRA_INF,
	FSRRA_NAN,

	FSRRA_CLASSES
};

static const ClassInfo fsrra_class[FSRRA_CLASSES] =
{
	{ "normal", 128000 },
	{ "one-to-four", 32000 },
	{ "simple", 16000 },
	{ "zero", 32 },
	{ "denormal", 4000 },
	{ "negative", 4000 },
	{ "inf", 32 },
	{ "nan", 4000 },
};

static void generate_fsrra(u32 cls, u32 index, Rng& r, FpuCase& c)
{
	u32 n = index % 16;
	u32 x;

	switch (cls)
	{
	case FSRRA_NORMAL:      x = r.range(1, 254) << 23; x |= (u32)(r.next() >> 20) & 0x7FFFFF; break;
	case FSRRA_ONE_TO_FOUR: x = (127 + r.bit()) << 23; x |= (u32)(r.next() >> 20) & 0x7FFFFF; break;
	case FSRRA_SIMPLE:      x = r.range(1, 254) << 23; x |= shaped_fraction(r); break;
	case FSRRA_ZERO:        x = ((index / 16) & 1) << 31; break;
	case FSRRA_DENORMAL:    x = denormal(r); break;
	case FSRRA_NEGATIVE:    x = normal(r, 1, 254) | 0x80000000; break;
	case FSRRA_INF:         x = single((index / 16) & 1, 0xFF, 0); break;
	default:                x = nan(r); break;
	}

	c.opcode = vector_fsrra(n);
	c.before.fr[n] = x;
}


//// all of them

static const ClassInfo* const classes_of[VI_COUNT] = { fipr_class, ftrv_class, fsca_class, fsrra_class };

u32 vector_classes(VectorInsn insn)
{
	static const u32 count[VI_COUNT] = { FIPR_CLASSES, FTRV_CLASSES, FSCA_CLASSES, FSRRA_CLASSES };

	return count[insn];
}

const char* vector_class_name(VectorInsn insn, u32 cls)
{
	return classes_of[insn][cls].name;
}

u32 vector_class_cases(VectorInsn insn, u32 cls)
{
	return classes_of[insn][cls].cases;
}

void vector_generate(VectorInsn insn, u32 cls, u32 index, u32 fpscr, FpuCase& c)
{
	Rng r(((u64)(insn + 1) << 56) ^ ((u64)(cls + 1) << 48) ^ index);

	fpu_fill(c.before, fpscr);

	switch (insn)
	{
	case VI_FIPR:  generate_fipr(cls, index, r, c); break;
	case VI_FTRV:  generate_ftrv(cls, index, r, c); break;
	case VI_FSCA:  generate_fsca(cls, index, r, c); break;
	default:       generate_fsrra(cls, index, r, c); break;
	}

	c.after = c.before;
	c.trap = false;
}
