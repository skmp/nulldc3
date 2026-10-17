/*
	aica.dsp.case.*: the DSP programs the caique project ran on a Dreamcast (caique-rtl model/cases/dsp_*.c and
	sh4_latch.c, mdec_hold.c), run here, against what the console answered (model/tests/<case>/hw*).

	A case is a C program on a small register and wave RAM interface (cases/aica_io.h). It is compiled from the
	checkout as it is, once for each machine it runs on:

	  model   the caique sample model, the whole sound chip, with the DSP core next to its DSP, compared behind
	          every step (dsp_lockstep.h). What the case writes out is the model's answer
	  core    the DSP core alone. Around it there is only what a silent sound generator does: every channel sends
	          0 to the bus of its ISEL in every sample. Cases that play a channel cannot run here
	  emu     the emulator's own sound chip (emu_aica.h): its AICA device with its register routing, its sound
	          generator and its DSP device, which holds the core. The case's register accesses are 32-bit accesses
	          of the SH4 through AICA::ReadReg / AICA::WriteReg, a sample is libAICA_TimeStep() sample by sample,
	          the wave memory is the device's. No channel is keyed, so every channel sends 0 to the bus of its
	          ISEL: the silent sound generator of the core machine. The emulator gives the latch no CPU read
	          (AicaDsp::BusRead is not wired, M3-D10). Only the cases anchored directly on the console's files run
	          here, as aica.dsp.case.<case>.emu, next to the core machine (the check emu-as-core)

	and what they write out is compared with the console's files. That makes two kinds of anchor:

	  direct              the core machine's output is the console's
	  through the model   the model's output is the console's, and the core is the model's DSP at every step

	The second kind depends on the model's output being the console's: the test counts that itself, in the
	checks named console-<run>:model, and a count below the baseline fails it.

	Both machines run on the time base of caique's own model harness (model/host/io_model.cpp): an access costs
	2400 ns, a timer read 200 ns, a sample starts every 1 / 44100 s. The preamble that caique's harness runs in
	front of a case is not run: it measures what a console cannot be told, and here nothing has to be measured.

	The case interface below (advance and the io_* calls) is written after that harness, and logs_aligned is the
	alignment loop of caique's checker of the dsp_coll logs (caique-rtl model/host/io_model.cpp and
	model/tools/coll_check.cpp, Copyright 2026 Stefanos Kornilios Mitsis Poiitidis, MIT licence:
	libswirl/hw/aica/dsp_core/README.md has the notice).
*/
#include "dsp_lockstep.h"
#include "dsp_suite.h"
#include "emu_aica.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifndef NULLDC_BASELINE
#define NULLDC_BASELINE "baseline.txt"
#endif
#ifndef NULLDC_CAIQUE_DIR
#define NULLDC_CAIQUE_DIR ""
#endif

/*
	The machines
*/

struct CaseMachine
{
	virtual ~CaseMachine() { }
	virtual uint32_t RegRead(uint32_t off) = 0;
	virtual void RegWrite(uint32_t off, uint32_t value) = 0;
	virtual uint32_t RamRead32(uint32_t off) = 0;
	virtual void RamWrite32(uint32_t off, uint32_t value) = 0;
	virtual void Sample() = 0;
	virtual bool Sh4Irq() = 0;
};

struct ModelMachine : CaseMachine
{
	DspLockstep ls;
	uint64_t samples = 0;

	uint32_t RegRead(uint32_t off) override { return ls.RegRead(off); }
	void RegWrite(uint32_t off, uint32_t value) override { ls.RegWrite(off, value); }
	uint32_t RamRead32(uint32_t off) override { return ls.RamRead32(off); }
	void RamWrite32(uint32_t off, uint32_t value) override { ls.RamWrite32(off, value); }
	bool Sh4Irq() override { return caique_sh4_irq(ls.model); }

	void Sample() override
	{
		ls.StepChip();
		samples++;

		//all of the wave RAM now and then: the steps compare what a DSP write can reach
		if ((samples & 1023) == 0)
			ls.CompareRam();
	}
};

