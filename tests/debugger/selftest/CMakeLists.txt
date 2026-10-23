## The selftests of the debugger, in the emulator ##
#
#	nulldc3 --config Debugger:SelfTest=<name>, headless, on the emulator the system tests build (the default
#	build, <build dir>/system/emulator, or NULLDC_EMULATOR). selftest.py runs a session and says what it has
#	to end with; README.md has the tests.
#
#	On Windows tests/system builds no emulator: NULLDC_EMULATOR is nulldc3.exe of an emulator build, and without it
#	the tests are skipped with that reason. There every program the scripts start runs inside nulldc3-winrun
#	(tests/common/emurun.py), whose path the tests have in NULLDC_WINRUN: the one of NULLDC_WINRUN, or the one
#	tests/windows builds.
#
#	On macOS tests/system builds no emulator either: NULLDC_EMULATOR is the executable of an emulator build's bundle,
#	<build>/nulldc3.app/Contents/MacOS/nulldc3, and without it, or with one that names no file, the tests are tests
#	whose data is missing (failures with NULLDC_TESTS_REQUIRE_DATA), and the configure says so. There the scripts
#	start the emulator, and every other program, through tests/macos/macrun.py (tests/common/emurun.py): the sandbox
#	profile that denies the window server, the Dock and LaunchServices, a session of its own, a timeout, TMPDIR in
#	the run's directory.
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
#	    CONFIG <section:key=value>...   more --config items for every run
#	    ARGS <argument>...          more arguments for selftest.py
#	    TIMEOUT <seconds>           of ctest, default 600
#
#	The sessions nap for fractions of a millisecond, between pauses, while they wait for a state and for every
#	slice inside a SLEEP wait. They nap with dbg::selftest::NapUs (libswirl/debugger/dbg_selftest_nap.h): on
#	Windows std::this_thread::sleep_for lasts a tick of the system timer, 15.6 ms, whatever it is asked for, and
#	emu outlasted its watchdog there and emurun.pauses its run; NapUs is sleep_for on a POSIX host and on Windows a
#	high-resolution waitable timer and a short spin. debugger.selftest.nap holds it
#

find_package(Python3 COMPONENTS Interpreter)

set(nulldc_selftest_skip "")
if(NOT NULLDC_SYSTEM_TESTS AND NOT NULLDC_EMULATOR)
  set(nulldc_selftest_skip "NULLDC_SYSTEM_TESTS is OFF and no NULLDC_EMULATOR is given: there is no emulator to run")
elseif(${HOST_OS} EQUAL ${OS_WINDOWS} AND NOT NULLDC_EMULATOR)
  set(nulldc_selftest_skip
      "no NULLDC_EMULATOR is given, and on Windows tests/system builds no emulator: configure with -DNULLDC_EMULATOR=<nulldc3.exe of an emulator build>")
elseif(NOT ${HOST_OS} EQUAL ${OS_LINUX} AND NOT ${HOST_OS} EQUAL ${OS_WINDOWS} AND NOT ${HOST_OS} EQUAL ${OS_DARWIN})
  set(nulldc_selftest_skip "headless mode is there on Linux, Windows and macOS hosts only")
elseif(NOT Python3_Interpreter_FOUND)
  set(nulldc_selftest_skip "no python3 to run tests/debugger/selftest/selftest.py with")
endif()

# macOS: the emulator is the one of NULLDC_EMULATOR, which has to name a file (nulldc_selftest_missing: the reason)
set(nulldc_selftest_missing "")
if(NOT nulldc_selftest_skip AND ${HOST_OS} EQUAL ${OS_DARWIN})
  if(NOT NULLDC_EMULATOR)
    set(nulldc_selftest_missing
        "no NULLDC_EMULATOR is given, and on macOS tests/system builds no emulator: configure with -DNULLDC_EMULATOR=<build>/nulldc3.app/Contents/MacOS/nulldc3")
  elseif(NOT EXISTS "${NULLDC_EMULATOR}" OR IS_DIRECTORY "${NULLDC_EMULATOR}")
    set(nulldc_selftest_missing
        "NULLDC_EMULATOR names no file (\"${NULLDC_EMULATOR}\"): configure with -DNULLDC_EMULATOR=<build>/nulldc3.app/Contents/MacOS/nulldc3")
  endif()
  if(nulldc_selftest_missing)
    message(WARNING "tests/debugger/selftest: ${nulldc_selftest_missing}")
  endif()
endif()

if(NULLDC_EMULATOR)
  set(nulldc_selftest_emulator "${NULLDC_EMULATOR}")
else()
  # what tests/system builds, every time the project is built
  set(nulldc_selftest_emulator "${CMAKE_BINARY_DIR}/system/emulator/nulldc3${CMAKE_EXECUTABLE_SUFFIX}")
