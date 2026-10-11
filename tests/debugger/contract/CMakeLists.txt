## The debugger's server, in the emulator, against the contract of the devtools UI ##
#
#	contract.py starts the emulator headless with 2ndmix.elf, as tests/system and the selftests start one (an
#	environment of its own, the clock pinned, a watchdog), on the emulator the system tests build (the default
#	build, <build dir>/system/emulator, or NULLDC_EMULATOR). README.md has the tests.
#
#	    debugger.contract.m1 .m1.interp .m1.fallback    the gate of milestone M1: the contract runner of the devtools
#	                                                    UI (npm run contract -- --url ... --profile emulator --elf
#	                                                    ... --milestone M1), on the JIT, on the interpreter and on
#	                                                    the JIT with Debugger:ForceFallback=yes
#	    debugger.contract.m2arm7 .interp .fallback      the part arm7 of the runner's scenario S16, by itself: the
#	                                                    breakpoints and steps of the ARM7 (--only S00,S16 --parts
#	                                                    S16:arm7), on the same three
#	    debugger.contract.m2arm7.guest .interp          the same against the sounding guest of the tree
#	                                                    (tests/debugger/guest/dsptest), where the run has to have
#	                                                    found the ARM7 held at its start and stepped over and out
#	                                                    of a bl (--require-note)
#	    debugger.contract.m2writes .interp .fallback    the parts arm7, writeMemory, setRegister and reset of S16
#	                                                    (--parts S16:arm7+writeMemory+setRegister+reset), on the
#	                                                    same three
#	    debugger.contract.m2writes.guest .interp        the same four parts against the sounding guest of the tree,
#	                                                    with the two notes
#	    debugger.contract.m2 .m2.interp .m2.fallback    the gate of milestone M2 (--milestone M2): the scenarios
#	                                                    of M1 and S16 whole, with its fifth part, the device tree
#	                                                    of M2, on the same three
#	    debugger.contract.m2.guest .interp              the second run of that gate, against the sounding guest
#	                                                    of the tree (--milestone M2 --only S00,S16), with the two
#	                                                    notes
#	    debugger.contract.grace                         the grace period after the last client left, and the
#	                                                    client that connects behind it
#	    debugger.contract.config                        the configuration errors of the server's keys, and a
#	                                                    server that cannot be started
#	    debugger.contract.slow                          the slow-client rule of the periodic tick
#	    debugger.contract.hash.off .noclient .idlebp    the state hash of a run with the server off, with the
#	      each with .interp                             server on and no client, and with an idle breakpoint
#
#	The runner needs the dreamcast-devtools checkout with its node_modules (NULLDC_DEVTOOLS_DIR) and npm:
#	without them m1*, m2*, m2arm7*, m2writes* and hash.idlebp* are tests whose data is missing. Everything
#	needs 2ndmix.elf of NULLDC_ELF_DIR, but the tests on the guest of the tree.
#

find_package(Python3 COMPONENTS Interpreter)
find_program(NULLDC_NPM NAMES npm DOC "npm, to run the contract runner of the devtools UI with (npm run contract)")

set(nulldc_contract_skip "")
if(NOT NULLDC_SYSTEM_TESTS AND NOT NULLDC_EMULATOR)
  set(nulldc_contract_skip "NULLDC_SYSTEM_TESTS is OFF and no NULLDC_EMULATOR is given: there is no emulator to run")
elseif(NOT ${HOST_OS} EQUAL ${OS_LINUX})
  set(nulldc_contract_skip "headless mode is there on Linux hosts only")
elseif(NOT Python3_Interpreter_FOUND)
  set(nulldc_contract_skip "no python3 to run tests/debugger/contract/contract.py with")
endif()

if(NULLDC_EMULATOR)
  set(nulldc_contract_emulator "${NULLDC_EMULATOR}")
else()
  # what tests/system builds, every time the project is built
  set(nulldc_contract_emulator "${CMAKE_BINARY_DIR}/system/emulator/devcast")
endif()

set(nulldc_contract_no_elf "")
if(NOT NULLDC_ELF_DIR OR NOT EXISTS "${NULLDC_ELF_DIR}/2ndmix.elf")
  set(nulldc_contract_no_elf
      "no 2ndmix.elf in NULLDC_ELF_DIR (\"${NULLDC_ELF_DIR}\"), configure with -DNULLDC_ELF_DIR=<directory with the KallistiOS examples>")
endif()