struct CoreMachine : CaseMachine
{
	AicaDsp dsp;
	std::vector<uint8_t> ram;
	uint16_t reg[0x8000 / 4];

	CoreMachine()
	{
		ram.assign(CAIQUE_RAM_SIZE, 0);
		memset(reg, 0, sizeof(reg));
		dsp.Init(ram.data(), CAIQUE_RAM_SIZE);
	}

	uint32_t RegRead(uint32_t off) override
	{
		off &= 0x7FFC;

		return AicaDsp::RegOwned(off) ? dsp.RegRead(off) : reg[off >> 2];
	}

	void RegWrite(uint32_t off, uint32_t value) override
	{
		off &= 0x7FFC;

		if (AicaDsp::RegOwned(off))
			dsp.RegWrite(off, value);
		else
			reg[off >> 2] = (uint16_t)value;
	}

	uint32_t RamRead32(uint32_t off) override
	{
		off &= CAIQUE_RAM_SIZE - 4;

		uint32_t value = ram[off] | (ram[off + 1] << 8) | (ram[off + 2] << 16) | ((uint32_t)ram[off + 3] << 24);

		dsp.BusRead((uint16_t)(value >> 16));

		return value;
	}

	void RamWrite32(uint32_t off, uint32_t value) override
	{
		off &= CAIQUE_RAM_SIZE - 4;

		for (int i = 0; i < 4; i++)
			ram[off + i] = (uint8_t)(value >> (8 * i));
	}

	bool Sh4Irq() override { return false; }

	//The silent sound generator: 64 channels that send 0 to their ISEL bus (console: mixs_write)
	void Sample() override
	{
		dsp.SweepBegin();

		for (int ch = 0; ch < 64; ch++)
			dsp.MixsSend(reg[(0x80 * ch + 0x20) >> 2] & 0xF, 0);

		dsp.RunSample();
	}
};

//The emulator's sound chip: a read or a write of a register is a 32-bit access of the SH4, a read of the wave
//memory reaches no latch
struct EmuMachine : CaseMachine
{
	EmuAica* emu;

	EmuMachine()
	{
		EmuCddaOn(false);
		emu = EmuAica::Create(CAIQUE_RAM_SIZE);
	}

	~EmuMachine() { delete emu; }

	uint32_t RegRead(uint32_t off) override { return emu->RegRead(off & 0x7FFC, 4); }
	void RegWrite(uint32_t off, uint32_t value) override { emu->RegWrite(off & 0x7FFC, value, 4); }

	uint32_t RamRead32(uint32_t off) override
	{
		const uint8_t* ram = emu->Ram();

		off &= CAIQUE_RAM_SIZE - 4;

		return ram[off] | (ram[off + 1] << 8) | (ram[off + 2] << 16) | ((uint32_t)ram[off + 3] << 24);
	}

	void RamWrite32(uint32_t off, uint32_t value) override
	{
		uint8_t* ram = emu->Ram();

		off &= CAIQUE_RAM_SIZE - 4;

		for (int i = 0; i < 4; i++)
			ram[off + i] = (uint8_t)(value >> (8 * i));
	}

	bool Sh4Irq() override { return emu->Sh4Irq(); }
	void Sample() override { emu->Sample(); }
};

/*
	The interface of the cases (caique-rtl model/cases/aica_io.h), on the machine that is running
*/

static CaseMachine* machine;
static uint64_t time_ns, samples_run;
static std::map<std::string, std::vector<uint8_t>> files;

static const uint64_t ACCESS_NS = 2400;

static void advance(uint64_t ns)
{
	time_ns += ns;

	uint64_t due = time_ns * 44100 / 1000000000ull;

	while (samples_run < due)
	{
		machine->Sample();
		samples_run++;
	}
}

