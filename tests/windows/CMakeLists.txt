## Windows: the run envelope and the emulator's headless mode on Windows ##
#
#	Only on Windows. nulldc3-winrun (tests/winrun/winrun.cpp) is built here as nulldc3-winrun.exe; every test of
#	this directory that starts a program starts it through nulldc3-winrun, on a desktop of its own, in a job, with
#	the error mode that keeps the system's boxes away (tests/winrun/README.md). The emulator is NULLDC_EMULATOR
#	(nulldc3.exe of an emulator build); the guest is the sounding guest of the tree
#	(tests/debugger/guest/dsptest/dsptest.elf). The cases are in windows.cmake; README.md has what each checks.
#
#	windows.winrun.probe        nulldc3-winrun --timeout 10 -- nulldc3-winrun --probe: the probe says the private
#	                            desktop's name, its window is in nulldc3-winrun's list, the verdict is 125
#	windows.winrun.detach       nulldc3-winrun --detach, read through a pipe, returns at once; the copy runs the
#	                            program (15 s) to its end and writes 0 into its rc file
#	windows.headless.nowindow   the guest headless with NULLDC_TEST_UI_REPORT=1: windows=0, no COM apartment
#	                            window, the policy read back (wer, invparam, purecall, sigabrt, abort), exit 0, the
#	                            exit line, no window seen
#	windows.fatal.<kind>        NULLDC_TEST_FATAL=<kind> for segv ill trap abort div stack invparam purecall: the
#	                            fault_handler line (for trap die's line and no fault_handler line), 128 + the signal,
#	                            no window, no timeout, nothing new in the crash-report places
#	windows.helpbox             the help/error box: a refused command line to a pipe and to NUL (exit 2, no window);
#	                            with a console of its own and config:Headless=yes (exit 2, no window, within 10 s);
#	                            with a console of its own and not headless (the box on the private desktop: 125)
#	windows.abi.xmm             Mainloop and RunBlock give xmm6-xmm15 back, and a stack walk from UpdateSystem_INTC
#	                            through each comes to its caller with the emulator's unwind data (abi_xmm.cpp)
#	windows.binary.stdout       no \r in the emulator's stdout or stderr
#	windows.dsound.failed       DirectSound that fails (NULLDC_TEST_DSOUND=fail) with the speed limit on: no sound,
#	                            and the guest headless still reaches its 90 vblanks and its exit line
#	windows.exitcode            a run that cannot go on (headless, nothing to run) exits with 1
#
#	NULLDC_WINRUN               an nulldc3-winrun.exe to use instead of the one built here; also what version.binary
#	                            (tests/version) starts the emulator through on Windows
#

if(NOT WIN32)
  return()
endif()

add_executable(nulldc3_winrun "${nulldc_tests_path}/winrun/winrun.cpp")
set_target_properties(nulldc3_winrun PROPERTIES OUTPUT_NAME nulldc3-winrun)
target_compile_definitions(nulldc3_winrun PRIVATE UNICODE _UNICODE)
target_link_libraries(nulldc3_winrun PRIVATE user32 advapi32)

set(NULLDC_WINRUN "" CACHE FILEPATH
    "nulldc3-winrun.exe that the tests start programs through on Windows, empty: the one the tests project builds")
if(NULLDC_WINRUN)
  set(nulldc_winrun "${NULLDC_WINRUN}")
else()
  set(nulldc_winrun "$<TARGET_FILE:nulldc3_winrun>")
endif()

set(nulldc_windows_guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")
set(nulldc_windows_script "${CMAKE_CURRENT_SOURCE_DIR}/windows.cmake")

# windows_case(<test> <case> [EMULATOR] [TIMEOUT <s>])
function(windows_case test case)
  cmake_parse_arguments(ARG "EMULATOR" "TIMEOUT" "" ${ARGN})
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 120)
  endif()
  if(ARG_EMULATOR AND NOT NULLDC_EMULATOR)
    nulldc_add_missing_data_test(${test} "no emulator to run: configure with -DNULLDC_EMULATOR=<nulldc3.exe>")
    return()
  endif()
  nulldc_work_dir(work_dir ${test})
  add_test(NAME ${test}
           COMMAND "${CMAKE_COMMAND}" "-DCASE=${case}" "-DWINRUN=${nulldc_winrun}" "-DEMULATOR=${NULLDC_EMULATOR}"
                   "-DGUEST=${nulldc_windows_guest}" "-DWORK=${work_dir}"
                   -P "${nulldc_windows_script}")
  set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
endfunction()

windows_case(windows.winrun.probe probe TIMEOUT 60)
windows_case(windows.winrun.detach detach TIMEOUT 150)
windows_case(windows.headless.nowindow headless EMULATOR)
foreach(kind segv ill trap abort div stack invparam purecall)
  windows_case(windows.fatal.${kind} fatal.${kind} EMULATOR)
endforeach()
windows_case(windows.helpbox helpbox EMULATOR)
windows_case(windows.binary.stdout binary_stdout EMULATOR)
windows_case(windows.dsound.failed dsound_failed EMULATOR)
windows_case(windows.exitcode exitcode EMULATOR)

# The canary thunk around the x64 backend's main loop and RunBlock, on a machine of its own (abi_xmm.cpp)
if((${HOST_CPU} EQUAL ${CPU_X64}) AND (${FEAT_SHREC} EQUAL ${DYNAREC_JIT}))
  nulldc_add_libswirl_library(nulldc_windows_dynarec SOURCES
    ${NULLDC_SH4_INTERP_SOURCES}
    hw/sh4/dyna/decoder.cpp
    hw/sh4/dyna/shil.cpp
    hw/sh4/dyna/driver.cpp
    hw/sh4/dyna/blockmanager.cpp
    jit/backend/x64/rec_x64.cpp
    deps/crypto/sha1.cpp
    windows/win_jit_unwind.cpp
  )
  # abi_xmm.cpp has the stubs of tests/common/sh4_stubs_rec.cpp itself, with a code region of its own that has the
  # emulator's unwind data (windows/win_jit_unwind.cpp), so that file of nulldc_test_common is not linked
  add_executable(windows_abi_xmm abi_xmm.cpp)
  target_link_libraries(windows_abi_xmm PRIVATE nulldc_windows_dynarec nulldc_test_common)
  add_test(NAME windows.abi.xmm COMMAND "${nulldc_winrun}" --timeout 60 -- $<TARGET_FILE:windows_abi_xmm>)
  set_tests_properties(windows.abi.xmm PROPERTIES TIMEOUT 120)
else()
  nulldc_add_skipped_test(windows.abi.xmm "the x64 backend is not in this build: no main loop to check")
endif()

# what version.binary starts the emulator through (tests/version)
set(nulldc_winrun "${nulldc_winrun}" PARENT_SCOPE)
