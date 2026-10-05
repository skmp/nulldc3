/*
	sh4_fpu <test>: the SH4 FPU instructions through the interpreter and through the dynarec, against what the
	hardware computes. The hardware's results are not in this repository: they are read from a shrike4-rtl
	checkout, and its headers are compiled in from there (README.md).

	  scalar    sh4.fpu.scalar, scalar.cpp: the console's records of the scalar instructions
	  vector    sh4.fpu.vector, vector.cpp: FIPR, FTRV, FSCA and FSRRA against shrike4-rtl's bit-exact model, and
	            the console's records that prove the model is called the right way
*/
#include "fpu_suite.h"

int fpu_scalar_main(const FpuOptions& opt);
int fpu_vector_main(const FpuOptions& opt);

int main(int argc, char** argv)
{
	std::string test = argc > 1 ? argv[1] : "";
	bool scalar = test == "scalar";

	FpuOptions opt;

	opt.data_dir = NULLDC_SHRIKE4_DIR;
	opt.baseline = scalar ? NULLDC_BASELINE_SCALAR : NULLDC_BASELINE_VECTOR;

	if (!fpu_suite_options(argc, argv, scalar || test == "vector" ? 2 : argc + 1, "sh4_fpu <scalar|vector> [options]", opt))
		return FPU_EXIT_ERROR;

	return scalar ? fpu_scalar_main(opt) : fpu_vector_main(opt);
}
