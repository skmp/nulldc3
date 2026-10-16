cmake_minimum_required(VERSION 3.16)
project(dbg_regs_tests CXX)

# Standalone tests for the device tree tables of libswirl/debugger (dbg_regs_tables.cpp), and under aica/ for
# the device state behind them that has a test without the machine. Nothing here is part of the emulator build;
# the table tests link nothing of the emulator, aica/ compiles the translation units it tests.
#
#   cmake -S tests/debugger/regs -B <builddir> -G Ninja && cmake --build <builddir> && ctest --test-dir <builddir>
#
# The panel id test compares with the devtools UI and is reported as skipped without it (default: the submodule
# devtools/ of the tree, else ../dreamcast-devtools beside it):
#   -DDEVTOOLS_DIR=<dreamcast-devtools checkout>
#
# The tests project (tests/CMakeLists.txt) adds this directory as well. What it sets is all optional here:
#   NULLDC_TEST_PREFIX          put in front of every test name (debugger.regs.)
#   NULLDC_DEVTOOLS_DIR         the dreamcast-devtools checkout, in place of DEVTOOLS_DIR
#   NULLDC_TESTS_REQUIRE_DATA   the panel id test fails without the devtools UI instead of being skipped

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
add_test(NAME ${NULLDC_TEST_PREFIX}tables COMMAND regs_test tables)

# The tables against the definitions they were written from: the NAME_addr lines of the on-chip modules and
# of the system bus
add_test(NAME ${NULLDC_TEST_PREFIX}sources COMMAND regs_test sources "${LIBSWIRL_DIR}")

# The panel ids against the list of the devtools UI
if(DEFINED NULLDC_DEVTOOLS_DIR)
  set(devtools_option NULLDC_DEVTOOLS_DIR)
else()
  # the submodule devtools/ of the tree, else a checkout beside it; a place counts when it has the list (an
  # uninitialised submodule is an empty directory, tests/devtools/lookup.cmake)
  include("${CMAKE_CURRENT_LIST_DIR}/../../devtools/lookup.cmake")
  nulldc_find_devtools(devtools_default "${DEVCAST_DIR}" src/lib/debuggerSchema.ts)
  if(NOT devtools_default)
    set(devtools_default "${DEVCAST_DIR}/../dreamcast-devtools")
  endif()
  set(DEVTOOLS_DIR "${devtools_default}" CACHE PATH "dreamcast-devtools checkout (panel id list)")
  set(devtools_option DEVTOOLS_DIR)
endif()
set(DEVTOOLS_SCHEMA "${${devtools_option}}/src/lib/debuggerSchema.ts")

# A skipped test prints "SKIP: <reason>". With NULLDC_TESTS_REQUIRE_DATA that is a failed test.
if(EXISTS "${DEVTOOLS_SCHEMA}")
  add_test(NAME ${NULLDC_TEST_PREFIX}panels COMMAND regs_test panels "${DEVTOOLS_SCHEMA}")
else()
  add_test(NAME ${NULLDC_TEST_PREFIX}panels COMMAND ${CMAKE_COMMAND} -E echo
    "SKIP: no ${DEVTOOLS_SCHEMA}; set -D${devtools_option}=<dreamcast-devtools checkout>")
endif()
if(NULLDC_TESTS_REQUIRE_DATA)
  set_tests_properties(${NULLDC_TEST_PREFIX}panels PROPERTIES FAIL_REGULAR_EXPRESSION "SKIP: ")
else()
  set_tests_properties(${NULLDC_TEST_PREFIX}panels PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: ")
endif()

add_subdirectory(aica)
