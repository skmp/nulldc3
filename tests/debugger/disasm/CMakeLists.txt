cmake_minimum_required(VERSION 3.16)
project(dbg_disasm_tests CXX)

# Standalone tests for libswirl/debugger/dbg_disasm. Nothing here is part of the emulator build.
#
#   cmake -S tests/debugger/disasm -B <builddir> -G Ninja && cmake --build <builddir> && ctest --test-dir <builddir>

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
