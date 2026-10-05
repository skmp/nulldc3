## SH4 FPU against the hardware ##
#
#	sh4.fpu.scalar    the console's records of the scalar FPU instructions, through the interpreter and the dynarec
#
#	The console's records are shrike4-rtl's. Nothing of it is in this repository: its headers are compiled in
#	from the checkout (NULLDC_SHRIKE4_DIR) and its data is read from there. Without a checkout the test is
#	skipped. It is gated on a baseline.
#

if(EXISTS "${CMAKE_SOURCE_DIR}/../../shrike4-rtl")
  get_filename_component(nulldc_shrike4_default "${CMAKE_SOURCE_DIR}/../../shrike4-rtl" ABSOLUTE)
else()
  set(nulldc_shrike4_default "")
endif()
set(NULLDC_SHRIKE4_DIR "${nulldc_shrike4_default}"
    CACHE PATH "shrike4-rtl checkout (SH4 FPU console records), empty: those tests are skipped")

# What the tests are compiled with. The data files are looked for when a test runs
set(nulldc_shrike4_headers
  "${NULLDC_SHRIKE4_DIR}/tools/hw/fpdn/fpdn_vec.h"
)

set(NULLDC_HAVE_SHRIKE4 OFF)

if(NOT NULLDC_SHRIKE4_DIR OR NOT EXISTS "${NULLDC_SHRIKE4_DIR}")
  set(NULLDC_SHRIKE4_MISSING
      "no shrike4-rtl checkout (NULLDC_SHRIKE4_DIR is \"${NULLDC_SHRIKE4_DIR}\"), configure with -DNULLDC_SHRIKE4_DIR=<path to shrike4-rtl>")
else()
  set(NULLDC_HAVE_SHRIKE4 ON)

  foreach(header ${nulldc_shrike4_headers})
    if(NULLDC_HAVE_SHRIKE4 AND NOT EXISTS "${header}")
      set(NULLDC_HAVE_SHRIKE4 OFF)
      set(NULLDC_SHRIKE4_MISSING "${header} is not there: NULLDC_SHRIKE4_DIR is not a shrike4-rtl checkout with its FPU console records")
    endif()
  endforeach()
endif()

if(NOT NULLDC_HAVE_SHRIKE4)
  message(STATUS "${NULLDC_SHRIKE4_MISSING}")
  message(STATUS "The tests that compare the SH4 FPU with the console's results do not run.")
endif()

foreach(test scalar)
  if(NOT TARGET nulldc_sh4_dynarec)
    nulldc_add_skipped_test(sh4.fpu.${test} "${nulldc_sh4_dynarec_missing}")
  elseif(NOT NULLDC_HAVE_SHRIKE4)
    nulldc_add_missing_data_test(sh4.fpu.${test} "${NULLDC_SHRIKE4_MISSING}")
  endif()
endforeach()

if(NOT TARGET nulldc_sh4_dynarec OR NOT NULLDC_HAVE_SHRIKE4)
  return()
endif()

# A test that finds its data missing when it runs says so and exits with this code. With --require-data it fails
set(nulldc_sh4_fpu_skip_code 77)

if(NULLDC_TESTS_REQUIRE_DATA)
  set(nulldc_sh4_fpu_require_data --require-data)
else()
  set(nulldc_sh4_fpu_require_data "")
endif()

# The dynarec is entered the way sh4.singlestep.dynarec and sh4.differential enter it
add_executable(sh4_fpu
  main.cpp
  fpu_suite.cpp   fpu_suite.h
  scalar.cpp
  "${nulldc_tests_path}/sh4_dynarec/dynarec_runner.cpp"
  "${nulldc_tests_path}/sh4_dynarec/dynarec_runner.h"
)
target_link_libraries(sh4_fpu PRIVATE nulldc_sh4_dynarec nulldc_test_common)
target_include_directories(sh4_fpu PRIVATE
  "${nulldc_tests_path}/sh4_dynarec"
  "${NULLDC_SHRIKE4_DIR}/tools/hw/fpdn"
)
target_compile_definitions(sh4_fpu PRIVATE
  NULLDC_SHRIKE4_DIR="${NULLDC_SHRIKE4_DIR}"
  NULLDC_BASELINE_SCALAR="${CMAKE_CURRENT_SOURCE_DIR}/baseline_scalar.txt"
)

foreach(test scalar)
  add_test(NAME sh4.fpu.${test} COMMAND sh4_fpu ${test} --strict ${nulldc_sh4_fpu_require_data})
  set_tests_properties(sh4.fpu.${test} PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE ${nulldc_sh4_fpu_skip_code})
endforeach()
