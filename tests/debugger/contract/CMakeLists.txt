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
#	    debugger.contract.grace                         the grace period after the last client left, and the
#	                                                    client that connects behind it
#	    debugger.contract.config                        the configuration errors of the server's keys, and a
#	                                                    server that cannot be started
#	    debugger.contract.slow                          the slow-client rule of the periodic tick
#	    debugger.contract.hash.off .noclient .idlebp    the state hash of a run with the server off, with the
#	      each with .interp                             server on and no client, and with an idle breakpoint
#
#	The runner needs the dreamcast-devtools checkout with its node_modules (NULLDC_DEVTOOLS_DIR) and npm:
#	without them m1* and hash.idlebp* are tests whose data is missing. Everything needs 2ndmix.elf of
#	NULLDC_ELF_DIR.
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
#                          [IDLE_SYMBOL <symbol>] [TIMEOUT <seconds>])
# makes debugger.contract.<name> (the SH4 on the JIT) and debugger.contract.<name>.interp (on the interpreter).
# RUNNER: the test runs the contract runner of the devtools UI.
function(nulldc_debugger_contract name)
  cmake_parse_arguments(ARG "RUNNER;FALLBACK" "MODE;IDLE_SYMBOL;TIMEOUT" "ENGINES" ${ARGN})

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
    if(nulldc_contract_no_elf)
      nulldc_add_missing_data_test(${test} "${nulldc_contract_no_elf}")
      continue()
    endif()
    if(ARG_RUNNER AND nulldc_contract_no_runner)
      nulldc_add_missing_data_test(${test} "${nulldc_contract_no_runner}")
      continue()
    endif()

    set(command "${Python3_EXECUTABLE}" "${nulldc_tests_path}/debugger/contract/contract.py"
                --emulator "${nulldc_contract_emulator}" --work "${CMAKE_BINARY_DIR}/debugger/contract/work/${test}"
                --guest "${NULLDC_ELF_DIR}/2ndmix.elf" --mode ${ARG_MODE} --engine ${engine})

    if(ARG_RUNNER)
      list(APPEND command --devtools "${NULLDC_DEVTOOLS_DIR}" --npm "${NULLDC_NPM}")
    endif()
    if(ARG_FALLBACK)
      list(APPEND command --fallback)
    endif()
    if(ARG_IDLE_SYMBOL)
      list(APPEND command --idle-symbol ${ARG_IDLE_SYMBOL})
    endif()

    add_test(NAME ${test} COMMAND ${command})
    # contract.py has a watchdog per run of the emulator and for the runner, this is the one behind them
    set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
  endforeach()
endfunction()

# The gate of milestone M1 (design 5.4, 5.5.3): every selected scenario has to pass. On the JIT, on the
# interpreter, and on the JIT treated as a backend without native breakpoints
nulldc_debugger_contract(m1          MODE m1 RUNNER ENGINES jit interp)
nulldc_debugger_contract(m1.fallback MODE m1 RUNNER FALLBACK)

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
