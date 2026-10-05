## The selftests of the debugger, in the emulator ##
#
#	devcast -config Debugger:SelfTest=<name>, headless, on the emulator the system tests build (the default
#	build, <build dir>/system/emulator, or NULLDC_EMULATOR). selftest.py runs a session and says what it has
#	to end with; README.md has the tests.
#
#	A package registers the selftest of its file libswirl/debugger/dbg_selftest_<package>.cpp with one line:
#
#	    nulldc_debugger_selftest(<name>)
#
#	which makes the tests debugger.selftest.<name> (the SH4 on the JIT) and debugger.selftest.<name>.interp
#	(on the interpreter). Options:
#
#	    ENGINES <jit|interp>...     the engines to run on, default both
#	    MODE <mode>                 what the session has to end with (selftest.py), default session
#	    SELFTEST <filter>           the filter of the session when it is not <name>; NONE: no session
#	    ELF <file>                  boot this program of NULLDC_ELF_DIR instead of the guest of this directory;
#	                                without the directory the test is one whose data is missing
#	    CONFIG <section:key=value>...   more -config items for every run
#	    ARGS <argument>...          more arguments for selftest.py
#	    TIMEOUT <seconds>           of ctest, default 600
#

find_package(Python3 COMPONENTS Interpreter)

set(nulldc_selftest_skip "")
if(NOT NULLDC_SYSTEM_TESTS AND NOT NULLDC_EMULATOR)
  set(nulldc_selftest_skip "NULLDC_SYSTEM_TESTS is OFF and no NULLDC_EMULATOR is given: there is no emulator to run")
elseif(NOT ${HOST_OS} EQUAL ${OS_LINUX})
  set(nulldc_selftest_skip "headless mode is there on Linux hosts only")
elseif(NOT Python3_Interpreter_FOUND)
  set(nulldc_selftest_skip "no python3 to run tests/debugger/selftest/selftest.py with")
endif()

if(NULLDC_EMULATOR)
  set(nulldc_selftest_emulator "${NULLDC_EMULATOR}")
else()
  # what tests/system builds, every time the project is built
  set(nulldc_selftest_emulator "${CMAKE_BINARY_DIR}/system/emulator/devcast")
endif()

function(nulldc_debugger_selftest name)
  cmake_parse_arguments(ARG "" "MODE;SELFTEST;ELF;TIMEOUT" "ENGINES;CONFIG;ARGS" ${ARGN})

  if(NOT ARG_ENGINES)
    set(ARG_ENGINES jit interp)
  endif()
  if(NOT ARG_MODE)
    set(ARG_MODE session)
  endif()
  if(NOT ARG_SELFTEST)
    set(ARG_SELFTEST ${name})
  endif()
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 600)
  endif()

  foreach(engine ${ARG_ENGINES})
    if(engine STREQUAL "jit")
      set(test debugger.selftest.${name})
    else()
      set(test debugger.selftest.${name}.${engine})
    endif()

    if(nulldc_selftest_skip)
      nulldc_add_skipped_test(${test} "${nulldc_selftest_skip}")
      continue()
    endif()

    set(command "${Python3_EXECUTABLE}" "${nulldc_tests_path}/debugger/selftest/selftest.py"
                --emulator "${nulldc_selftest_emulator}" --work "${CMAKE_BINARY_DIR}/debugger/selftest/work/${test}"
                --mode ${ARG_MODE} --engine ${engine})

    if(NOT ARG_SELFTEST STREQUAL "NONE")
      list(APPEND command --selftest ${ARG_SELFTEST})
    endif()

    if(ARG_ELF)
      if(NOT NULLDC_ELF_DIR OR NOT EXISTS "${NULLDC_ELF_DIR}/${ARG_ELF}")
        nulldc_add_missing_data_test(${test}
          "no ${ARG_ELF} in NULLDC_ELF_DIR (\"${NULLDC_ELF_DIR}\"), configure with -DNULLDC_ELF_DIR=<directory with the KallistiOS examples>")
        continue()
      endif()
      list(APPEND command --guest "${NULLDC_ELF_DIR}/${ARG_ELF}")
    endif()

    foreach(item ${ARG_CONFIG})
      list(APPEND command --config ${item})
    endforeach()

    add_test(NAME ${test} COMMAND ${command} ${ARG_ARGS})
    # selftest.py has a watchdog per run of the emulator, this is the one behind it
    set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
  endforeach()
endfunction()

# One line for the selftest of each package of the debugger

nulldc_debugger_selftest(emu)


# What the selftest of dbg::emu has besides its session (README.md)

# The guest returns to the BIOS: a headless run ends there
nulldc_debugger_selftest(emuexit.running MODE guest-exit)
nulldc_debugger_selftest(emuexit.pending MODE guest-exit)
nulldc_debugger_selftest(emuexit.parked  MODE guest-exit)

# The debugger at work in a run that an Exit.* key ends: the state hash of a run nobody touched.
# A pause and its resume leave no trace on a backend whose slice counter is the same after Loop() was left and
# entered again: emurun.pauses runs on those. The interpreter is one, it leaves at a slice end with nothing
# over; a backend that starts its counter anew at every entry loses what its last block ran over the slice
nulldc_debugger_selftest(emurun.reads   MODE same-hash ELF 2ndmix.elf)
nulldc_debugger_selftest(emurun.pending MODE same-hash ELF 2ndmix.elf)
nulldc_debugger_selftest(emurun.pauses  MODE same-hash ELF 2ndmix.elf ENGINES interp
                         ARGS --expect "pauses were taken and resumed")

# A parked run stays parked, whatever Exit.* key it has: by a session, and by Debugger:StartPaused
nulldc_debugger_selftest(emuhang.parked MODE parked ENGINES jit ARGS --vblanks 5 --expect "nothing resumes this run")
nulldc_debugger_selftest(startpaused    MODE parked ENGINES jit SELFTEST NONE CONFIG Debugger:StartPaused=yes ARGS --vblanks 5)