extern "C"
{
	const char* io_platform = "model";

	uint32_t io_r(uint32_t off)
	{
		advance(ACCESS_NS);
		return machine->RegRead(off);
	}

	void io_w(uint32_t off, uint32_t value)
	{
		advance(ACCESS_NS);
		machine->RegWrite(off, value);
	}

	uint32_t io_ram_r32(uint32_t off)
	{
		advance(ACCESS_NS);
		return machine->RamRead32(off);
	}

	void io_ram_w32(uint32_t off, uint32_t value)
	{
		advance(ACCESS_NS);
		machine->RamWrite32(off, value);
	}

	void io_wn(int n, const uint32_t* off, const uint32_t* value)
	{
		for (int i = 0; i < n; i++)
		{
			if (off[i] >= 0x100000u)
				io_ram_w32(off[i] - 0x100000u, value[i]);
			else
				io_w(off[i], value[i]);
		}
	}

	void io_ram_fill(uint32_t off, uint32_t value, uint32_t bytes)
	{
		for (uint32_t i = 0; i < bytes / 4; i++)
			io_ram_w32(off + 4 * i, value);
	}

	void io_ram_write(uint32_t off, const void* src, uint32_t bytes)
	{
		const uint8_t* s = (const uint8_t*)src;

		for (uint32_t i = 0; i < (bytes + 3) / 4; i++)
		{
			uint32_t value;

			memcpy(&value, s + 4 * i, 4);
			io_ram_w32(off + 4 * i, value);
		}
	}

	void io_wait_us(uint32_t us)
	{
		advance((uint64_t)us * 1000);
	}

	uint64_t io_now_us(void)
	{
		advance(200);
		return time_ns / 1000;
	}

	int io_write_file(const char* name, const void* data, uint32_t bytes)
	{
		const uint8_t* d = (const uint8_t*)data;

		files[name].assign(d, d + bytes);
		return 0;
	}

	void io_print(const char*) { }
	uint32_t io_sh4_irq(void) { advance(ACCESS_NS); return machine->Sh4Irq() ? 1 : 0; }
	void io_replay_point(uint32_t) { }
	void io_anchor_point(uint32_t) { }

	//The cases: each is compiled once per machine it runs on, with its test_main under these names
	int caique_case_dsp_basic_model(void);
	int caique_case_dsp_basic_core(void);
	int caique_case_dsp_mem_model(void);
	int caique_case_dsp_mem_core(void);
	int caique_case_dsp_mem2_model(void);
	int caique_case_dsp_mem2_core(void);
	int caique_case_dsp_mem3_model(void);
	int caique_case_dsp_mem3_core(void);
	int caique_case_dsp_wslot_model(void);
	int caique_case_dsp_wslot_core(void);
	int caique_case_dsp_temp_model(void);
	int caique_case_dsp_temp_core(void);
	int caique_case_dsp_unpack_model(void);
	int caique_case_dsp_unpack_core(void);
	int caique_case_dsp_pack_core(void);
	int caique_case_dsp_coll_model(void);
	int caique_case_sh4_latch_model(void);
	int caique_case_mdec_hold_model(void);
	int caique_case_dsp_basic_emu(void);
	int caique_case_dsp_mem_emu(void);
	int caique_case_dsp_mem2_emu(void);
	int caique_case_dsp_mem3_emu(void);
	int caique_case_dsp_wslot_emu(void);
	int caique_case_dsp_unpack_emu(void);
	int caique_case_dsp_pack_emu(void);
}

typedef std::map<std::string, std::vector<uint8_t>> CaseFiles;

static void run_case(CaseMachine* on, int (*test_main)(void), CaseFiles& out)
{
	machine = on;
	time_ns = 0;
	samples_run = 0;
	files.clear();

	test_main();

	out = files;
	machine = 0;
}

static std::vector<std::string> text_lines(const std::vector<uint8_t>& data)
{
	std::vector<std::string> lines;
	std::string line;

	for (size_t i = 0; i < data.size(); i++)
	{
		if (data[i] == '\n')
		{
			lines.push_back(line);
			line.clear();
		}
		else if (data[i] != '\r')
			line += (char)data[i];
	}

	if (!line.empty())
		lines.push_back(line);

	return lines;
}

/*
	Comparisons with the console's files
*/

static std::string caique_dir;

static std::string console_path(const char* test, const char* run, const std::string& file)
{
	return caique_dir + "/model/tests/" + test + "/" + run + "/" + file;
}

