#include "wren7_job.h"
#include "arm7_machine.h"
#include "arm7_suite.h"

#include <cstdlib>
#include <cstring>
#include <map>

//The ARM runs at 22.5792 MHz and no instruction is shorter than one 8 clock memory cycle: this is what the
//fastest possible program executes in a millisecond, rounded up
#define WREN7_STEPS_PER_MS 2823

#define WREN7_RES_ADDR 0x1FF000

struct JobSteps
{
	u64 max_steps;
	u64 steps;
	u64 ticks;
};

static bool read_lines(const std::string& path, std::vector<std::vector<std::string> >& lines)
{
	FILE* f = fopen(path.c_str(), "r");

	if (!f)
		return false;

	char line[4096];

	while (fgets(line, sizeof(line), f))
	{
		std::vector<std::string> words;

		for (char* word = strtok(line, " \t\r\n"); word; word = strtok(0, " \t\r\n"))
			words.push_back(word);

		if (!words.empty() && words[0][0] != '#')
			lines.push_back(words);
	}

	fclose(f);

	return true;
}

static u32 number(const std::string& text)
{
	return (u32)strtoul(text.c_str(), 0, 0);
}

//What is below build/ is not in a wren7-rtl checkout, it is made from it
bool wren7_is_build_output(const std::string& file)
{
	return file.compare(0, 9, "../build/") == 0;
}

//file is the name as the job file has it
static std::string missing(const std::string& path, const std::string& file = "")
{
	std::string text = path + " is not there";

	if (file.compare(0, 16, "../build/hw/arm/") == 0)
		text += ". wren7-rtl builds it with: make -C model/hw/arm (arm-eabi toolchain)";
	else if (file.compare(0, 22, "../build/hw/qemu_diff/") == 0)
		text += ". wren7-rtl builds it with: ../build/model/hw_suite gen qemu_diff, run in model/ (arm-eabi toolchain)";
	else if (wren7_is_build_output(file))
		text += ". It is a build output of wren7-rtl";
	else if (file.compare(0, 10, "../vendor/") == 0)
		text += ", the wren7-rtl checkout is not complete: its submodules are missing";
	else
		text += ", the wren7-rtl checkout is not complete";

	return text;
}

Wren7Load wren7_load_suite(const std::string& wren7_dir, const std::string& suite, Wren7Suite& out, std::string& error)
{
	out.model_dir = wren7_dir + "/model";
	out.out_dir.clear();
	out.jobs.clear();

	if (!arm7_dir_exists(out.model_dir))
	{
		error = "no wren7-rtl checkout in " + wren7_dir + " (it has no model directory)";
		return W7L_MISSING;
	}

	std::string job_file = out.model_dir + "/tests/hw/" + suite + "/jobs.txt";
	std::vector<std::vector<std::string> > lines;

	if (!read_lines(job_file, lines))
	{
		error = missing(job_file);
		return W7L_BAD;
	}

	std::map<std::string, std::vector<u8> > files;
	Wren7Job job;
	bool in_job = false;

	for (size_t i = 0; i < lines.size(); i++)
	{
		const std::vector<std::string>& w = lines[i];
		const std::string& cmd = w[0];

		if (cmd == "out" && w.size() >= 2)
		{
			out.out_dir = w[1];
		}
		else if (cmd == "job" && w.size() >= 4)
		{
			job = Wren7Job();
			job.name = w[1];
			job.timeout_ms = number(w[2]);
			job.repeat = number(w[3]);
			job.needs_sh4 = false;
			in_job = true;
		}
		else if (cmd == "samples" || cmd == "chstate")
		{
			//calibration of the console, not a part of any job
		}
		else if (!in_job)
		{
			error = job_file + ": " + cmd + " outside of a job";
			return W7L_BAD;
		}
		else if (cmd == "clear" || cmd == "quiet")
		{
			//wave RAM and the AICA as every job gets them here
		}
		else if (cmd == "load" && w.size() >= 3)
		{
			Wren7Image image;

			image.addr = number(w[1]);
			image.file = w[2];

			if (files.find(image.file) == files.end())
			{
				std::string path = out.model_dir + "/" + image.file;

				if (!arm7_read_file(path, files[image.file]))
				{
					error = missing(path, image.file);
					return wren7_is_build_output(image.file) ? W7L_MISSING : W7L_BAD;
				}
			}

			image.data = files[image.file];
			job.images.push_back(image);
		}
		else if (cmd == "word" && w.size() >= 3)
		{
			Wren7Word word;

			word.addr = number(w[1]);
			word.value = number(w[2]);
			job.words.push_back(word);
		}
		else if (cmd == "read" && w.size() >= 3)
		{
			Wren7Region region;

			region.addr = number(w[1]);
			region.size = number(w[2]);
			job.reads.push_back(region);
		}
		else if (cmd == "areg" || cmd == "rreg" || cmd == "sh4load")
		{
			job.needs_sh4 = true;
		}
		else if (cmd == "run")
		{
			out.jobs.push_back(job);
			in_job = false;
		}
		else
		{
			error = job_file + ": cannot read the " + cmd + " line";
			return W7L_BAD;
		}
	}

	if (out.jobs.empty() || out.out_dir.empty())
	{
		error = job_file + ": no jobs, or no out directory";
		return W7L_BAD;
	}

	//the console's side
	std::string console_dir = out.model_dir + "/" + out.out_dir;
	std::string results_file = console_dir + "/results.txt";

	lines.clear();

	if (!read_lines(results_file, lines))
	{
		error = missing(results_file);
		return W7L_BAD;
	}

	for (size_t j = 0; j < out.jobs.size(); j++)
	{
		Wren7Job& job = out.jobs[j];

		for (size_t i = 0; i < lines.size(); i++)
		{
			const std::vector<std::string>& w = lines[i];

			if (w.size() < 3 || w[0] != job.name)
				continue;

			job.console_status = w[1];

			//w[2] is MCIPD as the SH4 read it after the run
			for (size_t t = 3; t < w.size(); t++)
				job.console_sh4_cycles.push_back(strtoull(w[t].c_str(), 0, 10));
		}

		if (job.console_status.empty())
		{
			error = results_file + ": no result of job " + job.name;
			return W7L_BAD;
		}

		if (job.reads.empty())
			continue;

		std::string capture = console_dir + "/" + job.name + ".bin";

		if (!arm7_read_file(capture, job.console))
		{
			error = missing(capture);
			return W7L_BAD;
		}

		//register readouts follow the regions
		if (job.needs_sh4 ? job.console.size() < wren7_read_size(job) : job.console.size() != wren7_read_size(job))
		{
			error = capture + ": is not the size of the regions that job " + job.name + " reads";
			return W7L_BAD;
		}
	}

	return W7L_OK;
}

