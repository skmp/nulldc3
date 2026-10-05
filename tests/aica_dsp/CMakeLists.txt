## AICA DSP core tests ##
#
#	The core is libswirl/hw/aica/dsp_core: one translation unit that needs nothing of the emulator. It is compiled
#	here as the emulator's compilers have to take it, without the permissive mode of the emulator build.
#
#	aica_dsp_api          aica.dsp.api: what the calls of the core promise each other. No data needed
#	aica_dsp_diff <set>   aica.dsp.diff.<set>: the core against the DSP of the caique sample model on generated
#	                      programs, step for step
#	aica_dsp_bench        samples a second, not a test
#
#	The model is caique-rtl's. Nothing of it is in this repository: aica_dsp_diff is compiled with its sources
#	from the checkout (NULLDC_CAIQUE_DIR). Without a checkout its tests are skipped. All are gated on
#	baseline.txt.
#

string(REPLACE "-fpermissive" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(nulldc_dsp_core_path "${nulldc_core_path}/hw/aica/dsp_core")
set(nulldc_dsp_baseline "${CMAKE_CURRENT_SOURCE_DIR}/baseline.txt")

add_library(nulldc_aica_dsp_core STATIC
  "${nulldc_dsp_core_path}/aica_dsp_core.cpp"
  "${nulldc_dsp_core_path}/aica_dsp_core.h"
)
target_include_directories(nulldc_aica_dsp_core PUBLIC "${nulldc_core_path}")
target_compile_features(nulldc_aica_dsp_core PUBLIC cxx_std_20)

if(NOT MSVC)
  target_compile_options(nulldc_aica_dsp_core PRIVATE -Wall -Wextra -Werror)
endif()

add_library(nulldc_aica_dsp_test STATIC dsp_suite.cpp dsp_suite.h)
target_include_directories(nulldc_aica_dsp_test PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nulldc_aica_dsp_test PUBLIC nulldc_aica_dsp_core)


add_executable(aica_dsp_api api_main.cpp)
target_link_libraries(aica_dsp_api PRIVATE nulldc_aica_dsp_test)
target_compile_definitions(aica_dsp_api PRIVATE NULLDC_BASELINE="${nulldc_dsp_baseline}")

add_test(NAME aica.dsp.api COMMAND aica_dsp_api --strict)
set_tests_properties(aica.dsp.api PROPERTIES TIMEOUT 120)

add_executable(aica_dsp_bench bench_main.cpp)
target_link_libraries(aica_dsp_bench PRIVATE nulldc_aica_dsp_core)


## Against the caique model ##

set(nulldc_dsp_diff_sets random sparse directed)

if(NOT NULLDC_HAVE_CAIQUE)
  foreach(set ${nulldc_dsp_diff_sets})
    nulldc_add_missing_data_test(aica.dsp.diff.${set} "${NULLDC_CAIQUE_MISSING}")
  endforeach()
else()
  # The model as it is in the checkout. Its warnings are its own
  add_library(nulldc_aica_dsp_caique STATIC
    caique_model.cpp  caique_model.h
    dsp_lockstep.cpp  dsp_lockstep.h
  )
  target_include_directories(nulldc_aica_dsp_caique PRIVATE "${NULLDC_CAIQUE_DIR}/model")
  target_link_libraries(nulldc_aica_dsp_caique PUBLIC nulldc_aica_dsp_test)

  if(NOT MSVC)
    set_source_files_properties(caique_model.cpp PROPERTIES COMPILE_OPTIONS "-w")
  endif()

  add_executable(aica_dsp_diff diff_main.cpp)
  target_link_libraries(aica_dsp_diff PRIVATE nulldc_aica_dsp_caique)
  target_compile_definitions(aica_dsp_diff PRIVATE NULLDC_BASELINE="${nulldc_dsp_baseline}")

  foreach(set ${nulldc_dsp_diff_sets})
    add_test(NAME aica.dsp.diff.${set} COMMAND aica_dsp_diff ${set} --strict)
    set_tests_properties(aica.dsp.diff.${set} PROPERTIES TIMEOUT 300)
  endforeach()
endif()
