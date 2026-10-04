cmake_minimum_required(VERSION 3.16)
project(dbg_disasm_tests CXX)

# Standalone tests for libswirl/debugger/dbg_disasm. Nothing here is part of the emulator build.
#
#   cmake -S tests/debugger/disasm -B <builddir> -G Ninja && cmake --build <builddir> && ctest --test-dir <builddir>
#
# The oracle tests are reported as skipped when their tool is missing:
#   -DSH_OBJDUMP=<sh-elf-objdump>

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
add_test(NAME golden COMMAND disasm_test golden)

find_package(Python3 COMPONENTS Interpreter)

# A skipped test prints "SKIP: <reason>"; the scripts also return 77 when their oracle is missing
function(disasm_oracle_test name available reason)
  if(available)
    add_test(NAME ${name} COMMAND ${ARGN})
  else()
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E echo "SKIP: ${reason}")
  endif()
  set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION "SKIP: ")
endfunction()

find_program(SH_OBJDUMP
  NAMES sh-elf-objdump sh4-elf-objdump sh-none-elf-objdump sh4-linux-gnu-objdump
  HINTS /opt/toolchains/dc/sh-elf/bin)

disasm_oracle_test(sh4_objdump "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sh4_objdump.py"
  --objdump "${SH_OBJDUMP}" --test-bin $<TARGET_FILE:disasm_test> --workdir "${CMAKE_CURRENT_BINARY_DIR}/oracle")

disasm_oracle_test(sh4_opcode_table "${Python3_Interpreter_FOUND}" "python3 not found"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sh4_table_check.py"
  --opcode-list "${LIBSWIRL_DIR}/hw/sh4/sh4_opcode_list.cpp" --test-bin $<TARGET_FILE:disasm_test>)