u32 wren7_read_size(const Wren7Job& job)
{
	u32 size = 0;

	for (size_t i = 0; i < job.reads.size(); i++)
		size += job.reads[i].size;

	return size;
}

static void run_steps(void* arg)
{
	JobSteps* s = (JobSteps*)arg;

	while (!arm7m_scpu_raised() && s->steps < s->max_steps)
	{
		s->ticks += arm7m_step();
		s->steps++;
	}
}

void wren7_run_job(const Wren7Job& job, Wren7Run& run)
{
	u8* ram = arm7m_ram();

	run.guard = TGR_OK;
	run.done = false;
	run.steps = 0;
	run.ticks = 0;
	run.readback.clear();

	//a job does not depend on the jobs before it. From repeat to repeat the registers are kept, as on the console
	arm7m_power_on();

	for (u32 repeat = 0; repeat < job.repeat || repeat == 0; repeat++)
	{
		arm7m_hold_reset();
		arm7m_clear();

		for (size_t i = 0; i < job.images.size(); i++)
		{
			const Wren7Image& image = job.images[i];

			for (size_t b = 0; b < image.data.size(); b++)
				ram[(image.addr + b) & (ARM7M_RAM_SIZE - 1)] = image.data[b];
		}

		for (size_t i = 0; i < job.words.size(); i++)
		{
			for (u32 b = 0; b < 4; b++)
				ram[(job.words[i].addr + b) & (ARM7M_RAM_SIZE - 1)] = (u8)(job.words[i].value >> (8 * b));
		}

		arm7m_release_reset();

		JobSteps steps;

		steps.max_steps = (u64)job.timeout_ms * WREN7_STEPS_PER_MS;
		steps.steps = 0;
		steps.ticks = 0;

		run.guard = arm7_guarded(job.name, &run_steps, &steps);
		run.done = arm7m_scpu_raised();
		run.steps = steps.steps;
		run.ticks = steps.ticks;

		if (run.guard != TGR_OK)
			break;
	}

	for (size_t i = 0; i < job.reads.size(); i++)
	{
		for (u32 b = 0; b < job.reads[i].size; b++)
			run.readback.push_back(ram[(job.reads[i].addr + b) & (ARM7M_RAM_SIZE - 1)]);
	}
}

bool wren7_same_end(const Wren7Job& job, const Wren7Run& run)
{
	bool console_done = job.console_status == "done";

	if (run.guard != TGR_OK || run.done != console_done)
		return false;

	bool has_result = !job.reads.empty() && job.reads[0].addr == WREN7_RES_ADDR && job.reads[0].size >= WREN7_RES_SIZE;

	if (!console_done || !has_result)
		return true;

	return arm7_le32(job.console, WREN7_RES_MAGIC) == WREN7_MAGIC_DONE &&
		arm7_le32(run.readback, WREN7_RES_MAGIC) == WREN7_MAGIC_DONE &&
		arm7_le32(run.readback, WREN7_RES_EXIT) == arm7_le32(job.console, WREN7_RES_EXIT);
}
