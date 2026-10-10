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

# The device tree's registers, guest memory and the device accessors against the live machine
# (libswirl/debugger/dbg_selftest_regs.cpp): machine tests, on the machine of the booted program, and one
# test that runs the program to where it holds and releases the ARM7
nulldc_debugger_selftest(regs ELF 2ndmix.elf)

# The core: the wire protocol, the tables, the tick builder and the event log, on the booted program and with a
# thread that plays the host (libswirl/debugger/dbg_selftest_core.cpp). The session has no server
nulldc_debugger_selftest(core ELF 2ndmix.elf)

# SH4 breakpoints and steps on the park loop of the emulator (libswirl/debugger/dbg_selftest_sh4bp.cpp,
# tests/debugger/sh4bp/README.md): the session of the gates, and the two that make gates on the booted guest
nulldc_debugger_selftest(sh4bp     ELF 2ndmix.elf CONFIG Debugger:StartPaused=yes)
nulldc_debugger_selftest(sh4bpboot ELF 2ndmix.elf CONFIG Debugger:StartPaused=yes)
nulldc_debugger_selftest(sh4bpbase ELF 2ndmix.elf CONFIG Debugger:StartPaused=yes)

# What sessions of those have to agree on, and the forced fallback and idleskip off: the script of that
# directory, whose sessions are headless too
set(test debugger.selftest.sh4bp.sessions)
if(nulldc_selftest_skip)
  nulldc_add_skipped_test(${test} "${nulldc_selftest_skip}")
elseif(NOT NULLDC_ELF_DIR OR NOT EXISTS "${NULLDC_ELF_DIR}/2ndmix.elf")
  nulldc_add_missing_data_test(${test}
    "no 2ndmix.elf in NULLDC_ELF_DIR (\"${NULLDC_ELF_DIR}\"), configure with -DNULLDC_ELF_DIR=<directory with the KallistiOS examples>")
else()
  add_test(NAME ${test}
           COMMAND bash "${nulldc_tests_path}/debugger/sh4bp/selftest.sh" "${nulldc_selftest_emulator}" "${NULLDC_ELF_DIR}/2ndmix.elf"
                   "${CMAKE_BINARY_DIR}/debugger/selftest/work/${test}")
  set_tests_properties(${test} PROPERTIES TIMEOUT 1800)
endif()


# What the selftest of dbg::emu has besides its session (README.md)

# The guest returns to the BIOS: a headless run ends there
nulldc_debugger_selftest(emuexit.running MODE guest-exit)
nulldc_debugger_selftest(emuexit.pending MODE guest-exit)
nulldc_debugger_selftest(emuexit.parked  MODE guest-exit)

# The debugger at work in a run that an Exit.* key ends: the state hash of a run nobody touched.
# A pause and its resume leave no trace on a backend whose slice counter is the same after Loop() was left and
# entered again (SH4DBG_NATIVE, hw/sh4/sh4_if.h): emurun.pauses runs on those, the interpreter and the x64
# recompiler. A backend that starts its counter anew at every entry loses what its last block ran over the slice
set(nulldc_selftest_exact_engines interp)
if(${HOST_CPU} EQUAL ${CPU_X64})
  set(nulldc_selftest_exact_engines jit interp)
endif()

nulldc_debugger_selftest(emurun.reads   MODE same-hash ELF 2ndmix.elf)
nulldc_debugger_selftest(emurun.pending MODE same-hash ELF 2ndmix.elf)
nulldc_debugger_selftest(emurun.pauses  MODE same-hash ELF 2ndmix.elf ENGINES ${nulldc_selftest_exact_engines}
                         ARGS --expect "pauses were taken and resumed")

# The host stops the running machine and starts it again, 600 times on the way of 2ndmix to its 1200th vblank
# (libswirl/debugger/dbg_selftest_hostcycles.cpp): every emulator thread starts with an empty code cache, the
# cache never fills, the guest does not get slower, and the run ends in the state hash of a run nobody touched.
# The cycles begin at vblank 300, where the program is in its main loop and every thread has the same work;
# they take about 400 vblanks, and the run leaves them twice that.
# The interpreter has no code cache: its run is about the stops and the starts alone
nulldc_debugger_selftest(hostcycles MODE same-hash ELF 2ndmix.elf CONFIG Debugger:SelfTestHostCycleFrom=300
                         ARGS --vblanks 1200
                              --expect "times the host stopped the running machine and started it again"
                              --expect "the guest did not get slower")

# A parked run stays parked, whatever Exit.* key it has: by a session, and by Debugger:StartPaused. The second
# is no session, so it has the server enabled (on a port of the system's choice) and no client: without a
# server Debugger:StartPaused=yes is a configuration error
nulldc_debugger_selftest(emuhang.parked MODE parked ENGINES jit ARGS --vblanks 5 --expect "nothing resumes this run")
nulldc_debugger_selftest(startpaused    MODE parked ENGINES jit SELFTEST NONE CONFIG Debugger:StartPaused=yes Debugger:Enabled=yes Debugger:Port=0 ARGS --vblanks 5)