//A line with what differs from run to run taken out: the time a case measured (dsp_pack ends in "; <n> ms")
static std::string stable(const std::string& line)
{
	size_t ms = line.rfind(" ms");
	size_t semi = line.rfind("; ");

	if (ms != std::string::npos && ms + 3 == line.size() && semi != std::string::npos && semi < ms
		&& line.find_first_not_of("0123456789", semi + 2) == ms)
		return line.substr(0, semi);

	return line;
}

//Lines of `mine` that are the console's, of the console's number of lines
static void compare_text(const std::string& check, const std::vector<uint8_t>& mine, const std::string& console_file)
{
	std::vector<std::string> theirs;

	if (!dsp_read_lines(console_file, theirs))
	{
		dsp_problem("cannot read %s", console_file.c_str());
		return;
	}

	std::vector<std::string> ours = text_lines(mine);
	uint64_t same = 0;

	for (size_t i = 0; i < theirs.size(); i++)
	{
		bool equal = i < ours.size() && stable(ours[i]) == stable(theirs[i]);

		same += equal;

		if (!equal && dsp_suite_opt().verbose)
			dsp_note("  %s line %zu\n    console: %s\n    here:    %s", check.c_str(), i + 1, theirs[i].c_str(),
				i < ours.size() ? ours[i].c_str() : "(none)");
	}

	if (ours.size() != theirs.size())
		dsp_note("  %s: %zu lines here, %zu on the console", check.c_str(), ours.size(), theirs.size());

	dsp_count(check, theirs.size(), same);
}

static void compare_texts(const std::string& check, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
	std::vector<std::string> la = text_lines(a), lb = text_lines(b);
	uint64_t same = 0;

	for (size_t i = 0; i < la.size(); i++)
		same += i < lb.size() && la[i] == lb[i];

	dsp_count(check, la.size() > lb.size() ? la.size() : lb.size(), same);
}

//Words of a binary file that are the console's
static void compare_words(const std::string& check, const std::vector<uint8_t>& mine, const std::string& console_file,
	uint32_t word_bytes)
{
	std::vector<uint8_t> theirs;

	if (!dsp_read_file(console_file, theirs))
	{
		dsp_problem("cannot read %s", console_file.c_str());
		return;
	}

	uint64_t same = 0, total = theirs.size() / word_bytes;

	for (uint64_t i = 0; i < total; i++)
		same += (i + 1) * word_bytes <= mine.size() && memcmp(&mine[i * word_bytes], &theirs[i * word_bytes], word_bytes) == 0;

	dsp_count(check, total, same);
}

typedef std::vector<std::vector<uint16_t>> LogRows;

//A per-sample log of a case: rows of `width` 16 bit words, the first a counter
static LogRows log_rows(const std::vector<uint8_t>& data, uint32_t width)
{
	LogRows rows;

	for (size_t at = 0; at + 2 * width <= data.size(); at += 2 * width)
	{
		std::vector<uint16_t> row(width);

		for (uint32_t k = 0; k < width; k++)
			row[k] = (uint16_t)(data[at + 2 * k] | (data[at + 2 * k + 1] << 8));

		rows.push_back(row);
	}

	return rows;
}

/*
	Two logs of the same run start at different samples: the console's clock is not the harness's. They are the
	same when one shift puts every row of the overlap on an equal row, the first and last 8 rows of each log left
	out, and the overlap is 400 rows or more. That is the rule of caique's own checker (model/tools/coll_check.cpp).
	Returns the rows of the overlap at the best shift, and how many of them are equal.
*/
static bool logs_aligned(const LogRows& a, const LogRows& b, uint32_t width, int& overlap, int& equal)
{
	const int skip = 8, na = (int)a.size(), nb = (int)b.size();

	overlap = 0;
	equal = -1;

	for (int d = -(nb - skip); d <= na - skip; d++)
	{
		int eq = 0, ov = 0;

		for (int i = skip; i < na - skip; i++)
		{
			int j = i - d;

			if (j < skip || j >= nb - skip)
				continue;

			bool same = true;

			for (uint32_t k = 1; k < width; k++)
				same &= a[i][k] == b[j][k];

			ov++;
			eq += same;
		}

		if (eq > equal || (eq == equal && ov > overlap))
		{
			equal = eq;
			overlap = ov;
		}
	}

	return equal == overlap && overlap >= 400;
}

