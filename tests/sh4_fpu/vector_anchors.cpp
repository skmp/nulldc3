#include "vector_anchors.h"

//TS: the operand sets of ftrv_specials.txt, matrices by rows
#include "ftrv_specials_vectors.h"

#include <cstring>

struct AnchorFile
{
	const char* path;
	u32 records[VI_COUNT];	//FIPR, FTRV, FSCA, FSRRA
};

enum
{
	FILE_FIPR_SPECIALS,
	FILE_FTRV_SPECIALS,
	FILE_FPU_SPECIALS,
	FILE_FPU_SPECIALS2,
	FILE_FIPR_TEST,

	FILES
};

static const AnchorFile files[FILES] =
{
	{ "model/fipr/hw_specials/fipr_specials.txt", { 1700, 0, 0, 0 } },
	{ "model/fipr/hw_specials/ftrv_specials.txt", { 0, 391, 0, 0 } },
	{ "model/fsrra/hw_specials/fpu_specials.txt", { 0, 0, 91, 806 } },
	{ "model/fsrra/hw_specials/fpu_specials2.txt", { 0, 0, 6, 48 } },
	{ "model/fipr/kos_fipr_test/fipr_test.out", { 10000, 0, 2056, 544 } },	//a line is a result in each rounding mode
};

u32 vector_anchor_files()
{
	return FILES;
}

const char* vector_anchor_file(u32 file)
{
	return files[file].path;
}

u32 vector_anchor_records(u32 file, VectorInsn insn)
{
	return files[file].records[insn];
}

//FPSCR settings the console programs do not print but name
#define FPSCR_DN        0x00040000
#define FPSCR_PR        0x00080000
#define FPSCR_PRESET    (0x0003F000 | 0x0000007C)	//every cause and every flag

//The registers the console programs of the special cases put in front of FSCA
#define FSCA_FR0 0x12345678
#define FSCA_FR1 0x9ABCDEF0

//fipr_test.out: the lines of the FSCA block that are an angle in both rounding modes
#define FSCA_TEST_ANGLES 1024

static void start(VectorAnchor& a, u32 file, u32 line, VectorInsn insn, u32 opcode, u32 fpscr)
{
	a.file = file;
	a.line = line;
	a.insn = insn;
	a.has_fpscr = false;

	a.c.opcode = opcode;
	a.c.trap = false;
	fpu_fill(a.c.before, fpscr);
}

//What the console read back: FPSCR and the exception
static void finish(VectorAnchor& a, u32 fpscr, bool trap, std::vector<VectorAnchor>& anchors)
{
	a.c.after.fpscr = fpscr;
	a.c.trap = trap;
	a.has_fpscr = true;

	anchors.push_back(a);
}

static void fipr(VectorAnchor& a, u32 file, u32 line, u32 m, u32 n, u32 fpscr, const unsigned* fvm, const unsigned* fvn, u32 result)
{
	start(a, file, line, VI_FIPR, vector_fipr(m, n), fpscr);

	for (u32 i = 0; i < 4; i++)
	{
		a.c.before.fr[m + i] = fvm[i];
		a.c.before.fr[n + i] = fvn[i];
	}

	a.c.after = a.c.before;
	a.c.after.fr[n + 3] = result;
}

//LDS x,FPUL / FSTS FPUL,FR0 / FSRRA FR0
static void fsrra(VectorAnchor& a, u32 file, u32 line, u32 fpscr, u32 x, u32 result)
{
	start(a, file, line, VI_FSRRA, vector_fsrra(0), fpscr);

	a.c.before.fr[0] = x;
	a.c.before.fpul = x;

	a.c.after = a.c.before;
	a.c.after.fr[0] = result;
}

static void fsca(VectorAnchor& a, u32 file, u32 line, u32 fpscr, u32 angle, bool sentinels, u32 sine, u32 cosine)
{
	start(a, file, line, VI_FSCA, vector_fsca(0), fpscr);

	a.c.before.fpul = angle;

	if (sentinels)
	{
		a.c.before.fr[0] = FSCA_FR0;
		a.c.before.fr[1] = FSCA_FR1;
	}

	a.c.after = a.c.before;
	a.c.after.fr[0] = sine;
	a.c.after.fr[1] = cosine;
}