set(nulldc_contract_no_runner "")
if(NOT NULLDC_DEVTOOLS_DIR OR NOT EXISTS "${NULLDC_DEVTOOLS_DIR}/scripts/contract-test.ts")
  set(nulldc_contract_no_runner
      "no contract runner in NULLDC_DEVTOOLS_DIR (\"${NULLDC_DEVTOOLS_DIR}\"), configure with -DNULLDC_DEVTOOLS_DIR=<dreamcast-devtools checkout>")
elseif(NOT EXISTS "${NULLDC_DEVTOOLS_DIR}/node_modules/.bin/tsx")
  set(nulldc_contract_no_runner "${NULLDC_DEVTOOLS_DIR} has no node_modules (run npm ci there): the contract runner cannot run")
elseif(NOT NULLDC_NPM)
  set(nulldc_contract_no_runner "no npm to run the contract runner with (npm run contract)")
endif()

# nulldc_debugger_contract(<name> MODE <mode of contract.py> [ENGINES <jit|interp>...] [RUNNER] [FALLBACK]
#                          [IDLE_SYMBOL <symbol>] [TIMEOUT <seconds>] [GUEST <program>]
#                          [PARTS <scenario>:<part>[+<part>...]] [ONLY <scenario>[,<scenario>...]]
#                          [REQUIRE_NOTE <text>...])
# makes debugger.contract.<name> (the SH4 on the JIT) and debugger.contract.<name>.interp (on the interpreter).
# RUNNER: the test runs the contract runner of the devtools UI.
# GUEST: the emulator boots this program, not 2ndmix.elf: the test does not ask for NULLDC_ELF_DIR.
# PARTS: for the mode parts, handed to the runner as its --parts. ONLY: for the mode m2, as its --only.
# REQUIRE_NOTE: for the modes parts and m2, as its --require-note.
function(nulldc_debugger_contract name)
  cmake_parse_arguments(ARG "RUNNER;FALLBACK" "MODE;IDLE_SYMBOL;TIMEOUT;GUEST;PARTS;ONLY" "ENGINES;REQUIRE_NOTE" ${ARGN})

  if(NOT ARG_ENGINES)
    set(ARG_ENGINES jit)
  endif()
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 900)
  endif()

  foreach(engine ${ARG_ENGINES})
    if(engine STREQUAL "jit")
      set(test debugger.contract.${name})
    else()
      set(test debugger.contract.${name}.${engine})
    endif()

    if(nulldc_contract_skip)
      nulldc_add_skipped_test(${test} "${nulldc_contract_skip}")
      continue()
    endif()
    if(ARG_GUEST)
      set(guest "${ARG_GUEST}")
    else()
      set(guest "${NULLDC_ELF_DIR}/2ndmix.elf")
    endif()

    if(nulldc_contract_no_elf AND NOT ARG_GUEST)
      nulldc_add_missing_data_test(${test} "${nulldc_contract_no_elf}")
      continue()
    endif()
    if(ARG_RUNNER AND nulldc_contract_no_runner)
      nulldc_add_missing_data_test(${test} "${nulldc_contract_no_runner}")
      continue()
    endif()

    set(command "${Python3_EXECUTABLE}" "${nulldc_tests_path}/debugger/contract/contract.py"
                --emulator "${nulldc_contract_emulator}" --work "${CMAKE_BINARY_DIR}/debugger/contract/work/${test}"
                --guest "${guest}" --mode ${ARG_MODE} --engine ${engine})

    if(ARG_RUNNER)
      list(APPEND command --devtools "${NULLDC_DEVTOOLS_DIR}" --npm "${NULLDC_NPM}")
    endif()
    if(ARG_FALLBACK)
      list(APPEND command --fallback)
    endif()
    if(ARG_IDLE_SYMBOL)
      list(APPEND command --idle-symbol ${ARG_IDLE_SYMBOL})
    endif()
    if(ARG_PARTS)
      list(APPEND command --parts ${ARG_PARTS})
    endif()
    if(ARG_ONLY)
      list(APPEND command --only ${ARG_ONLY})
    endif()
    foreach(note ${ARG_REQUIRE_NOTE})
      list(APPEND command --require-note "${note}")
    endforeach()

    add_test(NAME ${test} COMMAND ${command})
    # contract.py has a watchdog per run of the emulator and for the runner, this is the one behind them
    set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
  endforeach()
endfunction()

# The gate of milestone M1 (design 5.4, 5.5.3): every selected scenario has to pass. On the JIT, on the
# interpreter, and on the JIT treated as a backend without native breakpoints
nulldc_debugger_contract(m1          MODE m1 RUNNER ENGINES jit interp)
nulldc_debugger_contract(m1.fallback MODE m1 RUNNER FALLBACK)

