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

set(sh4bp_gates b1 b2 b3 b4 b5 b6 b7 b8 b9 b10 b12 b13 b14 arm)

# The tests run the blocks of the x64 backend through its main loop
if(NOT ((${HOST_CPU} EQUAL ${CPU_X64}) AND (${FEAT_SHREC} EQUAL ${DYNAREC_JIT})))
  foreach(gate ${sh4bp_gates} selftest)
    add_test(NAME debugger.sh4bp.${gate}
      COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: the sh4bp gates need an x86-64 build with the SH4 recompiler")
    set_tests_properties(debugger.sh4bp.${gate} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
  endforeach()
  return()
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

nulldc_add_libswirl_library(nulldc_sh4bp
  SOURCES
    ${sh4bp_sources}
    hw/sh4/dyna/decoder.cpp
    hw/sh4/dyna/shil.cpp
    hw/sh4/dyna/driver.cpp
    hw/sh4/dyna/blockmanager.cpp
    jit/backend/x64/rec_x64.cpp
    deps/crypto/sha1.cpp
    debugger/dbg_sh4.cpp
)
target_sources(nulldc_sh4bp PRIVATE "${sh4bp_interrupts_src}" "${sh4bp_opcodes_src}")
target_compile_definitions(nulldc_sh4bp PUBLIC NULLDC_DEBUGGER=1)

add_executable(sh4bp_test
  sh4bp_test.cpp
  gates.cpp
  machine.cpp   machine.h
  stubs.cpp
  guest.inc
)
target_link_libraries(sh4bp_test PRIVATE nulldc_sh4bp)

foreach(gate ${sh4bp_gates})
  add_test(NAME debugger.sh4bp.${gate} COMMAND sh4bp_test ${gate})
  set_tests_properties(debugger.sh4bp.${gate} PROPERTIES TIMEOUT 300)
endforeach()


## The selftest of the emulator, on this machine ##
#
#	libswirl/debugger/dbg_selftest_sh4bp.cpp runs in the emulator, on its park loop. Here the same tests run on
#	the stand-in for run control: a session per engine, and the comparison of what the sessions print for one
#	another.
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