static bool load_file(const std::string& shrike4_dir, u32 file, std::vector<VectorAnchor>& anchors, std::vector<std::string>& complaints)
{
	const std::string path = shrike4_dir + "/" + files[file].path;

	FILE* f = fopen(path.c_str(), "r");

	if (!f)
		return false;

	char text[1024];
	u32 line = 0;

	//fipr_test.out: the block a line is in, how many lines of it were read, and the FPSCR the program found and
	//changes RM of
	VectorInsn block = VI_COUNT;
	u32 block_lines = 0;
	u32 test_fpscr = FPSCR_DN;

	while (fgets(text, sizeof(text), f))
	{
		VectorAnchor a;
		char label[64];
		unsigned cfg, index, x, after, dn, rm, en, w[11];
		const char* p;

		line++;

		//an exception other than the FPU's would be the illegal instruction exception: no record has one
		bool trap = strstr(text, "TRAP-FPU") != 0;

		if (strstr(text, "TRAP-") != 0 && !trap)
		{
			char note[160];

			snprintf(note, sizeof(note), "%s:%u: an exception that is not the FPU exception", files[file].path, line);
			complaints.push_back(note);
			continue;
		}

		switch (file)
		{
		case FILE_FIPR_SPECIALS:
			//FIPR FV0,FV4: a is FV0 (FVm), b is FV4 (FVn), the result is FR7
			if (sscanf(text, "%63s cfg=%x v=%u a=%x,%x,%x,%x b=%x,%x,%x,%x -> res=%x fpscr=%x", label, &cfg, &index, &w[0], &w[1], &w[2], &w[3],
					&w[4], &w[5], &w[6], &w[7], &x, &after) == 13)
			{
				fipr(a, file, line, 0, 4, cfg, w, w + 4, x);
				finish(a, after, trap, anchors);
			}
			break;

		case FILE_FTRV_SPECIALS:
			//FTRV XMTRX,FV0 on operand set t
			if (sscanf(text, "%63s cfg=%x t=%u -> res=%x,%x,%x,%x fpscr=%x", label, &cfg, &index, &w[0], &w[1], &w[2], &w[3], &after) == 8 &&
				index < NT)
			{
				start(a, file, line, VI_FTRV, vector_ftrv(0), cfg);

				for (u32 column = 0; column < 4; column++)
				{
					for (u32 row = 0; row < 4; row++)
						a.c.before.xf[4 * column + row] = TS[index].row[row][column];

					a.c.before.fr[column] = TS[index].v[column];
				}

				a.c.after = a.c.before;
				for (u32 i = 0; i < 4; i++)
					a.c.after.fr[i] = w[i];

				finish(a, after, trap, anchors);
			}
			break;

		case FILE_FPU_SPECIALS:
		case FILE_FPU_SPECIALS2:
			if (sscanf(text, "FSRRA x=%x DN=%u RM=%u EN=%x -> res=%x fpscr=%x", &x, &dn, &rm, &en, &w[0], &after) == 6)
			{
				fsrra(a, file, line, (dn << 18) | en | rm, x, w[0]);
				finish(a, after, trap, anchors);
			}
			else if (strncmp(text, "FSRRA-PRESET", 12) == 0 && (p = strstr(text, "x=")) != 0 && sscanf(p, "x=%x -> res=%x fpscr=%x", &x, &w[0], &after) == 3)
			{
				fsrra(a, file, line, FPSCR_DN | FPSCR_PRESET, x, w[0]);
				finish(a, after, trap, anchors);
			}
			else if (sscanf(text, "FSRRA-PR1 x=%x -> res=%x fpscr=%x", &x, &w[0], &after) == 3)
			{
				fsrra(a, file, line, FPSCR_DN | FPSCR_PR, x, w[0]);
				finish(a, after, trap, anchors);
			}
			else if (strncmp(text, "FSRRA2", 6) == 0 && (p = strstr(text, "cfg=")) != 0 &&
				sscanf(p, "cfg=%x x=%x -> res=%x fpscr=%x", &cfg, &x, &w[0], &after) == 4)
			{
				fsrra(a, file, line, cfg, x, w[0]);
				finish(a, after, trap, anchors);
			}
			else if (sscanf(text, "FSCA a=%x RM=%u EN=%x -> sin=%x cos=%x fpscr=%x", &x, &rm, &en, &w[0], &w[1], &after) == 6)
			{
				fsca(a, file, line, FPSCR_DN | en | rm, x, true, w[0], w[1]);
				finish(a, after, trap, anchors);
			}
			else if (sscanf(text, "FSCA-PR1 a=%x -> fr0=%x fr1=%x fpscr=%x", &x, &w[0], &w[1], &after) == 4)
			{
				fsca(a, file, line, FPSCR_DN | FPSCR_PR, x, true, w[0], w[1]);
				finish(a, after, trap, anchors);
			}
			else if (strncmp(text, "FSCA2", 5) == 0 && (p = strstr(text, "cfg=")) != 0 &&
				sscanf(p, "cfg=%x a=%x -> fr0=%x fr1=%x fpscr=%x", &cfg, &x, &w[0], &w[1], &after) == 5)
			{
				fsca(a, file, line, cfg, x, true, w[0], w[1]);
				finish(a, after, trap, anchors);
			}
			break;

		default:
			//blocks of hex words between BEGIN and END, behind the console's boot messages
			if (sscanf(text, "BEGIN FIPR fpscr=%x", &cfg) == 1)
			{
				block = VI_FIPR;
				test_fpscr = cfg & ~3u;
			}
			else if (strncmp(text, "BEGIN FSRRA", 11) == 0)
			{
				block = VI_FSRRA;
			}
			else if (strncmp(text, "BEGIN FSCA", 10) == 0)
			{
				block = VI_FSCA;
				block_lines = 0;
			}
			else if (strncmp(text, "END ", 4) == 0)
			{
				block = VI_COUNT;
			}
			else if (block == VI_FIPR && sscanf(text, "%x %x %x %x %x %x %x %x %x %x %x", &w[0], &w[1], &w[2], &w[3], &w[4], &w[5], &w[6], &w[7], &w[8],
				&w[9], &w[10]) == 11)
			{
				//FIPR FV4,FV0: a is FV0 (FVn), b is FV4 (FVm), the result is FR3. Word 8 is what a model predicted
				for (u32 rz = 0; rz < 2; rz++)
				{
					fipr(a, file, line, 4, 0, test_fpscr | rz, w + 4, w, w[9 + rz]);
					anchors.push_back(a);
				}
			}
			else if (block == VI_FSRRA && sscanf(text, "%x %x %x", &w[0], &w[1], &w[2]) == 3)
			{
				for (u32 rz = 0; rz < 2; rz++)
				{
					fsrra(a, file, line, test_fpscr | rz, w[0], w[1 + rz]);
					anchors.push_back(a);
				}
			}
			else if (block == VI_FSCA && sscanf(text, "%x %x %x %x %x", &w[0], &w[1], &w[2], &w[3], &w[4]) == 5)
			{
				//512 angles around the turn and 512 around a quarter of it: sine and cosine, round to nearest and
				//round to zero. Then 4 lines of an angle with something in FPUL[31:16] against the angle alone,
				//both round to nearest
				bool upper = block_lines++ >= FSCA_TEST_ANGLES;

				for (u32 second = 0; second < 2; second++)
				{
					fsca(a, file, line, test_fpscr | (upper ? 0 : second), upper && second ? (w[0] & 0xFFFF) : w[0], false, w[1 + second * 2],
						w[2 + second * 2]);
					anchors.push_back(a);
				}
			}
			break;
		}
	}

	fclose(f);

	return true;
}

bool vector_anchors_load(const std::string& shrike4_dir, std::vector<VectorAnchor>& anchors, std::string& error,
	std::vector<std::string>& complaints)
{
	for (u32 file = 0; file < FILES; file++)
	{
		if (!load_file(shrike4_dir, file, anchors, complaints))
		{
			error = "cannot read " + shrike4_dir + "/" + files[file].path;
			return false;
		}
	}

	return true;
}
