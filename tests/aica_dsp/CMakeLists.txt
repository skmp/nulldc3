## AICA DSP core tests ##
#
#	The core is libswirl/hw/aica/dsp_core: one translation unit that needs nothing of the emulator. It is compiled
#	here as the emulator's compilers have to take it, without the permissive mode of the emulator build, and with
#	no conversion that narrows or changes a sign left to the compiler.
#
#	aica_dsp_api          aica.dsp.api: what the calls of the core promise each other, and the known-answer
#	                      programs (dsp_known.cpp). No data needed
#	aica_dsp_diff <set>   aica.dsp.diff.<set>: the core against the DSP of the caique sample model on generated
#	                      programs, step for step
#	aica_dsp_cases <case> aica.dsp.case.<case>: the DSP programs caique ran on a Dreamcast, on the model with the
#	                      core in step and on the core alone, against the console's files
#	aica_dsp_wasm_trace   aica.dsp.capi: the C interface of the core against the core called itself, on random
#	                      programs and on the known-answer programs. And what aica.dsp.wasm replays on
#	                      aica_dsp_core.wasm: wasm_test.mjs, with node
#	aica_dsp_emu <mode>   aica.dsp.emu.<mode>: the core as the emulator's own sound chip drives it (emu_aica.h),
#	                      against a core of the test's own (lockstep) and in the mix, the reset and the save
#	                      state of the DSP device (state). No data needed
#	aica_dsp_cases <case>.emu
#	                      aica.dsp.case.<case>.emu: the cases anchored directly on the console's files on the
#	                      emulator's sound chip, against those files
#	aica_dsp_bench        samples a second, not a test
#
#	The model, the programs and the console's files are caique-rtl's and stay there: the tests are compiled with
#	its sources from the checkout (NULLDC_CAIQUE_DIR) and read its captures from there. What this directory has
#	of caique is the harness around them: cases_main.cpp and dsp_lockstep.cpp name the caique files they follow,
#	with the licence. Without a checkout the tests are skipped. All but aica.dsp.capi and the aica.dsp.wasm tests
#	are gated on baseline.txt, see README.md.
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
  target_compile_options(nulldc_aica_dsp_core PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Werror)
endif()

add_library(nulldc_aica_dsp_test STATIC dsp_suite.cpp dsp_suite.h dsp_known.cpp dsp_known.h dsp_gen.cpp dsp_gen.h)
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


## The core in the emulator's sound chip ##
#
#	The emulator's hw/aica/aica_mmio.cpp, sgc_if.cpp and dsp.cpp, with the definitions and flags of the emulator
#	build (tests/common/libswirl.cmake; the permissive mode this directory leaves out for the core is the
#	emulator's), and emu_aica.cpp, what they reference of the rest of the emulator. The core is the one above
#

nulldc_add_libswirl_library(nulldc_aica_dsp_emu_units
  SOURCES hw/aica/aica_mmio.cpp hw/aica/sgc_if.cpp hw/aica/dsp.cpp
)
target_sources(nulldc_aica_dsp_emu_units PRIVATE emu_aica.cpp emu_aica.h)
target_include_directories(nulldc_aica_dsp_emu_units PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nulldc_aica_dsp_emu_units PUBLIC nulldc_aica_dsp_core)

if(NOT MSVC)
  target_compile_options(nulldc_aica_dsp_emu_units PRIVATE -fpermissive)
endif()

add_executable(aica_dsp_emu emu_main.cpp)
target_link_libraries(aica_dsp_emu PRIVATE nulldc_aica_dsp_emu_units nulldc_aica_dsp_test)
target_compile_definitions(aica_dsp_emu PRIVATE NULLDC_BASELINE="${nulldc_dsp_baseline}")

foreach(mode lockstep state)
  add_test(NAME aica.dsp.emu.${mode} COMMAND aica_dsp_emu ${mode} --strict)
  set_tests_properties(aica.dsp.emu.${mode} PROPERTIES TIMEOUT 300)
