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
#	The vectors are git submodules under tests/vendor, the paths can point at any other checkout.
#	A test whose data is missing is reported as skipped, and ctest does not fail on skipped tests:
#	NULLDC_TESTS_REQUIRE_DATA makes it a failed test instead.
#

option(NULLDC_TESTS_REQUIRE_DATA "Missing test data (submodules, wren7-rtl checkout and its build outputs) fails the tests instead of skipping them" OFF)

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
  message(WARNING "${NULLDC_SH4_TESTS_MISSING}\nThe sh4.* tests that need them do not run.")
endif()

file(GLOB nulldc_arm7_bins "${NULLDC_ARM7_TESTS_DIR}/bins/*.s.bin")
if(nulldc_arm7_bins)
  set(NULLDC_HAVE_ARM7_TESTS ON)
else()
  set(NULLDC_HAVE_ARM7_TESTS OFF)
  set(NULLDC_ARM7_TESTS_MISSING
      "no ARM7 test binaries in ${NULLDC_ARM7_TESTS_DIR}/bins, run: git submodule update --init tests/vendor/arm7di-tests-dreamcast")
  message(WARNING "${NULLDC_ARM7_TESTS_MISSING}\nThe arm7.* tests that need them do not run.")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/../../caique-rtl")
  get_filename_component(nulldc_caique_default "${CMAKE_SOURCE_DIR}/../../caique-rtl" ABSOLUTE)
else()
  set(nulldc_caique_default "")
endif()
set(NULLDC_CAIQUE_DIR "${nulldc_caique_default}"
    CACHE PATH "caique-rtl checkout (AICA sample model and console captures), empty: the tests that need it are skipped")

if(NULLDC_WREN7_DIR AND EXISTS "${NULLDC_WREN7_DIR}")
  set(NULLDC_HAVE_WREN7 ON)
else()
  set(NULLDC_HAVE_WREN7 OFF)
  set(NULLDC_WREN7_MISSING
      "no wren7-rtl checkout (NULLDC_WREN7_DIR is \"${NULLDC_WREN7_DIR}\"), configure with -DNULLDC_WREN7_DIR=<path to wren7-rtl>")
  message(STATUS "${NULLDC_WREN7_MISSING}")
  message(STATUS "The tests that replay the wren7 console suites do not run.")
endif()

# What the AICA DSP tests compile of caique-rtl: its sample model and the DSP programs it ran on a console
set(NULLDC_HAVE_CAIQUE OFF)

if(NOT NULLDC_CAIQUE_DIR OR NOT EXISTS "${NULLDC_CAIQUE_DIR}")
  set(NULLDC_CAIQUE_MISSING
      "no caique-rtl checkout (NULLDC_CAIQUE_DIR is \"${NULLDC_CAIQUE_DIR}\"), configure with -DNULLDC_CAIQUE_DIR=<path to caique-rtl>")
elseif(NOT EXISTS "${NULLDC_CAIQUE_DIR}/model/sample-model/aica_model.cpp" OR NOT EXISTS "${NULLDC_CAIQUE_DIR}/model/cases/aica_io.h")
  set(NULLDC_CAIQUE_MISSING
      "${NULLDC_CAIQUE_DIR} has no model/sample-model/aica_model.cpp or no model/cases/aica_io.h: NULLDC_CAIQUE_DIR is not a caique-rtl checkout")
else()
  set(NULLDC_HAVE_CAIQUE ON)
endif()

if(NULLDC_HAVE_CAIQUE)
  # the DSP programs of caique-rtl are C
  enable_language(C)
else()
  message(STATUS "${NULLDC_CAIQUE_MISSING}")
  message(STATUS "The tests that compare the AICA DSP core with the caique model and with its console captures do not run.")
endif()

enable_testing()

# Registers <name> as a test that ctest reports as skipped, with the reason in its output
function(nulldc_add_skipped_test name reason)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: ${reason}")
  set_tests_properties(${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
endfunction()

# Registers <name> for a test whose data is not there: skipped, or failed with NULLDC_TESTS_REQUIRE_DATA
function(nulldc_add_missing_data_test name reason)
  if(NULLDC_TESTS_REQUIRE_DATA)
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E echo "MISSING: ${reason}")
    set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "MISSING:")
  else()
    nulldc_add_skipped_test(${name} "${reason}")
  endif()
endfunction()

include(common/libswirl.cmake)

add_subdirectory(common)
add_subdirectory(sh4_singlestep)
add_subdirectory(sh4_dynarec)
add_subdirectory(sh4_directed)
add_subdirectory(sh4_core)
add_subdirectory(arm7)
add_subdirectory(sh4_fpu)
add_subdirectory(cfg)
add_subdirectory(run_control)
add_subdirectory(system)
add_subdirectory(aica_dsp)
