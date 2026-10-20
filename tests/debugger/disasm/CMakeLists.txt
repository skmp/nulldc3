cmake_minimum_required(VERSION 3.16)
project(dbg_disasm_tests CXX)

# Standalone tests for libswirl/debugger/dbg_disasm. Nothing here is part of the emulator build.
#
#   cmake -S tests/debugger/disasm -B <builddir> -G Ninja && cmake --build <builddir> && ctest --test-dir <builddir>
#
# The oracle tests are reported as skipped when their tool is missing:
#   -DSH_OBJDUMP=<sh-elf-objdump>
#   -DARM_OBJDUMP=<arm-eabi-objdump>
#   -DDEVTOOLS_DIR=<dreamcast-devtools checkout with node_modules installed> (default: the submodule devtools/ of
#                   the tree, else ../dreamcast-devtools beside it): dsp_playground, and the second half of dsptest
#
# The tests project (tests/CMakeLists.txt) adds this directory as well. What it sets is all optional here:
#   NULLDC_TEST_PREFIX          put in front of every test name (debugger.disasm.)
#   NULLDC_DEVTOOLS_DIR         the dreamcast-devtools checkout, in place of DEVTOOLS_DIR
#   NULLDC_TESTS_REQUIRE_DATA   a test whose tool is missing fails instead of being skipped
#   NULLDC_WINRUN               Windows: the nulldc3-winrun.exe that dsptest runs inside (in the tests project by
#                               default the one tests/windows builds)
#
# On Windows the programs of these tests have the policy of the tests project for a program that dies
# (tests/common/win_policy.cpp: no box, no crash report, one line and 128 + the signal's number)

# The golden test walks a few million encodings
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DISASM_SANITIZE "Build with -fsanitize=address,undefined" OFF)

