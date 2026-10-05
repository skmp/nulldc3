## What the suites share ##
#
#	nulldc_sh4_interp     the SH4 interpreter of libswirl, without the recompiler (FEAT_SHREC = DYNAREC_NONE)
#	nulldc_test_common    test_guard (die()/verify() trap, watchdog), the SH4 environment and the stubs that
#	                      stand in for the rest of the emulator, the single-step vector reader, guest memory,
#	                      policy, state helpers and suite runner
#
#	A suite links a libswirl library and nulldc_test_common. Stubs are only pulled out of nulldc_test_common
#	for symbols that the suite's libswirl objects leave undefined.
#

nulldc_add_libswirl_library(nulldc_sh4_interp
  SHREC DYNAREC_NONE
  SOURCES ${NULLDC_SH4_INTERP_SOURCES}
)

add_library(nulldc_test_common STATIC
  test_guard.cpp      test_guard.h
  sh4_env.cpp         sh4_env.h
  sh4_stubs.cpp
  sh4_stubs_nocpu.cpp
  sh4_stubs_norec.cpp
  sh4_stubs_rec.cpp
  ss_format.cpp       ss_format.h
  ss_memory.cpp       ss_memory.h
  ss_policy.cpp       ss_policy.h
  ss_state.cpp        ss_state.h
  ss_suite.cpp        ss_suite.h
)
target_link_libraries(nulldc_test_common PUBLIC nulldc_libswirl_headers)
target_include_directories(nulldc_test_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(${HOST_OS} EQUAL ${OS_WINDOWS})
  enable_language(C)
  target_sources(nulldc_test_common PRIVATE "${nulldc_core_path}/deps/dirent/dirent.c")
endif()

# The harness tested on itself: what it has to catch, it catches
add_executable(common_selftest selftest.cpp)
target_link_libraries(common_selftest PRIVATE nulldc_sh4_interp nulldc_test_common)
add_test(NAME common.selftest COMMAND common_selftest)
set_tests_properties(common.selftest PROPERTIES TIMEOUT 60)
