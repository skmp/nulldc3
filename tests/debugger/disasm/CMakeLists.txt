cmake_minimum_required(VERSION 3.16)
project(dbg_disasm_tests CXX)

# Standalone tests for libswirl/debugger/dbg_disasm. Nothing here is part of the emulator build.
#
#   cmake -S tests/debugger/disasm -B <builddir> -G Ninja && cmake --build <builddir> && ctest --test-dir <builddir>
#
# The oracle tests are reported as skipped when their tool is missing:
#   -DSH_OBJDUMP=<sh-elf-objdump>
#   -DARM_OBJDUMP=<arm-eabi-objdump>
#   -DDEVTOOLS_DIR=<dreamcast-devtools checkout with node_modules installed>
#
# The tests project (tests/CMakeLists.txt) adds this directory as well. What it sets is all optional here:
#   NULLDC_TEST_PREFIX          put in front of every test name (debugger.disasm.)
#   NULLDC_DEVTOOLS_DIR         the dreamcast-devtools checkout, in place of DEVTOOLS_DIR
#   NULLDC_TESTS_REQUIRE_DATA   a test whose tool is missing fails instead of being skipped

# The golden test walks a few million encodings
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DISASM_SANITIZE "Build with -fsanitize=address,undefined" OFF)

get_filename_component(DEVCAST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(LIBSWIRL_DIR "${DEVCAST_DIR}/libswirl")

add_executable(disasm_test disasm_test.cpp "${LIBSWIRL_DIR}/debugger/dbg_disasm.cpp")
target_include_directories(disasm_test PRIVATE "${LIBSWIRL_DIR}" "${LIBSWIRL_DIR}/debugger")

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
# With NULLDC_TESTS_REQUIRE_DATA either one is a failed test.
function(disasm_oracle_test name available reason)
  set(name "${NULLDC_TEST_PREFIX}${name}")
  if(available)
    add_test(NAME ${name} COMMAND ${ARGN})
    # an oracle that is a script of this directory writes no bytecode into the source tree
    set_tests_properties(${name} PROPERTIES ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
  else()
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E echo "SKIP: ${reason}")
  endif()
  if(NULLDC_TESTS_REQUIRE_DATA)
    set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "SKIP: ")
  else()
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION "SKIP: ")
  endif()
endfunction()

find_program(SH_OBJDUMP
  NAMES sh-elf-objdump sh4-elf-objdump sh-none-elf-objdump sh4-linux-gnu-objdump
  HINTS /opt/toolchains/dc/sh-elf/bin)

disasm_oracle_test(sh4_objdump "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sh4_objdump.py"
  --objdump "${SH_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test> --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle")

# The comparison itself: a wrong flag, target or literal in the dump has to come back as a difference.
# Own workdir: the image file must not be shared with sh4_objdump under ctest -j.
disasm_oracle_test(sh4_objdump_selftest "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sh4_objdump.py" --self-test
  --objdump "${SH_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test>
  --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle/selftest")

disasm_oracle_test(sh4_opcode_table "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sh4_table_check.py"
  --opcode-list "${LIBSWIRL_DIR}/hw/sh4/sh4_opcode_list.cpp" --test-bin $<TARGET_FILE:disasm_test>)

find_program(ARM_OBJDUMP
  NAMES arm-eabi-objdump arm-none-eabi-objdump
  HINTS /opt/toolchains/dc/arm-eabi/bin /opt/toolchains/dc-dca3/arm-eabi/bin)

disasm_oracle_test(arm7_objdump "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/arm7_objdump.py"
  --objdump "${ARM_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test> --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle")

# The comparison itself, as for SH4: a wrong info field, one that is missing and one that should not be there
# have to come back as a difference. Own workdir: the image file must not be shared with arm7_objdump under ctest -j.
disasm_oracle_test(arm7_objdump_selftest "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/arm7_objdump.py" --self-test
  --objdump "${ARM_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test>
  --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle/selftest")

if(DEFINED NULLDC_DEVTOOLS_DIR)
  set(devtools_option NULLDC_DEVTOOLS_DIR)
else()
  set(DEVTOOLS_DIR "${DEVCAST_DIR}/../dreamcast-devtools" CACHE PATH "dreamcast-devtools checkout (DSP playground oracle)")
  set(devtools_option DEVTOOLS_DIR)
endif()
set(devtools_dir "${${devtools_option}}")
set(TSX "${devtools_dir}/node_modules/.bin/tsx")
set(DSP_UTILS "${devtools_dir}/src/app/dsp/dspUtils.ts")

if(devtools_dir AND EXISTS "${TSX}" AND EXISTS "${DSP_UTILS}")
  set(DSP_ORACLE_FOUND TRUE)
else()
  set(DSP_ORACLE_FOUND FALSE)
endif()
disasm_oracle_test(dsp_playground "${DSP_ORACLE_FOUND}"
  "no tsx / dspUtils.ts under \"${devtools_dir}\"; set -D${devtools_option}=<dreamcast-devtools checkout with node_modules>"
  "${TSX}" "${CMAKE_CURRENT_SOURCE_DIR}/dsp_playground.mts"
  --dsp-utils "${DSP_UTILS}" --test-bin $<TARGET_FILE:disasm_test>)