/*
	The cases
*/

static const char* const coll_runs[] =
{
	"p16_k20", "p16_k3", "p16_k20_q", "p16_k20_f", "p8_k20", "adp_om2", "adp_om1", "adp_o0", "adp_o1", "adp_o2",
	"p16_wr", "adp_om1_wr", "latch_odd", "latch_even", "p16_kon", "p16_kon_k3", "p8_kon", "adp_kon",
};

//dsp_coll: 18 runs with a channel playing, each a log of what the DSP read and wrote in the channel's slot
static void anchor_dsp_coll(const CaseFiles& model, const char* run)
{
	uint64_t same_runs = 0, rows = 0, rows_equal = 0;

	for (const char* name : coll_runs)
	{
		std::string file = std::string("dsp_coll_") + name + ".bin";
		std::vector<uint8_t> theirs;
		auto mine = model.find(file);

		if (!dsp_read_file(console_path("dsp_coll", run, file), theirs))
		{
			dsp_problem("cannot read %s", console_path("dsp_coll", run, file).c_str());
			continue;
		}

		if (mine == model.end())
			continue;

		int overlap, equal;
		bool same = logs_aligned(log_rows(theirs, 5), log_rows(mine->second, 5), 5, overlap, equal);

		same_runs += same;
		rows += overlap;
		rows_equal += equal > 0 ? equal : 0;

		if (!same)
			dsp_note("  dsp_coll %s %s: %d of %d samples as the console's", run, name, equal, overlap);
	}

	dsp_count(std::string("console-") + run + ":model:runs", sizeof(coll_runs) / sizeof(coll_runs[0]), same_runs);
	dsp_count(std::string("console-") + run + ":model:samples", rows, rows_equal);
}

/*
	sh4_latch: five reads of the DSP, logged every sample, while the SH4 reads one wave RAM word (0xBEEF1234) back
	to back or not at all, and channel 38 plays words of 0x3838 or is silent.

	The runs without SH4 reads are constant: every sample has the same five words on the console and in the model.
	The runs with them are not the sample model's to replay: which sample an SH4 read falls into, and into which
	slot of it, is the bus's timing. The core has the rule those samples follow (a read stays in the latch from its
	slot on), and that is checked on every sample of the console's logs: the core, given SH4 reads in slots where
	the bus can put them, has to return the console's five words. The words of the reads are the two halves of the
	SH4's word and nothing else, so a console sample with any other word would not pass.
*/
static const int latch_mrd[5] = { 16, 48, 81, 97, 64 };
static const int latch_iwt[5] = { 19, 51, 84, 99, 67 };
static const uint16_t latch_own[5] = { 0x1616, 0x4848, 0x8181, 0x9797, 0x6464 };

static void anchor_sh4_latch_constant(const CaseFiles& model, const char* name)
{
	std::string file = std::string("sl_") + name + ".bin";
	std::vector<uint8_t> theirs;
	auto mine = model.find(file);

	if (!dsp_read_file(console_path("sh4_latch", "hw11", file), theirs))
	{
		dsp_problem("cannot read %s", console_path("sh4_latch", "hw11", file).c_str());
		return;
	}

	LogRows a = log_rows(theirs, 6), b = mine != model.end() ? log_rows(mine->second, 6) : LogRows();
	uint64_t same = 0, total = 0;

	//the first 16 samples of a log are left out, as caique's checker leaves them out (model/tools/latch6_check.cpp)
	for (size_t i = 16; i < a.size(); i++)
	{
		bool equal = i < b.size();

		for (int k = 1; k < 6 && equal; k++)
			equal = a[i][k] == b[i][k];

		total++;
		same += equal;
	}

	dsp_count(std::string("console-hw11:model:") + name, total, same);
}

