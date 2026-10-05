## The SH4 as the emulator sets it up ##
#
#	sh4.core    SuperH4_impl, the CPU object of the emulator, on the interpreter: the state a reset leaves, the
#	            host rounding mode of every FPSCR.RM, the host FP mode of the thread that runs the CPU, the
#	            exception registers of TRAPA
#
#	Needs no vectors.
#

nulldc_add_libswirl_library(nulldc_sh4_cpu
  SHREC DYNAREC_NONE
  SOURCES hw/sh4/SuperH4_impl.cpp
)

find_package(Threads REQUIRED)

add_executable(sh4_core core_main.cpp)
target_link_libraries(sh4_core PRIVATE nulldc_sh4_interp nulldc_sh4_cpu nulldc_test_common Threads::Threads)

add_test(NAME sh4.core COMMAND sh4_core)
set_tests_properties(sh4.core PROPERTIES TIMEOUT 60)
