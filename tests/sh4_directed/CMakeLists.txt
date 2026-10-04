## Directed SH4 vectors ##
#
#	sh4_directed_gen       writes hand-written vectors (operands at the edges, T clear and set) in the format of
#	                       the single-step vectors, at build time, into the build directory
#	sh4.directed.interp    the SH4 suites on them: the executables of sh4_singlestep and sh4_dynarec with
#	sh4.directed.dynarec   --vectors, each with a baseline of its own here
#	sh4.directed.differential
#
#	The vectors are made here, the tests need no submodule.
#

add_executable(sh4_directed_gen directed_gen.cpp)
target_link_libraries(sh4_directed_gen PRIVATE nulldc_test_common)

set(nulldc_sh4_directed_dir "${CMAKE_CURRENT_BINARY_DIR}/vectors")
file(MAKE_DIRECTORY "${nulldc_sh4_directed_dir}")

add_custom_command(
  OUTPUT "${nulldc_sh4_directed_dir}/generated.stamp"
  COMMAND sh4_directed_gen "${nulldc_sh4_directed_dir}"
  COMMAND ${CMAKE_COMMAND} -E touch "${nulldc_sh4_directed_dir}/generated.stamp"
  DEPENDS sh4_directed_gen
  COMMENT "Generating the directed SH4 vectors"
  VERBATIM
)
add_custom_target(sh4_directed_vectors ALL DEPENDS "${nulldc_sh4_directed_dir}/generated.stamp")

add_test(NAME sh4.directed.interp
  COMMAND sh4_singlestep_interp --strict --name sh4.directed.interp --vectors "${nulldc_sh4_directed_dir}"
          --baseline "${CMAKE_CURRENT_SOURCE_DIR}/baseline_interp.txt")
set_tests_properties(sh4.directed.interp PROPERTIES TIMEOUT 60)

if(TARGET sh4_singlestep_dynarec)
  add_test(NAME sh4.directed.dynarec
    COMMAND sh4_singlestep_dynarec --strict --name sh4.directed.dynarec --vectors "${nulldc_sh4_directed_dir}"
            --baseline "${CMAKE_CURRENT_SOURCE_DIR}/baseline_dynarec.txt")
  add_test(NAME sh4.directed.differential
    COMMAND sh4_differential --strict --name sh4.directed.differential --vectors "${nulldc_sh4_directed_dir}"
            --baseline "${CMAKE_CURRENT_SOURCE_DIR}/baseline_differential.txt")
  set_tests_properties(sh4.directed.dynarec sh4.directed.differential PROPERTIES TIMEOUT 60)
else()
  nulldc_add_skipped_test(sh4.directed.dynarec "${nulldc_sh4_dynarec_missing}")
  nulldc_add_skipped_test(sh4.directed.differential "${nulldc_sh4_dynarec_missing}")
endif()