static void anchor_sh4_latch_reads(const char* name, bool channel)
{
	std::string file = std::string("sl_") + name + ".bin";
	std::vector<uint8_t> theirs;

	if (!dsp_read_file(console_path("sh4_latch", "hw11", file), theirs))
	{
		dsp_problem("cannot read %s", console_path("sh4_latch", "hw11", file).c_str());
		return;
	}

	LogRows rows = log_rows(theirs, 6);
	std::vector<uint8_t> ram(CAIQUE_RAM_SIZE, 0);
	AicaDsp dsp;

	dsp.Init(ram.data(), CAIQUE_RAM_SIZE);
	dsp.RegWrite(0x2804, (0x100000 >> 11) | (2 << 13));

	//the five reads as the case programs them: each of its own word, in table mode, NOFL up to its IWT. The fifth
	//has an MWT in its step, which writes a word of its own
	for (int p = 0; p < 5; p++)
	{
		AicaDspInst inst;
		uint16_t w[4];

		dsp.RegWrite(0x3200 + 4 * p, 0xC000 + p);
		ram[0x100000 + 2 * (0xC000 + p)] = (uint8_t)latch_own[p];
		ram[0x100000 + 2 * (0xC000 + p) + 1] = (uint8_t)(latch_own[p] >> 8);

		for (int s = latch_mrd[p]; s <= latch_iwt[p]; s++)
		{
			memset(&inst, 0, sizeof(inst));
			inst.NOFL = 1;

			if (s == latch_mrd[p])
			{
				inst.MRD = 1;
				inst.TABLE = 1;
				inst.MASA = (uint8_t)p;

				if (p == 4)
				{
					inst.MWT = 1;
					inst.MASA = 9;
				}
			}

			if (s == latch_iwt[p])
			{
				inst.IWT = 1;
				inst.IWA = (uint8_t)p;
			}

			AicaDspEncode(&inst, w);

			for (int k = 0; k < 4; k++)
				dsp.RegWrite(0x3400 + 16 * s + 4 * k, w[k]);
		}
	}

	dsp.RegWrite(0x3200 + 4 * 9, 0xC009);

	uint64_t same = 0, total = 0;

	for (size_t i = 16; i < rows.size(); i++)
	{
		uint16_t want[5];

		//the case logs the negated word
		for (int p = 0; p < 5; p++)
			want[p] = (uint16_t)(0 - rows[i][1 + p]);

		//What the latch holds for the fifth read when nothing reads behind step 49: the channel's word from its
		//slot, step 62, or what the second read left
		uint16_t behind = channel ? 0x3838 : want[1];

		bool half0 = want[0] == 0x1234 || want[0] == 0xBEEF, half1 = want[1] == 0x1234 || want[1] == 0xBEEF;
		bool half4 = want[4] == 0x1234 || want[4] == 0xBEEF;
		bool possible = (want[0] == latch_own[0] || half0) && (want[1] == latch_own[1] || half1)
			&& (want[4] == behind || half4);

		for (int s = 0; s < AICA_DSP_STEPS && possible; s++)
		{
			//an SH4 read in the odd slot behind an even MRD
			if (s == 17 && half0)
				dsp.BusRead(want[0]);

			if (s == 49 && half1)
				dsp.BusRead(want[1]);

			if (s == 62 && channel)
				dsp.ChannelFetch(0x3838);

			if (s == 65 && want[4] != behind)
				dsp.BusRead(want[4]);

			//and an SH4 read behind each of the odd MRDs, where it is too late for the IWT: both halves
			if (s == 82 || s == 98)
				dsp.BusRead(0x1234);

			if (s == 84 || s == 100)
				dsp.BusRead(0xBEEF);

			dsp.StepOne();
		}

		bool equal = possible;

		for (int p = 0; p < 5 && equal; p++)
			equal = (uint16_t)(dsp.MEMS[p] >> 8) == want[p];

		total++;
		same += equal;
	}

	dsp_count(std::string("console-hw11:core:") + name, total, same);
}

struct CaseDef
{
	const char* name;
	int (*on_model)(void);		//0: dsp_pack, see README.md
	int (*on_core)(void);		//0: the case plays a channel
	const char* text;			//its report
	const char* console[2];		//the console runs its report is compared with, line by line
	int (*on_emu)(void);		//the case on the emulator's sound chip, for the cases anchored directly on the console
};

