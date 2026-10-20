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
  # the policy for a program that dies (win_policy.cpp), which test_guard.cpp takes into every suite
  target_sources(nulldc_test_common PRIVATE win_policy.cpp)
endif()

# The harness tested on itself: what it has to catch, it catches
add_executable(common_selftest selftest.cpp)
target_link_libraries(common_selftest PRIVATE nulldc_sh4_interp nulldc_test_common)
add_test(NAME common.selftest COMMAND common_selftest)
set_tests_properties(common.selftest PROPERTIES TIMEOUT 60)

# How the harnesses of the tests start the emulator and node, on every host (emurun.py): its test, with python and
# nothing else. On Windows it starts a program through nulldc3-winrun, as the harnesses do: NULLDC_WINRUN, or the one
# tests/windows builds
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  set(nulldc_emurun_env PYTHONDONTWRITEBYTECODE=1)
  if(${HOST_OS} EQUAL ${OS_WINDOWS})
    if(NULLDC_WINRUN)
      list(APPEND nulldc_emurun_env "NULLDC_WINRUN=${NULLDC_WINRUN}")
    else()
      list(APPEND nulldc_emurun_env "NULLDC_WINRUN=$<TARGET_FILE:nulldc3_winrun>")
    endif()
  endif()
  add_test(NAME harness.emurun
           COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/emurun_test.py" --work "${CMAKE_BINARY_DIR}/common/work/harness.emurun")
  set_tests_properties(harness.emurun PROPERTIES TIMEOUT 300 ENVIRONMENT "${nulldc_emurun_env}")
else()
  nulldc_add_skipped_test(harness.emurun "no python3 to run tests/common/emurun_test.py with")
endif()

# Windows: the policy for a program that dies (win_policy.cpp: no box, no crash report, one line and 128 + the
# signal's number), which every suite that links test_guard.cpp has, and the programs the tests run besides: each made
# to fault through nulldc3-winrun, and the crash-report places of the profile listed by name before and after
# (guard_windows.cmake). common_selftest every way the policy takes; the others, which link win_policy.cpp each in
# their own directory, two ways: disasm_test (debugger.disasm.*), sh4bp_test (debugger.sh4bp.*) and, where the
# caique checkout is, aica_dsp_guest_oracle (the mode oracle of soundlines.py)
if(${HOST_OS} EQUAL ${OS_WINDOWS})
  if(NULLDC_WINRUN)
    set(nulldc_guard_winrun "${NULLDC_WINRUN}")
  else()
    set(nulldc_guard_winrun "$<TARGET_FILE:nulldc3_winrun>")
  endif()
  # each where this build has it (the targets of directories behind this one, so as generator expressions)
  set(nulldc_guard_tools "")
  foreach(tool disasm_test sh4bp_test aica_dsp_guest_oracle)
    string(APPEND nulldc_guard_tools "$<$<TARGET_EXISTS:${tool}>:|$<TARGET_FILE:${tool}>>")
  endforeach()
  add_test(NAME common.guard.windows
           COMMAND "${CMAKE_COMMAND}" "-DWINRUN=${nulldc_guard_winrun}" "-DPROGRAM=$<TARGET_FILE:common_selftest>"
                   "-DTOOLS=${nulldc_guard_tools}"
                   "-DWORK=${CMAKE_BINARY_DIR}/common/work/common.guard.windows" -P "${CMAKE_CURRENT_SOURCE_DIR}/guard_windows.cmake")
  set_tests_properties(common.guard.windows PROPERTIES TIMEOUT 600)
endif()
