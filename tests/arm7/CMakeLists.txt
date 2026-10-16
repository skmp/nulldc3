## ARM7 tests ##
#
#	nulldc_arm7_interp    the ARM7 interpreter, SoundCPU and the AICA's register block of libswirl, without the
#	                      recompiler (FEAT_AREC = DYNAREC_NONE). SoundCPU.cpp is compiled through
#	                      arm7_machine.cpp, which wraps it into the machine the tests run their guests on
#	nulldc_arm7_test      what the tests share: options, report, checks and baseline (arm7_suite), the console
#	                      jobs of wren7-rtl (wren7_job)
#
#	arm7_vendor           arm7.vendor: the programs of tests/vendor/arm7di-tests-dreamcast
#	arm7_directed         arm7.directed: single instructions with hand-written results, no data needed
#	arm7_wren7 <suite>    arm7.wren7.<suite>: console jobs of a wren7-rtl checkout against the console's results.
#	                      Nothing of wren7-rtl is in this repository, the tests are skipped without a checkout
#	                      (NULLDC_WREN7_DIR) or without its build outputs. A checkout that lacks anything else
#	                      fails them
#
#	Each of them is registered a second time as <name>.halves, with ARM7M_STEP=halves in its environment: the
#	machine then makes a step of ARM7Backend::StepIrq and ARM7Backend::StepExec, the two halves that the debug
#	loop of the sound burst runs in place of ARM7Backend::Step (arm7_machine.cpp). Same program, same
#	conditions, same baseline.
#
#	All of them are gated on baseline.txt, see README.md.
#

nulldc_add_libswirl_library(nulldc_arm7_interp
  AREC DYNAREC_NONE
  SOURCES hw/arm7/arm7.cpp hw/arm7/arm7_interpreter.cpp hw/aica/aica_mmio.cpp
)
target_sources(nulldc_arm7_interp PRIVATE arm7_machine.cpp arm7_machine.h)
target_include_directories(nulldc_arm7_interp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(nulldc_arm7_test STATIC
  arm7_suite.cpp      arm7_suite.h
  wren7_job.cpp       wren7_job.h
)
target_link_libraries(nulldc_arm7_test PUBLIC nulldc_test_common)
target_include_directories(nulldc_arm7_test PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(nulldc_arm7_test PRIVATE $<TARGET_PROPERTY:nulldc_arm7_interp,INTERFACE_COMPILE_DEFINITIONS>)

set(nulldc_arm7_baseline "${CMAKE_CURRENT_SOURCE_DIR}/baseline.txt")

# A test that finds its data missing when it runs says so and exits with this code. With --require-data it fails
set(nulldc_arm7_skip_code 77)

if(NULLDC_TESTS_REQUIRE_DATA)
  set(nulldc_arm7_require_data --require-data)
else()
  set(nulldc_arm7_require_data "")
endif()


add_executable(arm7_vendor vendor_main.cpp)
target_link_libraries(arm7_vendor PRIVATE nulldc_arm7_interp nulldc_arm7_test nulldc_test_common)
target_compile_definitions(arm7_vendor PRIVATE
  NULLDC_ARM7_TESTS_DIR="${NULLDC_ARM7_TESTS_DIR}"
  NULLDC_BASELINE="${nulldc_arm7_baseline}"
)

if(NULLDC_HAVE_ARM7_TESTS)
  add_test(NAME arm7.vendor COMMAND arm7_vendor --strict ${nulldc_arm7_require_data})
  set_tests_properties(arm7.vendor PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE ${nulldc_arm7_skip_code})
  add_test(NAME arm7.vendor.halves COMMAND arm7_vendor --strict ${nulldc_arm7_require_data})
  set_tests_properties(arm7.vendor.halves PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE ${nulldc_arm7_skip_code} ENVIRONMENT "ARM7M_STEP=halves")
else()
  nulldc_add_missing_data_test(arm7.vendor "${NULLDC_ARM7_TESTS_MISSING}")
  nulldc_add_missing_data_test(arm7.vendor.halves "${NULLDC_ARM7_TESTS_MISSING}")
endif()


add_executable(arm7_directed directed_main.cpp)
target_link_libraries(arm7_directed PRIVATE nulldc_arm7_interp nulldc_arm7_test nulldc_test_common)
target_compile_definitions(arm7_directed PRIVATE NULLDC_BASELINE="${nulldc_arm7_baseline}")

add_test(NAME arm7.directed COMMAND arm7_directed --strict)
set_tests_properties(arm7.directed PROPERTIES TIMEOUT 60)
add_test(NAME arm7.directed.halves COMMAND arm7_directed --strict)
set_tests_properties(arm7.directed.halves PROPERTIES TIMEOUT 60 ENVIRONMENT "ARM7M_STEP=halves")


add_executable(arm7_wren7 wren7_main.cpp)
target_link_libraries(arm7_wren7 PRIVATE nulldc_arm7_interp nulldc_arm7_test nulldc_test_common)
target_compile_definitions(arm7_wren7 PRIVATE
  NULLDC_WREN7_DIR="${NULLDC_WREN7_DIR}"
  NULLDC_BASELINE="${nulldc_arm7_baseline}"
)

if(NULLDC_HAVE_WREN7 AND NOT EXISTS "${NULLDC_WREN7_DIR}/build/hw/arm/stub.bin")
  message(STATUS "${NULLDC_WREN7_DIR}/build/hw/arm has no done stub: wren7-rtl's ARM programs are not built (make -C model/hw/arm)")
  message(STATUS "The tests that replay the wren7 console suites do not run until they are.")
endif()

foreach(suite vendor qemu_diff mulsem bringup)
  if(NULLDC_HAVE_WREN7)
    add_test(NAME arm7.wren7.${suite} COMMAND arm7_wren7 ${suite} --strict ${nulldc_arm7_require_data})
    set_tests_properties(arm7.wren7.${suite} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE ${nulldc_arm7_skip_code})
    add_test(NAME arm7.wren7.${suite}.halves COMMAND arm7_wren7 ${suite} --strict ${nulldc_arm7_require_data})
    set_tests_properties(arm7.wren7.${suite}.halves PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE ${nulldc_arm7_skip_code}
                         ENVIRONMENT "ARM7M_STEP=halves")
  else()
    nulldc_add_missing_data_test(arm7.wren7.${suite} "${NULLDC_WREN7_MISSING}")
    nulldc_add_missing_data_test(arm7.wren7.${suite}.halves "${NULLDC_WREN7_MISSING}")
  endif()
endforeach()
