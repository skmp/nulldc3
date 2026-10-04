## The SH4 dynarec ##
#
#	sh4.singlestep.dynarec    the single-step vectors through decoder -> shil -> x64 backend
#
#	It enters the blocks of the x64 backend directly (dynarec_runner.h), on other hosts it is skipped.
#

if(NOT ((${HOST_CPU} EQUAL ${CPU_X64}) AND (${FEAT_SHREC} EQUAL ${DYNAREC_JIT})))
  set(nulldc_sh4_dynarec_missing
      "the SH4 dynarec tests run the blocks of the x64 backend, this is not an x86-64 build with the SH4 recompiler")
  nulldc_add_skipped_test(sh4.singlestep.dynarec "${nulldc_sh4_dynarec_missing}")
  return()
endif()

# The interpreter is part of it: the recompiler falls back to its handlers
nulldc_add_libswirl_library(nulldc_sh4_dynarec
  SOURCES ${NULLDC_SH4_INTERP_SOURCES}
          hw/sh4/dyna/decoder.cpp
          hw/sh4/dyna/shil.cpp
          hw/sh4/dyna/driver.cpp
          hw/sh4/dyna/blockmanager.cpp
          jit/backend/x64/rec_x64.cpp
          deps/crypto/sha1.cpp
)

add_executable(sh4_singlestep_dynarec singlestep.cpp dynarec_runner.cpp dynarec_runner.h)
target_link_libraries(sh4_singlestep_dynarec PRIVATE nulldc_sh4_dynarec nulldc_test_common)
target_compile_definitions(sh4_singlestep_dynarec PRIVATE
  NULLDC_SH4_TESTS_DIR="${NULLDC_SH4_TESTS_DIR}"
  NULLDC_BASELINE="${CMAKE_CURRENT_SOURCE_DIR}/baseline_singlestep.txt"
)

if(NULLDC_HAVE_SH4_TESTS)
  add_test(NAME sh4.singlestep.dynarec COMMAND sh4_singlestep_dynarec)
  set_tests_properties(sh4.singlestep.dynarec PROPERTIES TIMEOUT 300)
else()
  nulldc_add_skipped_test(sh4.singlestep.dynarec "${NULLDC_SH4_TESTS_MISSING}")
endif()
