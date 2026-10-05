## AICA DSP core tests ##
#
#	The core is libswirl/hw/aica/dsp_core: one translation unit that needs nothing of the emulator. It is compiled
#	here as the emulator's compilers have to take it, without the permissive mode of the emulator build.
#
#	aica_dsp_api          aica.dsp.api: what the calls of the core promise each other. No data needed
#	aica_dsp_diff <set>   aica.dsp.diff.<set>: the core against the DSP of the caique sample model on generated
#	                      programs, step for step
#	aica_dsp_cases <case> aica.dsp.case.<case>: the DSP programs caique ran on a Dreamcast, on the model with the
#	                      core in step and on the core alone, against the console's files
#	aica_dsp_wasm_trace   what aica.dsp.wasm replays on aica_dsp_core.wasm: wasm_test.mjs, with node
#	aica_dsp_bench        samples a second, not a test
#
#	The model, the programs and the console's files are caique-rtl's and stay there: the tests are compiled with
#	its sources from the checkout (NULLDC_CAIQUE_DIR) and read its captures from there. What this directory has
#	of caique is the harness around them: cases_main.cpp and dsp_lockstep.cpp name the caique files they follow,
#	with the licence. Without a checkout the tests are skipped. All but aica.dsp.wasm are gated on baseline.txt,
#	see README.md.
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

# A test that finds its data missing when it runs says so and exits with this code. With --require-data it fails
set(nulldc_dsp_skip_code 77)

if(NULLDC_TESTS_REQUIRE_DATA)
  set(nulldc_dsp_require_data --require-data)
else()
  set(nulldc_dsp_require_data "")
endif()


add_executable(aica_dsp_api api_main.cpp)
target_link_libraries(aica_dsp_api PRIVATE nulldc_aica_dsp_test)
target_compile_definitions(aica_dsp_api PRIVATE NULLDC_BASELINE="${nulldc_dsp_baseline}")

add_test(NAME aica.dsp.api COMMAND aica_dsp_api --strict)
set_tests_properties(aica.dsp.api PROPERTIES TIMEOUT 120)

add_executable(aica_dsp_bench bench_main.cpp)
target_link_libraries(aica_dsp_bench PRIVATE nulldc_aica_dsp_core)


## Against the caique model and its console captures ##

set(nulldc_dsp_diff_sets random sparse directed)

# The cases, and the ones that play a channel or are too long for the model: those run on one machine only
set(nulldc_dsp_cases dsp_basic dsp_mem dsp_mem2 dsp_mem3 dsp_wslot dsp_unpack dsp_pack dsp_temp dsp_coll sh4_latch mdec_hold)
set(nulldc_dsp_cases_model_only dsp_coll sh4_latch mdec_hold)
set(nulldc_dsp_cases_core_only dsp_pack)