static const CaseDef cases[] =
{
	{ "dsp_basic", caique_case_dsp_basic_model, caique_case_dsp_basic_core, "dsp_basic.txt", { "hw9", "hw" }, caique_case_dsp_basic_emu },
	{ "dsp_mem", caique_case_dsp_mem_model, caique_case_dsp_mem_core, "dsp_mem.txt", { "hw9", "hw" }, caique_case_dsp_mem_emu },
	{ "dsp_mem2", caique_case_dsp_mem2_model, caique_case_dsp_mem2_core, "dsp_mem2.txt", { "hw9", "hw" }, caique_case_dsp_mem2_emu },
	{ "dsp_mem3", caique_case_dsp_mem3_model, caique_case_dsp_mem3_core, "dsp_mem3.txt", { "hw9", "hw" }, caique_case_dsp_mem3_emu },
	{ "dsp_wslot", caique_case_dsp_wslot_model, caique_case_dsp_wslot_core, "dsp_wslot.txt", { "hw9", "hw" }, caique_case_dsp_wslot_emu },
	{ "dsp_unpack", caique_case_dsp_unpack_model, caique_case_dsp_unpack_core, "dsp_unpack.txt", { "hw9", "hw" }, caique_case_dsp_unpack_emu },
	{ "dsp_pack", 0, caique_case_dsp_pack_core, "dsp_pack.txt", { "hw9", "hw" }, caique_case_dsp_pack_emu },
	{ "dsp_temp", caique_case_dsp_temp_model, caique_case_dsp_temp_core, "dsp_temp.txt", { 0, 0 } },
	{ "dsp_coll", caique_case_dsp_coll_model, 0, "dsp_coll.txt", { 0, 0 } },
	{ "sh4_latch", caique_case_sh4_latch_model, 0, "sh4_latch.txt", { 0, 0 } },
	{ "mdec_hold", caique_case_mdec_hold_model, 0, "mdec_hold.txt", { 0, 0 } },
};

