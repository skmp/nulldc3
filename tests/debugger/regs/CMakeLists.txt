cmake_minimum_required(VERSION 3.16)
project(dbg_regs_tests CXX)

# Standalone tests for the device tree tables of libswirl/debugger (dbg_regs_tables.cpp). Nothing here is part
# of the emulator build, and nothing of the emulator is linked.
#
#   cmake -S tests/debugger/regs -B <builddir> -G Ninja && cmake --build <builddir> && ctest --test-dir <builddir>
#
# The panel id test compares with the devtools UI and is reported as skipped without it:
#   -DDEVTOOLS_DIR=<dreamcast-devtools checkout>

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(REGS_SANITIZE "Build with -fsanitize=address,undefined" OFF)

get_filename_component(DEVCAST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(LIBSWIRL_DIR "${DEVCAST_DIR}/libswirl")

add_executable(regs_test regs_test.cpp "${LIBSWIRL_DIR}/debugger/dbg_regs_tables.cpp")
target_include_directories(regs_test PRIVATE "${LIBSWIRL_DIR}" "${LIBSWIRL_DIR}/debugger")

if(MSVC)
  target_compile_options(regs_test PRIVATE /W4 /permissive-)
  target_compile_definitions(regs_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(regs_test PRIVATE -Wall -Wextra -Wshadow -Wconversion -pedantic-errors)
  if(REGS_SANITIZE)
    target_compile_options(regs_test PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(regs_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()

enable_testing()

# The tables by themselves: structure, names, widths, keys, events, panels, the paths clients rely on
add_test(NAME tables COMMAND regs_test tables)

# The tables against the definitions they were written from: the NAME_addr lines of the on-chip modules and
# of the system bus
add_test(NAME sources COMMAND regs_test sources "${LIBSWIRL_DIR}")

# The panel ids against the list of the devtools UI
set(DEVTOOLS_DIR "${DEVCAST_DIR}/../dreamcast-devtools" CACHE PATH "dreamcast-devtools checkout (panel id list)")
set(DEVTOOLS_SCHEMA "${DEVTOOLS_DIR}/src/lib/debuggerSchema.ts")

if(EXISTS "${DEVTOOLS_SCHEMA}")
  add_test(NAME panels COMMAND regs_test panels "${DEVTOOLS_SCHEMA}")
else()
  add_test(NAME panels COMMAND ${CMAKE_COMMAND} -E echo
    "SKIP: no ${DEVTOOLS_SCHEMA}; set -DDEVTOOLS_DIR=<dreamcast-devtools checkout>")
endif()
set_tests_properties(panels PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: ")