endif()

# The environment of the tests: Python writes no bytecode into the source tree; on Windows the scripts start every
# program inside nulldc3-winrun (tests/common/emurun.py)
set(nulldc_selftest_env PYTHONDONTWRITEBYTECODE=1)
if(${HOST_OS} EQUAL ${OS_WINDOWS})
  if(NULLDC_WINRUN)
    list(APPEND nulldc_selftest_env "NULLDC_WINRUN=${NULLDC_WINRUN}")
  else()
    list(APPEND nulldc_selftest_env "NULLDC_WINRUN=$<TARGET_FILE:nulldc3_winrun>")
  endif()
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
    elseif(nulldc_selftest_missing)
      nulldc_add_missing_data_test(${test} "${nulldc_selftest_missing}")
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
    set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT} ENVIRONMENT "${nulldc_selftest_env}")
  endforeach()
endfunction()

# One line for the selftest of each package of the debugger

nulldc_debugger_selftest(emu)

# The naps of the sessions (libswirl/debugger/dbg_selftest_nap.cpp): naps of 100 us to 50 ms last what they were
# asked for, never less and at most a little more, and wait without a spin for the nap; on a POSIX host as long as
# std::this_thread::sleep_for of the same request, to within 50 us; on Windows also without the high-resolution
# timer. A machine test, on one engine
nulldc_debugger_selftest(nap ENGINES jit)

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

