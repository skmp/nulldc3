cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

## debugger.sh4bp.*: SH4 code breakpoints and steps in the active backend ##
#
#	The mechanism (libswirl/debugger/dbg_sh4.cpp, hw/sh4/dyna, jit/backend/x64, the interpreter) linked with a
#	stand-in for run control and driven by guest programs the tests load themselves. README.md has the gates.
#
#	Stands alone, or as a directory of the tests project (add_subdirectory(debugger/sh4bp) in tests/CMakeLists.txt):
#
#	    cmake -S tests/debugger/sh4bp -B <build dir> -G Ninja && ninja -C <build dir> && ctest --test-dir <build dir>
#

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  find_program(CCACHE_FOUND ccache)
  if (CCACHE_FOUND AND NOT CMAKE_HOST_WIN32)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
  endif()

  project(nulldc-sh4bp-tests CXX)

  get_filename_component(nulldc_root_path "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
  set(nulldc_core_path  "${nulldc_root_path}/libswirl")
  set(nulldc_tests_path "${nulldc_root_path}/tests")

  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  endif()

  enable_testing()

  include("${nulldc_tests_path}/common/libswirl.cmake")
endif()

set(sh4bp_gates b1 b2 b3 b4 b5 b6 b7 b8 b9 b10 b12 switch ownslot dense b13 b14 slot midpair vector arm event threads)
if(WIN32)
  # the stack walks of the system's unwinder through the code region (gates.cpp, gate_unwind)
  list(APPEND sh4bp_gates unwind)
endif()

# the configurations that the gates on the booted guest are made in: engine, and "off" for idleskip off
set(sh4bp_boots jit interpreter fallback jit-idleskip-off fallback-idleskip-off)

set(sh4bp_boot_tests "")
foreach(boot ${sh4bp_boots})
  list(APPEND sh4bp_boot_tests boot.${boot})
endforeach()

# The tests run the blocks of the SH4 recompiler of the host through its main loop and RunBlock, with its native
# debugger checks (DebugChecks): the x64 backend (jit/backend/x64/rec_x64.cpp) on Linux and on Windows (its check stub
# with the home space of the Windows calling convention, and its own budget of checks), the arm64 backend
# (jit/backend/arm64/rec_arm64.cpp, with vixl) on Linux and on macOS. On arm64 also abi: the registers a caller keeps,
# around the main loop and RunBlock, through each of their exits (as unwind is Windows-only). b11 counts host
# instructions with the performance counters of Linux and says SKIPPED on another host, and where the kernel refuses
# them.
# On macOS the code region is MAP_JIT memory at the address of SH4_TCB, as the emulator maps it (stubs.cpp): a host
# that refuses it makes every program of this directory say "SKIPPED: the code region: ..." at its start, with the
# request, the error and the distance to SH4_TCB, which ctest counts as skipped
if((${HOST_CPU} EQUAL ${CPU_X64}) AND (${FEAT_SHREC} EQUAL ${DYNAREC_JIT}))
  set(sh4bp_backend_sources jit/backend/x64/rec_x64.cpp)
elseif((${HOST_CPU} EQUAL ${CPU_A64}) AND (${FEAT_SHREC} EQUAL ${DYNAREC_JIT}))
  file(GLOB sh4bp_vixl_sources RELATIVE "${nulldc_core_path}" "${nulldc_core_path}/deps/vixl/aarch64/*.cc")
  set(sh4bp_backend_sources jit/backend/arm64/rec_arm64.cpp
    deps/vixl/code-buffer-vixl.cc deps/vixl/cpu-features.cc deps/vixl/utils-vixl.cc deps/vixl/compiler-intrinsics-vixl.cc
    ${sh4bp_vixl_sources})
  list(APPEND sh4bp_gates abi)
else()
  set(sh4bp_skip "the sh4bp gates need a build with the SH4 recompiler of an x86-64 or an arm64 host")
  foreach(gate ${sh4bp_gates} selftest ${sh4bp_boot_tests} b11)
    add_test(NAME debugger.sh4bp.${gate}
      COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: ${sh4bp_skip}")
    set_tests_properties(debugger.sh4bp.${gate} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
  endforeach()
  return()
endif()

# On arm64 a program that the host refuses its code region says so (stubs.cpp): sh4bp_refusal gives the tests that
# start one the regular expression of that line
set(sh4bp_refused "")
if(${HOST_CPU} EQUAL ${CPU_A64})
  set(sh4bp_refused "SKIPPED: the code region")
endif()


## The call sites of run control ##
#
#	The mechanism is called from two files that belong to run control: hw/sh4/sh4_interrupts.cpp (the report of an
#	accepted interrupt or exception, the stop latch in UpdateINTC) and hw/sh4/interpr/sh4_opcodes.cpp (the flow
#	report of RTE, the stop latch in SLEEP). While the tree's files do not have those calls, the tests compile
#	copies that do: the same text with the calls put in, each at one place that has to be found exactly once.
#	A file that has its call is compiled as it is.
#

function(sh4bp_put text_var anchor replacement file)
  string(FIND "${${text_var}}" "${anchor}" first)
  string(FIND "${${text_var}}" "${anchor}" last REVERSE)
  if(first EQUAL -1 OR NOT first EQUAL last)
    message(FATAL_ERROR "sh4bp: ${file}: a call site of run control was not found exactly once:\n${anchor}")
  endif()
  string(REPLACE "${anchor}" "${replacement}" out "${${text_var}}")
  set(${text_var} "${out}" PARENT_SCOPE)
endfunction()

function(sh4bp_callsites out_var src marker)
  set(file "${nulldc_core_path}/${src}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  file(READ "${file}" text)

  string(FIND "${text}" "${marker}" has)
  if(NOT has EQUAL -1)
    set(${out_var} "${file}" PARENT_SCOPE)
    return()
  endif()

  if(src STREQUAL "hw/sh4/sh4_interrupts.cpp")
    sh4bp_put(text
"#include \"sh4_mmr.h\"
"
"#include \"sh4_mmr.h\"
#include \"debugger/debugger.h\"
" "${src}")

    sh4bp_put(text
"int UpdateINTC()
{
"
"int UpdateINTC()
{
	if (unlikely(dbg::g_stop_latched)) { dbg::g_intc_deferred = true; return 0; }

" "${src}")

    sh4bp_put(text
"bool Do_Interrupt(u32 intEvn)
{
	auto sh4mmr = sh4_cpu->sh4mmr.get();
"
"bool Do_Interrupt(u32 intEvn)
{
	auto sh4mmr = sh4_cpu->sh4mmr.get();

	dbg_sh4_OnException();
" "${src}")

    sh4bp_put(text
"bool Do_Exception(u32 epc, u32 expEvn, u32 CallVect)
{
	auto sh4mmr = sh4_cpu->sh4mmr.get();
"
"bool Do_Exception(u32 epc, u32 expEvn, u32 CallVect)
{
	auto sh4mmr = sh4_cpu->sh4mmr.get();

	dbg_sh4_OnException();
" "${src}")

  elseif(src STREQUAL "hw/sh4/interpr/sh4_opcodes.cpp")
    sh4bp_put(text
"#include \"../sh4_interrupts.h\"
"
"#include \"../sh4_interrupts.h\"
#include \"debugger/debugger.h\"
" "${src}")

    sh4bp_put(text
"	ExecuteDelayslot_RTE();
	next_pc = newpc;
	UpdateINTC();"
"	ExecuteDelayslot_RTE();
	next_pc = newpc;

	if (unlikely(dbg_sh4_stepout != 0))
		dbg_sh4_CheckFlow(DBG_SH4_FLOW_RTE);

	UpdateINTC();" "${src}")

    sh4bp_put(text
"	int i=0,s=1;

	while (!UpdateSystem_INTC())//448
	{
		if (i++>1000)
		{
			s=0;
			break;
		}
	}
"
"	int i=0,s=1;

	dbg::g_in_sleep = true;
	for (;;)
	{
		if (dbg::g_stop_latched)
		{
			s=0;
			dbg::g_sleep_cut = true;
			break;
		}
		if (UpdateSystem_INTC())//448
			break;
		if (i++>1000)
		{
			s=0;
			break;
		}
	}
	dbg::g_in_sleep = false;
" "${src}")

  else()
    message(FATAL_ERROR "sh4bp: no call sites are known for ${src}")
  endif()

  get_filename_component(name "${src}" NAME)
  get_filename_component(dir "${file}" DIRECTORY)
  set(copy "${CMAKE_CURRENT_BINARY_DIR}/callsites/${name}")

  # written only when it changes, so that a configure does not recompile it
  set(old "")
  if(EXISTS "${copy}")
    file(READ "${copy}" old)
  endif()
  if(NOT old STREQUAL text)
    file(WRITE "${copy}" "${text}")
  endif()

  # the copy includes the headers next to the file it was made from
  set_source_files_properties("${copy}" PROPERTIES INCLUDE_DIRECTORIES "${dir}")

  message(STATUS "sh4bp: ${src} is compiled with the call sites of run control put in")
  set(${out_var} "${copy}" PARENT_SCOPE)
endfunction()

sh4bp_callsites(sh4bp_interrupts_src "hw/sh4/sh4_interrupts.cpp" "dbg_sh4_OnException")
sh4bp_callsites(sh4bp_opcodes_src "hw/sh4/interpr/sh4_opcodes.cpp" "DBG_SH4_FLOW_RTE")


## The mechanism ##

set(sh4bp_sources ${NULLDC_SH4_INTERP_SOURCES})
list(REMOVE_ITEM sh4bp_sources hw/sh4/sh4_interrupts.cpp hw/sh4/interpr/sh4_opcodes.cpp)

# On Windows the emulator's unwind data of the code region (stubs.cpp hands the region to it, machine.cpp walks the
# stack through it at every stop)
set(sh4bp_host_sources "")
if(WIN32)
  set(sh4bp_host_sources windows/win_jit_unwind.cpp)
endif()

nulldc_add_libswirl_library(nulldc_sh4bp
  SOURCES
    ${sh4bp_sources}
    hw/sh4/dyna/decoder.cpp
    hw/sh4/dyna/shil.cpp
    hw/sh4/dyna/driver.cpp
    hw/sh4/dyna/blockmanager.cpp
    ${sh4bp_backend_sources}
    deps/crypto/sha1.cpp
    debugger/dbg_sh4.cpp
    ${sh4bp_host_sources}
)
target_sources(nulldc_sh4bp PRIVATE "${sh4bp_interrupts_src}" "${sh4bp_opcodes_src}")
target_compile_definitions(nulldc_sh4bp PUBLIC NULLDC_DEBUGGER=1)
# vixl's headers include each other by the names under deps/vixl (the emulator's include path has it as well)
if(${HOST_CPU} EQUAL ${CPU_A64})
  target_include_directories(nulldc_sh4bp PRIVATE "${nulldc_core_path}/deps/vixl")
endif()

add_executable(sh4bp_test
  sh4bp_test.cpp
  gates.cpp
  machine.cpp   machine.h
  stubs.cpp
  "${nulldc_core_path}/debugger/dbg_selftest_sh4bp_guest.inc"
)
target_link_libraries(sh4bp_test PRIVATE nulldc_sh4bp)

# sh4bp_refusal(<test>): a test whose program the host can refuse its code region (arm64) counts that as a skip
function(sh4bp_refusal test)
  if(sh4bp_refused)
    set_tests_properties(${test} PROPERTIES SKIP_REGULAR_EXPRESSION "${sh4bp_refused}")
  endif()
endfunction()

foreach(gate ${sh4bp_gates})
  add_test(NAME debugger.sh4bp.${gate} COMMAND sh4bp_test ${gate})
  set_tests_properties(debugger.sh4bp.${gate} PROPERTIES TIMEOUT 300)
  sh4bp_refusal(debugger.sh4bp.${gate})
endforeach()


## The selftest of the emulator, on this machine ##
#
#	libswirl/debugger/dbg_selftest_sh4bp.cpp runs in the emulator, on its park loop. Here the same tests run on
#	the stand-in for run control: a session per engine, and the comparison of what the sessions print for one
#	another. The tests on the booted guest are sessions of their own (selftest_boot.cmake).
#

add_executable(sh4bp_selftest
  selftest_shim.cpp
  machine.cpp   machine.h
  stubs.cpp
  "${nulldc_core_path}/debugger/dbg_selftest.cpp"
  "${nulldc_core_path}/debugger/dbg_selftest_sh4bp.cpp"
)
target_link_libraries(sh4bp_selftest PRIVATE nulldc_sh4bp)

add_test(NAME debugger.sh4bp.selftest
  COMMAND ${CMAKE_COMMAND} -DSELFTEST=$<TARGET_FILE:sh4bp_selftest> -P "${CMAKE_CURRENT_SOURCE_DIR}/selftest_compare.cmake")
set_tests_properties(debugger.sh4bp.selftest PROPERTIES TIMEOUT 600)
sh4bp_refusal(debugger.sh4bp.selftest)

# The gates on the booted guest: sessions of the tests sh4bpboot against sessions of their twins sh4bpbase
foreach(boot ${sh4bp_boots})
  string(REPLACE "-idleskip-off" "" engine "${boot}")
  set(idleskip on)
  if(NOT engine STREQUAL boot)
    set(idleskip off)
  endif()

  add_test(NAME debugger.sh4bp.boot.${boot}
    COMMAND ${CMAKE_COMMAND} -DSELFTEST=$<TARGET_FILE:sh4bp_selftest> -DENGINE=${engine} -DIDLESKIP=${idleskip}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/selftest_boot.cmake")
  set_tests_properties(debugger.sh4bp.boot.${boot} PROPERTIES TIMEOUT 900)
  sh4bp_refusal(debugger.sh4bp.boot.${boot})
endforeach()


## The cost for a guest that uses nothing of the mechanism (gate B11) ##
#
#	sh4bp_cost counts the host instructions of the workload. It is built twice from the same sources, with the
#	debugger and without; b11_compare.cmake runs both and applies the criteria (README.md).
#

nulldc_add_libswirl_library(nulldc_sh4bp_off
  SOURCES
    ${NULLDC_SH4_INTERP_SOURCES}
    hw/sh4/dyna/decoder.cpp
    hw/sh4/dyna/shil.cpp
    hw/sh4/dyna/driver.cpp
    hw/sh4/dyna/blockmanager.cpp
    ${sh4bp_backend_sources}
    deps/crypto/sha1.cpp
    ${sh4bp_host_sources}
)
if(${HOST_CPU} EQUAL ${CPU_A64})
  target_include_directories(nulldc_sh4bp_off PRIVATE "${nulldc_core_path}/deps/vixl")
endif()

add_executable(sh4bp_cost_on cost.cpp stubs.cpp "${nulldc_core_path}/debugger/dbg_selftest_sh4bp_guest.inc")
target_link_libraries(sh4bp_cost_on PRIVATE nulldc_sh4bp)

add_executable(sh4bp_cost_off cost.cpp stubs.cpp "${nulldc_core_path}/debugger/dbg_selftest_sh4bp_guest.inc")
target_link_libraries(sh4bp_cost_off PRIVATE nulldc_sh4bp_off)

add_test(NAME debugger.sh4bp.b11
  COMMAND ${CMAKE_COMMAND} -DCOST_ON=$<TARGET_FILE:sh4bp_cost_on> -DCOST_OFF=$<TARGET_FILE:sh4bp_cost_off>
    -P "${CMAKE_CURRENT_SOURCE_DIR}/b11_compare.cmake")
set_tests_properties(debugger.sh4bp.b11 PROPERTIES TIMEOUT 600 SKIP_REGULAR_EXPRESSION "SKIPPED:")


## Windows: the policy for a program that dies ##
#
#	No box, no crash report, one line and 128 + the signal's number (tests/common/win_policy.cpp, the tests
#	project's, which common.guard.windows holds on sh4bp_test): a gate that crashes ends its program as a fault ends
#	one on Linux. The vectored handler of the policy takes a fault in the code region, whose stack the filter of the
#	process would never see when the fault broke it
#

if(WIN32)
  foreach(program sh4bp_test sh4bp_selftest sh4bp_cost_on sh4bp_cost_off)
    target_sources(${program} PRIVATE "${nulldc_tests_path}/common/win_policy.cpp")
  endforeach()
endif()