int main(int argc, char** argv)
{
	static const char* usage =
		"aica_dsp_cases <case> [options]\n"
		"A DSP program of the caique project on the caique sample model with the DSP core next to it, and on the\n"
		"core alone, against what a Dreamcast answered. Cases: dsp_basic dsp_mem dsp_mem2 dsp_mem3 dsp_wslot\n"
		"dsp_unpack dsp_pack dsp_temp dsp_coll sh4_latch mdec_hold. <case>.emu: the case on the emulator's sound\n"
		"chip, next to the core alone, for dsp_basic dsp_mem dsp_mem2 dsp_mem3 dsp_wslot dsp_unpack dsp_pack";

	DspOptions opt;

	opt.baseline = NULLDC_BASELINE;
	opt.data_dir = NULLDC_CAIQUE_DIR;

	const CaseDef* def = 0;
	bool emu = false;

	for (const CaseDef& c : cases)
	{
		if (argc > 1 && std::string(argv[1]) == c.name)
			def = &c;

		if (argc > 1 && c.on_emu && std::string(argv[1]) == std::string(c.name) + ".emu")
		{
			def = &c;
			emu = true;
		}
	}

	if (!def)
	{
		fprintf(stderr, "%s\n", usage);
		return DSP_EXIT_ERROR;
	}

	if (!dsp_suite_options(argc, argv, 2, usage, opt))
		return DSP_EXIT_ERROR;

	std::string name = std::string("aica.dsp.case.") + def->name + (emu ? ".emu" : "");

	caique_dir = opt.data_dir;

	//the console's captures: the directory of the case has to be there
	std::vector<std::string> probe;

	if (caique_dir.empty() || !dsp_read_lines(caique_dir + "/model/cases/" + def->name + ".c", probe))
		return dsp_suite_skip(name.c_str(), opt, "no caique-rtl checkout with model/cases in \"" + caique_dir + "\"");

	if (!dsp_suite_begin(name.c_str(), opt))
		return DSP_EXIT_ERROR;

	CaseFiles on_model, on_core, on_emu;

	if (emu)
	{
		EmuMachine* m = new EmuMachine();

		run_case(m, def->on_emu, on_emu);
		dsp_note("emu:   %llu samples", (unsigned long long)samples_run);
		delete m;

		CoreMachine* c = new CoreMachine();

		run_case(c, def->on_core, on_core);
		delete c;

		//what the emulator's chip writes, line by line against the core alone: where the wiring differs from it
		compare_texts("emu-as-core:lines", on_emu[def->text], on_core[def->text]);

		for (const char* run : def->console)
			compare_text(std::string("console-") + run + ":emu:lines", on_emu[def->text], console_path(def->name, run, def->text));

		if (std::string(def->name) == "dsp_unpack")
		{
			for (const char* run : def->console)
				compare_words(std::string("console-") + run + ":emu:words", on_emu["unpack.bin"], console_path("dsp_unpack", run, "unpack.bin"), 4);
		}

		return dsp_suite_end();
	}

	if (def->on_model)
	{
		ModelMachine* m = new ModelMachine();

		run_case(m, def->on_model, on_model);
		m->ls.CompareRam();

		dsp_count("model:steps", m->samples * 128, m->ls.steps);
		dsp_count("model:run-samples", m->samples, m->ls.fast_samples);
		dsp_count("model:cpu-reads", m->ls.reg_reads, m->ls.reg_reads);
		dsp_note("model: %llu samples, %llu steps compared, %llu CPU reads of DSP registers compared",
			(unsigned long long)m->samples, (unsigned long long)m->ls.steps, (unsigned long long)m->ls.reg_reads);

		const DspLockstep::Coverage& c = m->ls.cover;

		dsp_note("       %llu wave RAM reads, %llu writes, %llu lost to a channel, %llu IWT, %llu channel fetches",
			(unsigned long long)c.ram_reads, (unsigned long long)c.ram_writes, (unsigned long long)c.slots_lost,
			(unsigned long long)(c.iwt_raw + c.iwt_float), (unsigned long long)c.fetches);

		if (!m->ls.ok)
			dsp_note("first difference: %s", m->ls.first.c_str());

		if (!m->ls.ok && m->ls.first.find("announce") != std::string::npos)
			dsp_problem("%s", m->ls.first.c_str());

		delete m;
	}

	if (def->on_core)
	{
		CoreMachine* m = new CoreMachine();

		run_case(m, def->on_core, on_core);
		dsp_note("core:  %llu samples", (unsigned long long)samples_run);
		delete m;

		//the same report from both: the model's sound generator did nothing the silent one does not do
		if (def->on_model)
			compare_texts("core-as-model:lines", on_core[def->text], on_model[def->text]);
	}

	for (const char* run : def->console)
	{
		if (!run)
			continue;

		std::string console = console_path(def->name, run, def->text);

		if (def->on_model)
			compare_text(std::string("console-") + run + ":model:lines", on_model[def->text], console);

		if (def->on_core)
			compare_text(std::string("console-") + run + ":core:lines", on_core[def->text], console);
	}

	std::string which = def->name;

	if (which == "dsp_unpack")
	{
		//all 65536 words through MRD and IWT: the MEMS values the console read back
		for (const char* run : def->console)
		{
			compare_words(std::string("console-") + run + ":model:words", on_model["unpack.bin"], console_path("dsp_unpack", run, "unpack.bin"), 4);
			compare_words(std::string("console-") + run + ":core:words", on_core["unpack.bin"], console_path("dsp_unpack", run, "unpack.bin"), 4);
		}
	}
	else if (which == "dsp_coll")
	{
		anchor_dsp_coll(on_model, "hw9");
		anchor_dsp_coll(on_model, "hw");
	}
	else if (which == "sh4_latch")
	{
		anchor_sh4_latch_constant(on_model, "idle");
		anchor_sh4_latch_constant(on_model, "ch");
		anchor_sh4_latch_reads("sh4", false);
		anchor_sh4_latch_reads("sh4_ch", true);
	}

	return dsp_suite_end();
}