if(NOT NULLDC_HAVE_CAIQUE)
  foreach(set ${nulldc_dsp_diff_sets})
    nulldc_add_missing_data_test(aica.dsp.diff.${set} "${NULLDC_CAIQUE_MISSING}")
  endforeach()

  foreach(case ${nulldc_dsp_cases})
    nulldc_add_missing_data_test(aica.dsp.case.${case} "${NULLDC_CAIQUE_MISSING}")
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

  # Every case is a C program with a test_main and static data of its own: one object per machine it runs on, with
  # its entry point under a name of its own
  add_executable(aica_dsp_cases cases_main.cpp)
  target_link_libraries(aica_dsp_cases PRIVATE nulldc_aica_dsp_caique)
  target_compile_definitions(aica_dsp_cases PRIVATE
    NULLDC_BASELINE="${nulldc_dsp_baseline}"
    NULLDC_CAIQUE_DIR="${NULLDC_CAIQUE_DIR}"
  )

  foreach(case ${nulldc_dsp_cases})
    set(machines model core)

    if(case IN_LIST nulldc_dsp_cases_model_only)
      set(machines model)
    elseif(case IN_LIST nulldc_dsp_cases_core_only)
      set(machines core)
    endif()

    foreach(machine ${machines})
      add_library(nulldc_caique_case_${case}_${machine} OBJECT "${NULLDC_CAIQUE_DIR}/model/cases/${case}.c")
      target_compile_definitions(nulldc_caique_case_${case}_${machine} PRIVATE
        CASE="${case}" test_main=caique_case_${case}_${machine})
      set_target_properties(nulldc_caique_case_${case}_${machine} PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

      if(NOT MSVC)
        target_compile_options(nulldc_caique_case_${case}_${machine} PRIVATE -w)
      endif()

      target_sources(aica_dsp_cases PRIVATE $<TARGET_OBJECTS:nulldc_caique_case_${case}_${machine}>)
    endforeach()

    add_test(NAME aica.dsp.case.${case} COMMAND aica_dsp_cases ${case} --strict ${nulldc_dsp_require_data})
    set_tests_properties(aica.dsp.case.${case} PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE ${nulldc_dsp_skip_code})
  endforeach()

  # mdec_hold ends with the measurement that caique's harness runs in front of every case
  add_library(nulldc_caique_replay OBJECT "${NULLDC_CAIQUE_DIR}/model/cases/common/replay.c")
  set_target_properties(nulldc_caique_replay PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

  if(NOT MSVC)
    target_compile_options(nulldc_caique_replay PRIVATE -w)
  endif()

  target_sources(aica_dsp_cases PRIVATE $<TARGET_OBJECTS:nulldc_caique_replay>)
endif()


## The WebAssembly build ##
#
#	aica.dsp.wasm needs emcc and node. emcc: on the PATH, or the emsdk in the environment's EMSDK or in ~/emsdk.
#

add_executable(aica_dsp_wasm_trace
  wasm_trace.cpp
  "${nulldc_dsp_core_path}/wasm/aica_dsp_capi.cpp"
  "${nulldc_dsp_core_path}/wasm/aica_dsp_capi.h"
)
target_link_libraries(aica_dsp_wasm_trace PRIVATE nulldc_aica_dsp_test)
target_compile_definitions(aica_dsp_wasm_trace PRIVATE AICA_DSP_CAPI)

find_program(NULLDC_NODE node)
find_program(NULLDC_EMCC emcc HINTS "$ENV{EMSDK}/upstream/emscripten" "$ENV{HOME}/emsdk/upstream/emscripten")

if(NOT NULLDC_EMCC)
  nulldc_add_skipped_test(aica.dsp.wasm "no emcc: not on the PATH, and no emsdk in EMSDK or ~/emsdk")
elseif(NOT NULLDC_NODE)
  nulldc_add_skipped_test(aica.dsp.wasm "no node on the PATH")
else()
  get_filename_component(nulldc_emcc_dir "${NULLDC_EMCC}" DIRECTORY)
  get_filename_component(nulldc_emsdk_dir "${nulldc_emcc_dir}/../.." ABSOLUTE)

  set(nulldc_dsp_wasm_dir "${CMAKE_CURRENT_BINARY_DIR}/wasm")
  set(nulldc_dsp_wasm_trace "${CMAKE_CURRENT_BINARY_DIR}/wasm_trace.bin")

  # build.sh --check builds twice and fails when the two builds differ or have a path of this machine in them
  add_custom_command(
    OUTPUT "${nulldc_dsp_wasm_dir}/aica_dsp_core.wasm" "${nulldc_dsp_wasm_dir}/aica_dsp_core.mjs"
    COMMAND ${CMAKE_COMMAND} -E env "EMSDK=${nulldc_emsdk_dir}" "PATH=${nulldc_emcc_dir}:$ENV{PATH}"
            sh "${nulldc_dsp_core_path}/wasm/build.sh" --check "${nulldc_dsp_wasm_dir}"
    DEPENDS "${nulldc_dsp_core_path}/aica_dsp_core.cpp" "${nulldc_dsp_core_path}/aica_dsp_core.h"
            "${nulldc_dsp_core_path}/wasm/aica_dsp_capi.cpp" "${nulldc_dsp_core_path}/wasm/aica_dsp_capi.h"
            "${nulldc_dsp_core_path}/wasm/aica_dsp_core.mjs" "${nulldc_dsp_core_path}/wasm/build.sh"
    COMMENT "AICA DSP core for WebAssembly, twice"
    VERBATIM
  )

  add_custom_command(
    OUTPUT "${nulldc_dsp_wasm_trace}"
    COMMAND aica_dsp_wasm_trace "${nulldc_dsp_wasm_trace}"
    DEPENDS aica_dsp_wasm_trace
    COMMENT "Trace of the native AICA DSP core for aica.dsp.wasm"
    VERBATIM
  )

  add_custom_target(aica_dsp_wasm ALL
    DEPENDS "${nulldc_dsp_wasm_dir}/aica_dsp_core.wasm" "${nulldc_dsp_wasm_trace}")

  add_test(NAME aica.dsp.wasm
    COMMAND "${NULLDC_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/wasm_test.mjs"
            "${nulldc_dsp_wasm_dir}/aica_dsp_core.mjs" "${nulldc_dsp_wasm_dir}/aica_dsp_core.wasm" "${nulldc_dsp_wasm_trace}")
  set_tests_properties(aica.dsp.wasm PROPERTIES TIMEOUT 120)
endif()