# The sound side: the sound burst that the debugger suspends and runs on, the breakpoints and steps of the ARM7
# and its register writer (libswirl/debugger/dbg_selftest_arm7.cpp), on the sounding guest of the tree
# (tests/debugger/guest/dsptest), which needs no NULLDC_ELF_DIR. On the JIT, on the interpreter and on the JIT
# with the interpreter fallback of the SH4 forced. Every time step of the AICA makes its sample, also one that
# runs in the park context
nulldc_debugger_selftest(arm7 ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")
nulldc_debugger_selftest(arm7.fallback SELFTEST arm7 ENGINES jit CONFIG Debugger:ForceFallback=yes
                         ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")

# The run control of the DSP: the DSP phase of the sound burst and the end of a sample as places the burst is
# suspended at, the breakpoints and steps of the DSP, and the rows of dc.aica.dsp (libswirl/debugger/
# dbg_selftest_dsp.cpp), on the sounding guest of the tree, whose DSP program the guest loads at its start. On the
# JIT, on the interpreter and on the JIT with the interpreter fallback of the SH4 forced
nulldc_debugger_selftest(dsp ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")
nulldc_debugger_selftest(dsp.fallback SELFTEST dsp ENGINES jit CONFIG Debugger:ForceFallback=yes
                         ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")

# The SGC frame capture, the outputs the sound generator keeps for it, the binary frames of the core and its
# deferred response of state.recordSgcFrames (libswirl/debugger/dbg_selftest_sgc.cpp: sgc.* and mem.binary.*), on
# the sounding guest of the tree, whose two channels play. On the JIT and the interpreter
nulldc_debugger_selftest(sgc SELFTEST sgc,mem.binary ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")

# The debugger's writes: guest memory and the registers of the two processors, with what each write owes the
# machine (libswirl/debugger/dbg_selftest_writes.cpp), on the same guest. On the JIT, on the interpreter and on
# the JIT with the interpreter fallback of the SH4 forced.
# writes.smc is the steps of the test writes.mem.cached where a main-memory write without the discard of the
# compiled blocks of its pages would show: with Dynarec.SmcCheckLevel=-1 a block of a page that holds data does
# not check its code when it is entered. In every other configuration that check, or the fault handler, makes
# such a write good, and the steps pass with or without the discard. So the run is a test of its own,
# smcwrites.cached, which is not selected by the session "writes" and which fails when the level of its run is
# not -1, or when the SH4 does not run on the recompiler of an emulator that has one: a key that did not reach
# the run cannot make this target the default run under another name. (On an emulator built without a recompiler
# this target fails as every target of the JIT does there, for its run is not made on the recompiler (selftest.py);
# the test's own branch for such a build is reached by a session started by hand, selftest/README.md.)
nulldc_debugger_selftest(writes ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")
nulldc_debugger_selftest(writes.fallback SELFTEST writes ENGINES jit CONFIG Debugger:ForceFallback=yes
                         ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")
nulldc_debugger_selftest(writes.smc SELFTEST smcwrites ENGINES jit CONFIG config:Dynarec.SmcCheckLevel=-1
                         ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")

# What sessions of those have to agree on, and the forced fallback and idleskip off: sessions.py of that
# directory, whose sessions are headless too
set(test debugger.selftest.sh4bp.sessions)
if(nulldc_selftest_skip)
  nulldc_add_skipped_test(${test} "${nulldc_selftest_skip}")
elseif(nulldc_selftest_missing)
  nulldc_add_missing_data_test(${test} "${nulldc_selftest_missing}")
elseif(NOT NULLDC_ELF_DIR OR NOT EXISTS "${NULLDC_ELF_DIR}/2ndmix.elf")
  nulldc_add_missing_data_test(${test}
    "no 2ndmix.elf in NULLDC_ELF_DIR (\"${NULLDC_ELF_DIR}\"), configure with -DNULLDC_ELF_DIR=<directory with the KallistiOS examples>")
else()
  add_test(NAME ${test}
           COMMAND "${Python3_EXECUTABLE}" "${nulldc_tests_path}/debugger/sh4bp/sessions.py" "${nulldc_selftest_emulator}"
                   "${NULLDC_ELF_DIR}/2ndmix.elf" "${CMAKE_BINARY_DIR}/debugger/selftest/work/${test}")
  set_tests_properties(${test} PROPERTIES TIMEOUT 1800 ENVIRONMENT "${nulldc_selftest_env}")
endif()

# What the scripts make of what they read, on made-up input, no emulator is run: there wherever python3 is.
# log.rule: selftest.py's checks on the output of a session whose lines end in \n and in \r\n. sessions.rule:
# the comparisons of sessions.py on made-up sessions, every key it holds and the repeats of its rule
if(Python3_Interpreter_FOUND)
  add_test(NAME debugger.selftest.log.rule
           COMMAND "${Python3_EXECUTABLE}" "${nulldc_tests_path}/debugger/selftest/selftest.py" --mode log-rule
                   --work "${CMAKE_BINARY_DIR}/debugger/selftest/work/debugger.selftest.log.rule")
  add_test(NAME debugger.selftest.sh4bp.sessions.rule
           COMMAND "${Python3_EXECUTABLE}" "${nulldc_tests_path}/debugger/sh4bp/sessions.py" --rule)
  set_tests_properties(debugger.selftest.log.rule debugger.selftest.sh4bp.sessions.rule PROPERTIES
                       TIMEOUT 60 ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
else()
  nulldc_add_skipped_test(debugger.selftest.log.rule "no python3 to run tests/debugger/selftest/selftest.py with")
  nulldc_add_skipped_test(debugger.selftest.sh4bp.sessions.rule "no python3 to run tests/debugger/sh4bp/sessions.py with")
endif()


# What the selftest of dbg::emu has besides its session (README.md)

# The guest returns to the BIOS: a headless run ends there
nulldc_debugger_selftest(emuexit.running MODE guest-exit)
nulldc_debugger_selftest(emuexit.pending MODE guest-exit)
nulldc_debugger_selftest(emuexit.parked  MODE guest-exit)

# The debugger at work in a run that an Exit.* key ends: the state hash of a run nobody touched.
# A pause and its resume leave no trace on a backend whose slice counter is the same after Loop() was left and
# entered again, and whose loop is left with nothing owed (hw/sh4/sh4_if.h): the interpreter, and the recompilers
# of x86-64 and arm64 hosts, which keep their counters and test them in front of a block's charge. emurun.pauses runs
# on both engines of every host
set(nulldc_selftest_exact_engines jit interp)

nulldc_debugger_selftest(emurun.reads   MODE same-hash ELF 2ndmix.elf)
nulldc_debugger_selftest(emurun.pending MODE same-hash ELF 2ndmix.elf)
nulldc_debugger_selftest(emurun.pauses  MODE same-hash ELF 2ndmix.elf ENGINES ${nulldc_selftest_exact_engines}
                         ARGS --expect "pauses were taken and resumed")

# The debugger's writes in a run of the guest of the tree that an Exit.* key ends: 240 stops latched at slice ends,
# each with a write of main memory, of the page of the guest's code, of a general register, of SR, of FPSCR (which
# empties the code cache) and of R0 to R12 of the ARM7, and the restore of each; half of them as the requests of a
# client. The state hash of a run nobody touched. The test never returns: it is no test of the session "writes",
# whose filter would select a test named under its prefix and never print its summary
nulldc_debugger_selftest(writesrun.neutral MODE same-hash
                         ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf"
                              --expect "240 stops latched at a slice end, each with its writes and their restores"
                              --expect "every write read back, and every restore"
                              --expect "240 writes of main memory and 240 writes into the page of the guest's code"
                              --expect "240 writes of R8, 240 of SR and 240 of FPSCR that empty the code cache"
                              --expect "3120 writes of R0 to R12 of the ARM7"
                              --expect "4320 of the writes and restores were requests of a client")

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