endforeach()


## Against the caique model and its console captures ##

set(nulldc_dsp_diff_sets random sparse directed)

# The cases, and the ones that play a channel or are too long for the model: those run on one machine only
set(nulldc_dsp_cases dsp_basic dsp_mem dsp_mem2 dsp_mem3 dsp_wslot dsp_unpack dsp_pack dsp_temp dsp_coll sh4_latch mdec_hold)
set(nulldc_dsp_cases_model_only dsp_coll sh4_latch mdec_hold)
set(nulldc_dsp_cases_core_only dsp_pack)

# The cases anchored directly on the console's files, which also run on the emulator's sound chip
set(nulldc_dsp_cases_emu dsp_basic dsp_mem dsp_mem2 dsp_mem3 dsp_wslot dsp_unpack dsp_pack)

if(NOT NULLDC_HAVE_CAIQUE)
  foreach(set ${nulldc_dsp_diff_sets})
    nulldc_add_missing_data_test(aica.dsp.diff.${set} "${NULLDC_CAIQUE_MISSING}")
  endforeach()

  foreach(case ${nulldc_dsp_cases})
    nulldc_add_missing_data_test(aica.dsp.case.${case} "${NULLDC_CAIQUE_MISSING}")
  endforeach()

  foreach(case ${nulldc_dsp_cases_emu})
    nulldc_add_missing_data_test(aica.dsp.case.${case}.emu "${NULLDC_CAIQUE_MISSING}")
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
  target_link_libraries(aica_dsp_cases PRIVATE nulldc_aica_dsp_caique nulldc_aica_dsp_emu_units)
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

    if(case IN_LIST nulldc_dsp_cases_emu)
      list(APPEND machines emu)
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

    if(case IN_LIST nulldc_dsp_cases_emu)
      add_test(NAME aica.dsp.case.${case}.emu COMMAND aica_dsp_cases ${case}.emu --strict ${nulldc_dsp_require_data})
      set_tests_properties(aica.dsp.case.${case}.emu PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE ${nulldc_dsp_skip_code})
    endif()
  endforeach()

  # mdec_hold ends with the measurement that caique's harness runs in front of every case
  add_library(nulldc_caique_replay OBJECT "${NULLDC_CAIQUE_DIR}/model/cases/common/replay.c")
  set_target_properties(nulldc_caique_replay PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

  if(NOT MSVC)
    target_compile_options(nulldc_caique_replay PRIVATE -w)
  endif()

  target_sources(aica_dsp_cases PRIVATE $<TARGET_OBJECTS:nulldc_caique_replay>)
endif()


## The C interface and the WebAssembly build ##
#
#	aica_dsp_wasm_trace drives the core through its C interface and with its own calls. Run by itself it compares
#	the two: aica.dsp.capi, which needs nothing. The trace it writes has the answers of the core's own calls, for
#	aica.dsp.wasm.
#	aica.dsp.wasm needs emcc and node. emcc: on the PATH, or the emsdk in the environment's EMSDK or in ~/emsdk.
#	aica.dsp.wasm.emsdk and aica.dsp.wasm.noemcc are build.sh finding its emcc, and need the emcc to be an emsdk's.
#

add_executable(aica_dsp_wasm_trace
  wasm_trace.cpp
  "${nulldc_dsp_core_path}/wasm/aica_dsp_capi.cpp"
  "${nulldc_dsp_core_path}/wasm/aica_dsp_capi.h"
)
target_link_libraries(aica_dsp_wasm_trace PRIVATE nulldc_aica_dsp_test)
target_compile_definitions(aica_dsp_wasm_trace PRIVATE AICA_DSP_CAPI)

add_test(NAME aica.dsp.capi COMMAND aica_dsp_wasm_trace)
set_tests_properties(aica.dsp.capi PROPERTIES TIMEOUT 120)

find_program(NULLDC_NODE node)
find_program(NULLDC_EMCC emcc HINTS "$ENV{EMSDK}/upstream/emscripten" "$ENV{HOME}/emsdk/upstream/emscripten")

if(NOT NULLDC_EMCC)
  foreach(test aica.dsp.wasm aica.dsp.wasm.emsdk aica.dsp.wasm.noemcc)
    nulldc_add_skipped_test(${test} "no emcc: not on the PATH, and no emsdk in EMSDK or ~/emsdk")
  endforeach()
elseif(NOT NULLDC_NODE)
  foreach(test aica.dsp.wasm aica.dsp.wasm.emsdk aica.dsp.wasm.noemcc)
    nulldc_add_skipped_test(${test} "no node on the PATH")
  endforeach()
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
    COMMENT "Trace of the AICA DSP core for aica.dsp.wasm"
    VERBATIM
  )

  add_custom_target(aica_dsp_wasm ALL
    DEPENDS "${nulldc_dsp_wasm_dir}/aica_dsp_core.wasm" "${nulldc_dsp_wasm_trace}")

  add_test(NAME aica.dsp.wasm
    COMMAND "${NULLDC_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/wasm_test.mjs"
            "${nulldc_dsp_wasm_dir}/aica_dsp_core.mjs" "${nulldc_dsp_wasm_dir}/aica_dsp_core.wasm" "${nulldc_dsp_wasm_trace}")
  set_tests_properties(aica.dsp.wasm PROPERTIES TIMEOUT 120)

  # build.sh finds emcc by itself: on the PATH, or in the emsdk of EMSDK. The command above hands it the emcc on
  # the PATH. These two run it by sh on the PATH without the directories that have an emcc: with the emsdk in
  # EMSDK it has to build the module, and with no emsdk there it has to say that it has no emcc
  string(REPLACE ":" ";" nulldc_path_dirs "$ENV{PATH}")
  set(nulldc_path_no_emcc "")
  set(nulldc_sh_no_emcc "")

  foreach(dir ${nulldc_path_dirs})
    if(NOT EXISTS "${dir}/emcc")
      list(APPEND nulldc_path_no_emcc "${dir}")

      if(NOT nulldc_sh_no_emcc AND EXISTS "${dir}/sh")
        set(nulldc_sh_no_emcc "${dir}/sh")
      endif()
    endif()
  endforeach()

  list(JOIN nulldc_path_no_emcc ":" nulldc_path_no_emcc)

  if(nulldc_sh_no_emcc AND EXISTS "${nulldc_emsdk_dir}/upstream/emscripten/emcc")
    add_test(NAME aica.dsp.wasm.emsdk
      COMMAND ${CMAKE_COMMAND} -E env "PATH=${nulldc_path_no_emcc}" "EMSDK=${nulldc_emsdk_dir}"
              "${nulldc_sh_no_emcc}" "${nulldc_dsp_core_path}/wasm/build.sh" "${CMAKE_CURRENT_BINARY_DIR}/wasm_emsdk")
    set_tests_properties(aica.dsp.wasm.emsdk PROPERTIES TIMEOUT 300)

    add_test(NAME aica.dsp.wasm.noemcc
      COMMAND ${CMAKE_COMMAND} -E env "PATH=${nulldc_path_no_emcc}" "EMSDK=${CMAKE_CURRENT_BINARY_DIR}/no_emsdk"
              "${nulldc_sh_no_emcc}" "${nulldc_dsp_core_path}/wasm/build.sh" "${CMAKE_CURRENT_BINARY_DIR}/wasm_noemcc")
    set_tests_properties(aica.dsp.wasm.noemcc PROPERTIES
      TIMEOUT 300 PASS_REGULAR_EXPRESSION "no emcc on the PATH and no emsdk in")
  else()
    foreach(test aica.dsp.wasm.emsdk aica.dsp.wasm.noemcc)
      nulldc_add_skipped_test(${test} "the emcc is not in an emsdk, or the PATH has no sh outside the directories with an emcc")
    endforeach()
  endif()
endif()