# The part arm7 of the runner's scenario S16 (the breakpoints and steps of the ARM7, design 5.5), by itself: the
# scenario as a whole is partial until the tree of milestone M2 is there, and partial is no pass. On the three
# engines against 2ndmix, whose ARM7 is released more than once while the program starts; and on the JIT and the
# interpreter against the sounding guest of the tree, where the run has to have met the ARM7 held at its start and
# a bl to step over and out of: the runner fails a run that lacks a note
nulldc_debugger_contract(m2arm7          MODE parts PARTS S16:arm7 RUNNER ENGINES jit interp)
nulldc_debugger_contract(m2arm7.fallback MODE parts PARTS S16:arm7 RUNNER FALLBACK)
nulldc_debugger_contract(m2arm7.guest    MODE parts PARTS S16:arm7 RUNNER ENGINES jit interp
                         GUEST "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf"
                         REQUIRE_NOTE "arm7: held at the start" "arm7: bl stepped over and out")

# The four parts of S16 that the emulator has behind the writes and the reset: arm7, writeMemory, setRegister and
# reset (the fifth, tree, comes with the M2 rows of the device tree). On the three engines against 2ndmix, and on
# the JIT and the interpreter against the sounding guest of the tree, where the write side differs: the scratch
# address below a stack at the top of main memory and its two mirrors, the 64 KiB written back over the guest's own
# code, the register banks under the handler of the AICA's interrupt, the reset of a program that reios loads again
nulldc_debugger_contract(m2writes          MODE parts PARTS S16:arm7+writeMemory+setRegister+reset RUNNER ENGINES jit interp)
nulldc_debugger_contract(m2writes.fallback MODE parts PARTS S16:arm7+writeMemory+setRegister+reset RUNNER FALLBACK)
nulldc_debugger_contract(m2writes.guest    MODE parts PARTS S16:arm7+writeMemory+setRegister+reset RUNNER ENGINES jit interp
                         GUEST "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf"
                         REQUIRE_NOTE "arm7: held at the start" "arm7: bl stepped over and out")

# The gate of milestone M2 (M2-ROUND 3.6, 3.10): --milestone M2 selects the scenarios of M1 and S16, and each has
# to pass whole. S16 has its fifth part from the tree of this milestone on: the M2 nodes with their parents, the
# two register panels of the processors, a register on every node whose registers are M2's, and each of the twelve
# register panels the action of one node. And S08 reaches the bound of 256 watches on a tree of 2940 registers.
# On the three engines against 2ndmix; and the second run of the gate on the JIT and the interpreter against the
# sounding guest of the tree, where the arm7 part has to have met the ARM7 held at its start and a bl
nulldc_debugger_contract(m2          MODE m2 RUNNER ENGINES jit interp)
nulldc_debugger_contract(m2.fallback MODE m2 RUNNER FALLBACK)
nulldc_debugger_contract(m2.guest    MODE m2 ONLY S00,S16 RUNNER ENGINES jit interp
                         GUEST "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf"
                         REQUIRE_NOTE "arm7: held at the start" "arm7: bl stepped over and out")

# A client leaves watches and a target behind that is stopped at a breakpoint on a function 2ndmix calls once
# in every frame (the one of hash.idlebp, below): the grace period releases the stop, keeps the tables and
# clears the active mask, so the target runs on through that breakpoint; a client that connects makes
# breakpoints active again and opens the event gates
nulldc_debugger_contract(grace MODE grace IDLE_SYMBOL stars_one_frame)

# Debugger:Required and Debugger:StartPaused without an enabled server: exit code 2 and a line on stderr. And
# an enabled server whose port is taken: exit code 2 with either key, a run without the server with neither
nulldc_debugger_contract(config MODE config TIMEOUT 300)

# A client that has stopped reading is queued no periodic tick behind a megabyte of ticks, every tick that a
# request caused, and a full tick next
nulldc_debugger_contract(slow MODE slow)

# No regression (design 5.7): 2ndmix for 600 vblanks, twice, with the same state hash, part for part.
# hash.off is also the test of the default of an automated run: no server without a Debugger: key.
nulldc_debugger_contract(hash.off      MODE hash-off      ENGINES jit interp)
nulldc_debugger_contract(hash.noclient MODE hash-noclient ENGINES jit interp)

# The breakpoint that is listed and never stops is on a function 2ndmix calls once in every frame
# (README.md has how that was shown: a muted breakpoint reports nothing, and an address that is never
# executed would pass this test as well)
nulldc_debugger_contract(hash.idlebp   MODE hash-idlebp   ENGINES jit interp RUNNER IDLE_SYMBOL stars_one_frame)
