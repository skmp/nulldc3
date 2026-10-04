## SH4 single-step vectors through the interpreter ##

add_executable(sh4_singlestep_interp main.cpp)
target_link_libraries(sh4_singlestep_interp PRIVATE nulldc_sh4_interp nulldc_test_common)
target_compile_definitions(sh4_singlestep_interp PRIVATE
  NULLDC_SH4_TESTS_DIR="${NULLDC_SH4_TESTS_DIR}"
  NULLDC_BASELINE="${CMAKE_CURRENT_SOURCE_DIR}/baseline.txt"
)

if(NULLDC_HAVE_SH4_TESTS)
  add_test(NAME sh4.singlestep.interp COMMAND sh4_singlestep_interp --strict)
  set_tests_properties(sh4.singlestep.interp PROPERTIES TIMEOUT 300)
else()
  nulldc_add_skipped_test(sh4.singlestep.interp "${NULLDC_SH4_TESTS_MISSING}")
endif()
