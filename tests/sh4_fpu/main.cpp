/*
	sh4_fpu <test>: the SH4 FPU instructions through the interpreter and through the dynarec, against what the
	hardware computes. The hardware's results are not in this repository: they are read from a shrike4-rtl
	checkout, and its headers are compiled in from there.

	  scalar    sh4.fpu.scalar, scalar.cpp: the console's records of the scalar instructions
*/
#include "fpu_suite.h"

int fpu_scalar_main(const FpuOptions& opt);

int main(int argc, char** argv)
{
	std::string test = argc > 1 ? argv[1] : "";

	FpuOptions opt;

	opt.data_dir = NULLDC_SHRIKE4_DIR;
	opt.baseline = NULLDC_BASELINE_SCALAR;

	if (!fpu_suite_options(argc, argv, test == "scalar" ? 2 : argc + 1, "sh4_fpu <scalar> [options]", opt))
		return FPU_EXIT_ERROR;

	return fpu_scalar_main(opt);
}
