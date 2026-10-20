## The selftests of the debugger, in the emulator ##
#
#	nulldc3 --config Debugger:SelfTest=<name>, headless, on the emulator the system tests build (the default
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
#	    CONFIG <section:key=value>...   more --config items for every run
#	    ARGS <argument>...          more arguments for selftest.py
#	    TIMEOUT <seconds>           of ctest, default 600
#

find_package(Python3 COMPONENTS Interpreter)

set(nulldc_selftest_skip "")
if(NOT NULLDC_SYSTEM_TESTS AND NOT NULLDC_EMULATOR)
  set(nulldc_selftest_skip "NULLDC_SYSTEM_TESTS is OFF and no NULLDC_EMULATOR is given: there is no emulator to run")
elseif(${HOST_OS} EQUAL ${OS_WINDOWS})
  set(nulldc_selftest_skip "W64b")
elseif(NOT ${HOST_OS} EQUAL ${OS_LINUX})
  set(nulldc_selftest_skip "headless mode is there on Linux hosts only")
elseif(NOT Python3_Interpreter_FOUND)
  set(nulldc_selftest_skip "no python3 to run tests/debugger/selftest/selftest.py with")
endif()

if(NULLDC_EMULATOR)
  set(nulldc_selftest_emulator "${NULLDC_EMULATOR}")
else()
  # what tests/system builds, every time the project is built
  set(nulldc_selftest_emulator "${CMAKE_BINARY_DIR}/system/emulator/nulldc3")
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
    # selftest.py has a watchdog per run of the emulator, this is the one behind it. Python writes no bytecode
    # into the source tree
    set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT} ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
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