get_filename_component(nulldc_root_path "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(LIBSWIRL_DIR "${nulldc_root_path}/libswirl")

add_executable(disasm_test disasm_test.cpp "${LIBSWIRL_DIR}/debugger/dbg_disasm.cpp")
target_include_directories(disasm_test PRIVATE "${LIBSWIRL_DIR}" "${LIBSWIRL_DIR}/debugger")
if(WIN32)
  target_sources(disasm_test PRIVATE "${nulldc_root_path}/tests/common/win_policy.cpp")
endif()

if(MSVC)
  target_compile_options(disasm_test PRIVATE /W4 /permissive-)
  target_compile_definitions(disasm_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(disasm_test PRIVATE -Wall -Wextra -Wshadow -Wconversion -pedantic-errors)
  if(DISASM_SANITIZE)
    target_compile_options(disasm_test PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(disasm_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()

enable_testing()

# Always runs: checked-in golden encodings and the invariants that need no external tool
add_test(NAME ${NULLDC_TEST_PREFIX}golden COMMAND disasm_test golden)

find_package(Python3 COMPONENTS Interpreter)

# A skipped test prints "SKIP: <reason>"; the scripts also return 77 when their oracle is missing.
# With NULLDC_TESTS_REQUIRE_DATA either one is a failed test. An oracle has 600 s, then ctest ends it.
function(disasm_oracle_test name available reason)
  set(name "${NULLDC_TEST_PREFIX}${name}")
  if(available)
    add_test(NAME ${name} COMMAND ${ARGN})
    # an oracle that is a script of this directory writes no bytecode into the source tree
    set_tests_properties(${name} PROPERTIES ENVIRONMENT PYTHONDONTWRITEBYTECODE=1 TIMEOUT 600)
  else()
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E echo "SKIP: ${reason}")
  endif()
  if(NULLDC_TESTS_REQUIRE_DATA)
    set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "SKIP: ")
  else()
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION "SKIP: ")
  endif()
endfunction()

# An objdump oracle on a host that is not Linux (the Windows host, the Mac), where the tool is missing: a skip of
# the platform, also with NULLDC_TESTS_REQUIRE_DATA (which keeps a missing tool a failure on Linux)
function(disasm_objdump_test name tool_var tool_name)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ${tool_var})
    set(name "${NULLDC_TEST_PREFIX}${name}")
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E echo "SKIP: no ${tool_name} on this host")
    set_tests_properties(${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: ")
  else()
    disasm_oracle_test(${name} ${ARGN})
  endif()
endfunction()

find_program(SH_OBJDUMP
  NAMES sh-elf-objdump sh4-elf-objdump sh-none-elf-objdump sh4-linux-gnu-objdump
  HINTS /opt/toolchains/dc/sh-elf/bin)

disasm_objdump_test(sh4_objdump SH_OBJDUMP sh-elf-objdump "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sh4_objdump.py"
  --objdump "${SH_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test> --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle")

# The comparison itself: a wrong flag, target or literal in the dump has to come back as a difference.
# Own workdir: the image file must not be shared with sh4_objdump under ctest -j.
disasm_objdump_test(sh4_objdump_selftest SH_OBJDUMP sh-elf-objdump "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sh4_objdump.py" --self-test
  --objdump "${SH_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test>
  --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle/selftest")

disasm_oracle_test(sh4_opcode_table "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sh4_table_check.py"
  --opcode-list "${LIBSWIRL_DIR}/hw/sh4/sh4_opcode_list.cpp" --test-bin $<TARGET_FILE:disasm_test>)

find_program(ARM_OBJDUMP
  NAMES arm-eabi-objdump arm-none-eabi-objdump
  HINTS /opt/toolchains/dc/arm-eabi/bin /opt/toolchains/dc-dca3/arm-eabi/bin)

disasm_objdump_test(arm7_objdump ARM_OBJDUMP arm-eabi-objdump "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/arm7_objdump.py"
  --objdump "${ARM_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test> --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle")

# The comparison itself, as for SH4: a wrong info field, one that is missing and one that should not be there
# have to come back as a difference. Own workdir: the image file must not be shared with arm7_objdump under ctest -j.
disasm_objdump_test(arm7_objdump_selftest ARM_OBJDUMP arm-eabi-objdump "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/arm7_objdump.py" --self-test
  --objdump "${ARM_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test>
  --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle/selftest")

if(DEFINED NULLDC_DEVTOOLS_DIR)
  set(devtools_option NULLDC_DEVTOOLS_DIR)
else()
  # the submodule devtools/ of the tree, else a checkout beside it; a place counts when it has tsx (npm ci was run
  # there) and dspUtils.ts: an uninitialised submodule is an empty directory (tests/devtools/lookup.cmake)
  include("${CMAKE_CURRENT_LIST_DIR}/../../devtools/lookup.cmake")
  nulldc_find_devtools(devtools_default "${nulldc_root_path}" node_modules/.bin/tsx src/app/dsp/dspUtils.ts)
  if(NOT devtools_default)
    set(devtools_default "${nulldc_root_path}/../dreamcast-devtools")
  endif()
  set(DEVTOOLS_DIR "${devtools_default}" CACHE PATH "dreamcast-devtools checkout (DSP playground oracle)")
  set(devtools_option DEVTOOLS_DIR)
endif()
set(devtools_dir "${${devtools_option}}")
if(WIN32)
  # node_modules/.bin/tsx is a shell script there, which CreateProcess cannot start; npm writes tsx.cmd beside it
  set(TSX "${devtools_dir}/node_modules/.bin/tsx.cmd")
else()
  set(TSX "${devtools_dir}/node_modules/.bin/tsx")
endif()
set(DSP_UTILS "${devtools_dir}/src/app/dsp/dspUtils.ts")

if(devtools_dir AND EXISTS "${TSX}" AND EXISTS "${DSP_UTILS}")
  set(DSP_ORACLE_FOUND TRUE)
else()
  set(DSP_ORACLE_FOUND FALSE)
endif()
if(WIN32)
  # disasm_test writes its stdout in text mode there, every line ending in \r\n, and dsp_playground.mts takes its
  # lines apart at \n: every line it compares ends in a \r. A skip of the platform until one of the two is changed
  # (neither is a file of the Windows round's tests package), also with NULLDC_TESTS_REQUIRE_DATA
  add_test(NAME ${NULLDC_TEST_PREFIX}dsp_playground COMMAND ${CMAKE_COMMAND} -E echo
    "SKIP: on Windows disasm_test writes its lines in text mode, ending in CR LF, and dsp_playground.mts splits them at LF")
  set_tests_properties(${NULLDC_TEST_PREFIX}dsp_playground PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: ")
else()
  disasm_oracle_test(dsp_playground "${DSP_ORACLE_FOUND}"
    "no tsx / dspUtils.ts under \"${devtools_dir}\"; set -D${devtools_option}=<dreamcast-devtools checkout with node_modules>"
    "${TSX}" "${CMAKE_CURRENT_SOURCE_DIR}/dsp_playground.mts"
    --dsp-utils "${DSP_UTILS}" --test-bin $<TARGET_FILE:disasm_test>)
endif()

# The DSP program of the sounding guest (tests/debugger/guest/dsptest): its text, dsptest_dsp.txt, is the
# disassembly of the words its ELF loads, and with the checkout the playground's assembler gives those words for
# that text (dsptest_text.py). Without the checkout the first half runs and the test says "SKIP: "
if(DSP_ORACLE_FOUND)
  set(dsptest_playground --tsx "${TSX}" --dsp-utils "${DSP_UTILS}")
else()
  set(dsptest_playground "")
endif()
# On Windows the script runs inside an nulldc3-winrun of its own, as the harnesses of the tests run their programs
# (tests/common/emurun.py): tsx.cmd is cmd.exe, which starts node, which starts node, and the timeout of the script
# ends cmd.exe alone, behind which its read of their output waits for the two nodes. The job of nulldc3-winrun ends
# all of them, at its timeout too (420 s; the script's own is 300 s). The test passes only with its line of an exit
# status 0 (and without a "SKIP: " where NULLDC_TESTS_REQUIRE_DATA is set). Without NULLDC_WINRUN a build of this
# directory by itself runs the script as it is
set(dsptest_winrun "")
if(WIN32)
  if(NULLDC_WINRUN)
    set(dsptest_winrun "${NULLDC_WINRUN}" --timeout 420 --)
  elseif(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(dsptest_winrun "$<TARGET_FILE:nulldc3_winrun>" --timeout 420 --)
  endif()
endif()
disasm_oracle_test(dsptest "${Python3_Interpreter_FOUND}" "python3 not found"
  ${dsptest_winrun} "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/dsptest_text.py"
  --test-bin $<TARGET_FILE:disasm_test> --guest "${nulldc_root_path}/tests/debugger/guest/dsptest" ${dsptest_playground})
if(dsptest_winrun AND Python3_Interpreter_FOUND)
  # one expression: a ; would make three, any of which would do (the . stand for the ; of the line). node keeps its
  # files (tsx its cache) in TEMP, and npm's cache is where npm writes its logs: both in the build directory, not in
  # the profile
  set(dsptest_temp "${CMAKE_CURRENT_BINARY_DIR}/oracle/dsptest-tmp")
  file(MAKE_DIRECTORY "${dsptest_temp}")
  set_tests_properties(${NULLDC_TEST_PREFIX}dsptest PROPERTIES PASS_REGULAR_EXPRESSION
    "winrun: desktop [^\n]*winrun-[0-9]+: [0-9]+ window[^\n]*, 0 visible, 0 of the emulator's class. the program's exit status 0. winrun exits with 0"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;TEMP=${dsptest_temp};TMP=${dsptest_temp};npm_config_cache=${CMAKE_CURRENT_BINARY_DIR}/oracle/dsptest-npm-cache")
endif()
