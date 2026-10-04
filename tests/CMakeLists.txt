cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND AND NOT CMAKE_HOST_WIN32)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif()

project(nulldc-tests CXX)

# Standalone test project: cmake -S tests -B <build dir> -G Ninja && ninja -C <build dir> && ctest --test-dir <build dir>
# The suites compile hand-picked libswirl translation units, the emulator itself is not built here.

get_filename_component(nulldc_root_path "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(nulldc_core_path  "${nulldc_root_path}/libswirl")
set(nulldc_tests_path "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()


## Test data ##
#
#	The vectors are git submodules under tests/vendor, the paths can point at any other checkout
#

set(NULLDC_SH4_TESTS_DIR "${nulldc_tests_path}/vendor/sh4-tests"
    CACHE PATH "SingleStepTests/sh4 checkout (*.json.bin)")
set(NULLDC_ARM7_TESTS_DIR "${nulldc_tests_path}/vendor/arm7di-tests-dreamcast"
    CACHE PATH "arm7di-tests-dreamcast checkout (bins/*.s.bin)")

if(EXISTS "${CMAKE_SOURCE_DIR}/../../wren7-rtl")
  get_filename_component(nulldc_wren7_default "${CMAKE_SOURCE_DIR}/../../wren7-rtl" ABSOLUTE)
else()
  set(nulldc_wren7_default "")
endif()
set(NULLDC_WREN7_DIR "${nulldc_wren7_default}"
    CACHE PATH "wren7-rtl checkout (ARM7 console suites), empty: those tests are skipped")

file(GLOB nulldc_sh4_vectors "${NULLDC_SH4_TESTS_DIR}/*.json.bin")
if(nulldc_sh4_vectors)
  set(NULLDC_HAVE_SH4_TESTS ON)
else()
  set(NULLDC_HAVE_SH4_TESTS OFF)
  set(NULLDC_SH4_TESTS_MISSING
      "no SH4 single-step vectors in ${NULLDC_SH4_TESTS_DIR}, run: git submodule update --init tests/vendor/sh4-tests")
  message(WARNING "${NULLDC_SH4_TESTS_MISSING}\nThe sh4.* tests are registered as skipped.")
endif()

file(GLOB nulldc_arm7_bins "${NULLDC_ARM7_TESTS_DIR}/bins/*.s.bin")
if(nulldc_arm7_bins)
  set(NULLDC_HAVE_ARM7_TESTS ON)
else()
  set(NULLDC_HAVE_ARM7_TESTS OFF)
  set(NULLDC_ARM7_TESTS_MISSING
      "no ARM7 test binaries in ${NULLDC_ARM7_TESTS_DIR}/bins, run: git submodule update --init tests/vendor/arm7di-tests-dreamcast")
  message(WARNING "${NULLDC_ARM7_TESTS_MISSING}\nThe arm7.* tests that need them are registered as skipped.")
endif()

if(NULLDC_WREN7_DIR AND EXISTS "${NULLDC_WREN7_DIR}")
  set(NULLDC_HAVE_WREN7 ON)
else()
  set(NULLDC_HAVE_WREN7 OFF)
  set(NULLDC_WREN7_MISSING
      "no wren7-rtl checkout (NULLDC_WREN7_DIR is \"${NULLDC_WREN7_DIR}\"), configure with -DNULLDC_WREN7_DIR=<path to wren7-rtl>")
  message(STATUS "${NULLDC_WREN7_MISSING}")
  message(STATUS "The tests that replay the wren7 console suites are registered as skipped.")
endif()

enable_testing()

# Registers <name> as a test that ctest reports as skipped, with the reason in its output
function(nulldc_add_skipped_test name reason)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: ${reason}")
  set_tests_properties(${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
endfunction()

include(common/libswirl.cmake)

add_subdirectory(common)
add_subdirectory(sh4_dynarec)
add_subdirectory(arm7)
