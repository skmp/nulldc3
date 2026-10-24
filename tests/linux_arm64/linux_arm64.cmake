# The cases of tests/linux_arm64 (CMakeLists.txt of this directory has the list), as a script:
#
#   cmake -DCASE=<hashes|pages> -DEMULATOR=<nulldc3> -DGUEST=<dsptest.elf> -DWORK=<scratch directory>
#         [-DELF_DIR=<the directory of 2ndmix.elf>] [-DTEST_TIMEOUT=<the test's ctest TIMEOUT, s>] -P linux_arm64.cmake
#
# Every emulator run carries the keys of an automated run (a portable directory under WORK, config:Headless=yes,
# audio:backend=none, config:Dreamcast.RTC=946684800), runs without DISPLAY and WAYLAND_DISPLAY in its environment
# (env -u), with a core file size limit of 1 byte (prlimit(1), as tests/common/emurun.py starts an emulator on a POSIX
# host: the one limit with which the kernel does not pipe a crash to the system's core handler, apport or
# systemd-coredump, which write outside the build) and ends within its timeout (timeout(1): TERM, then KILL 10 s
# later; exit status 124 when it was ended so). Each case first runs a shell in that envelope, with DISPLAY and
# WAYLAND_DISPLAY set in its own environment, and starts no emulator unless the shell had neither and its core file
# size limit was 1 byte.

cmake_minimum_required(VERSION 3.16)

if(NOT CASE OR NOT EMULATOR OR NOT WORK)
  message(FATAL_ERROR "linux_arm64.cmake: needs -DCASE= -DEMULATOR= -DWORK=")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(problems "")
macro(problem text)
  list(APPEND problems "${text}")
endmacro()

if(NOT EXISTS "${EMULATOR}")
  message(FATAL_ERROR "linux_arm64.cmake: no emulator at ${EMULATOR}")
endif()
find_program(env_program env)
find_program(prlimit_program prlimit)
find_program(timeout_program timeout)
find_program(sh_program sh)
if(NOT env_program OR NOT prlimit_program OR NOT timeout_program OR NOT sh_program)
  message(FATAL_ERROR "linux_arm64.cmake: no env(1), prlimit(1), timeout(1) or sh(1) to run the emulator with")
endif()

# run(<name> [TIMEOUT <s>] [ENV <name=value>...] COMMAND <program> <argument>...): headless, with a core file size
# limit of 1 byte (prlimit --core=1:, the hard limit kept; the kernel compares bytes, so ulimit -c's blocks of 512 or
# 1024 bytes cannot give it), under timeout(1), through env(1) and prlimit(1), which execute it (the exit status is
# the program's, 128 + N for a signal N, 124 for the timeout); <name>_rc, <name>_out, <name>_err
function(run name)
  cmake_parse_arguments(ARG "" "TIMEOUT" "ENV;COMMAND" ${ARGN})
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 90)
  endif()
  math(EXPR outer "${ARG_TIMEOUT} + 30")
  string(TIMESTAMP t0 "%s")
  execute_process(COMMAND "${env_program}" -u DISPLAY -u WAYLAND_DISPLAY ${ARG_ENV}
                          "${prlimit_program}" --core=1: --
                          "${timeout_program}" -k 10 ${ARG_TIMEOUT} ${ARG_COMMAND}
                  WORKING_DIRECTORY "${WORK}" TIMEOUT ${outer} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(TIMESTAMP t1 "%s")
  math(EXPR seconds "${t1} - ${t0}")
  file(WRITE "${WORK}/${name}.log" "exit ${rc} after ${seconds} s\n--- stdout\n${out}\n--- stderr\n${err}\n")
  message("${name}: exit ${rc} after ${seconds} s")
  set(${name}_rc "${rc}" PARENT_SCOPE)
  set(${name}_out "${out}" PARENT_SCOPE)
  set(${name}_err "${err}" PARENT_SCOPE)
endfunction()

# The envelope, before any emulator: a shell run as run() runs the emulator, with DISPLAY and WAYLAND_DISPLAY in this
# script's environment for that run (restored after it), prints its limits and its environment
foreach(var DISPLAY WAYLAND_DISPLAY)
  set(saved_defined_${var} 0)
  if(DEFINED ENV{${var}})
    set(saved_defined_${var} 1)
    set(saved_${var} "$ENV{${var}}")
  endif()
  set(ENV{${var}} "nulldc-envelope-check")
endforeach()
run(envelope TIMEOUT 30 COMMAND "${sh_program}" -c "cat /proc/self/limits && echo --- && env")
foreach(var DISPLAY WAYLAND_DISPLAY)
  if(saved_defined_${var})
    set(ENV{${var}} "${saved_${var}}")
  else()
    unset(ENV{${var}})
  endif()
endforeach()
if(NOT "${envelope_rc}" STREQUAL "0")
  problem("envelope: exit status ${envelope_rc}, not 0: ${envelope_err}")
endif()
if(NOT "${envelope_out}" MATCHES "\nMax core file size +([^ ]+) +([^ ]+) +bytes")
  problem("envelope: no core file size line in /proc/self/limits")
elseif(NOT CMAKE_MATCH_1 STREQUAL "1")
  problem("envelope: the core file size limit is ${CMAKE_MATCH_1}, not 1 byte (hard ${CMAKE_MATCH_2}): a crash would be piped to the system's core handler")
endif()
if("${envelope_out}" MATCHES "\n((WAYLAND_)?DISPLAY=[^\n]*)")
  problem("envelope: the run's environment has ${CMAKE_MATCH_1}")
endif()
if(problems)
  string(REPLACE ";" "\n  " problems "${problems}")
  message(FATAL_ERROR "${CASE}: no emulator started, the envelope is not the one of an automated run:\n  ${problems}\n(${WORK}/envelope.log)")
endif()
message("envelope: core file size limit 1 byte, no DISPLAY and no WAYLAND_DISPLAY")

if(CASE STREQUAL "hashes")
  # The state hashes of the SH4 on the recompiler of a Linux arm64 host (config:Dynarec.Enabled=yes; the ARM7 on
  # the interpreter, PL-D46): the arm64 host's own references, as P0's are Linux x86_64's and macos.hashes.jit's
  # the Mac's. A64 took them (PL-D86): the arm64 recompiler tests its slice counter in front of a block's charge, as
  # the x64 recompiler and the interpreter fallback do, which moved every slice end of its runs (LA64's were
  # c9bb6e60601ab606 and 6ab1933550d37163). A64's runs read them twice equal on the default and the
  # -DNULLDC_DEBUGGER=OFF builds (the interpreter's there are P0's); they are the Mac's as well. A hash is good for
  # one host (design 5.6): these are no comparison with another host's, and a host whose recompiler computes
  # otherwise gets references of its own
  set(ref_guest 6a1e1008459e6ab3)
  set(ref_2ndmix 56bd7274490f91aa)
  set(file_guest "${GUEST}")
  set(file_2ndmix "${ELF_DIR}/2ndmix.elf")
  set(vblanks_guest 90)
  set(vblanks_2ndmix 600)
  # the two runs one after the other, each ended at run_timeout (each took about 2 s), inside the test's TIMEOUT
  set(run_timeout 60)
  math(EXPR runs_at_most "2 * (${run_timeout} + 40)")
  math(EXPR with_the_rest "${runs_at_most} + 30")
  if(NOT TEST_TIMEOUT OR NOT with_the_rest LESS TEST_TIMEOUT)
    problem("the two runs may take ${runs_at_most} s, and 30 s for the rest: not inside the test's TIMEOUT (${TEST_TIMEOUT} s)")
  endif()
  foreach(prog guest 2ndmix)
    if(NOT EXISTS "${file_${prog}}")
      problem("${prog}: no ${file_${prog}}")
      continue()
    endif()
    file(MAKE_DIRECTORY "${WORK}/portable-${prog}")
    run(${prog} TIMEOUT ${run_timeout} COMMAND "${EMULATOR}" --portable "${WORK}/portable-${prog}" --config config:Headless=yes ,
        audio:backend=none , config:Dreamcast.RTC=946684800 , config:Exit.AfterVblanks=${vblanks_${prog}} ,
        config:Exit.StateHash=yes , config:Dynarec.Enabled=yes "${file_${prog}}")
    if(NOT "${${prog}_rc}" STREQUAL "0")
      problem("${prog}: exit status ${${prog}_rc}, not 0")
    endif()
    if(NOT "${${prog}_out}" MATCHES "\nUsing MCPU Recompiler\n")
      problem("${prog}: the SH4 does not run on the recompiler (no \"Using MCPU Recompiler\")")
    endif()
    if("${${prog}_out}${${prog}_err}" MATCHES "fault_handler: [^\n]*")
      problem("${prog}: ${CMAKE_MATCH_0}")
    endif()
    if(NOT "${${prog}_out}" MATCHES "\nnulldc-exit: ([^\n]*)\n")
      problem("${prog}: no exit line")
      continue()
    endif()
    set(line "${CMAKE_MATCH_1}")
    message("${prog}: nulldc-exit: ${line}")
    set(hash "")
    if(line MATCHES " hash=([0-9a-f]+) ")
      set(hash "${CMAKE_MATCH_1}")
    endif()
    if(NOT line MATCHES "^reason=vblanks " OR NOT line MATCHES " vblanks=${vblanks_${prog}} " OR NOT hash)
      problem("${prog}: the exit line is not the one of ${vblanks_${prog}} vblanks with a state hash: ${line}")
    elseif(NOT hash STREQUAL "${ref_${prog}}")
      problem("${prog}: hash=${hash}, the reference of the arm64 recompiler on Linux arm64 is ${ref_${prog}}")
    else()
      message("${prog}: hash=${hash}, the reference")
    endif()
  endforeach()

elseif(CASE STREQUAL "pages")
  # The refusal of linux/common.cpp, the first thing the start does on Linux arm64: NULLDC_TEST_PAGE_SIZE stands
  # for the kernel's page size there. A kernel of 16 or 64 KiB pages ends the start with the line and 1; the
  # kernel's own 4 KiB, given or not, starts and answers --version with its one line (linux-dist/main.cpp answers it
  # before the setup prints its lines, and after the refusal)
  set(keys --portable "${WORK}/portable" --config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800)
  file(MAKE_DIRECTORY "${WORK}/portable")
  foreach(size 16384 65536)
    math(EXPR kib "${size} / 1024")
    run(refused-${size} TIMEOUT 30 ENV NULLDC_TEST_PAGE_SIZE=${size} COMMAND "${EMULATOR}" ${keys} --version)
    set(want "nulldc3: this build needs 4 KiB pages; this kernel uses ${kib} KiB\n")
    if(NOT "${refused-${size}_rc}" STREQUAL "1")
      problem("${size}: exit status ${refused-${size}_rc}, not 1")
    endif()
    if(NOT "${refused-${size}_err}" STREQUAL "${want}")
      problem("${size}: stderr is \"${refused-${size}_err}\", not the one line \"${want}\"")
    endif()
    if(NOT "${refused-${size}_out}" STREQUAL "")
      problem("${size}: stdout is not empty: \"${refused-${size}_out}\"")
    endif()
  endforeach()
  foreach(name given plain)
    if(name STREQUAL "given")
      run(started-${name} TIMEOUT 30 ENV NULLDC_TEST_PAGE_SIZE=4096 COMMAND "${EMULATOR}" ${keys} --version)
    else()
      run(started-${name} TIMEOUT 30 COMMAND "${EMULATOR}" ${keys} --version)
    endif()
    if(NOT "${started-${name}_rc}" STREQUAL "0")
      problem("4096 (${name}): exit status ${started-${name}_rc}, not 0")
    endif()
    if(NOT "${started-${name}_out}" MATCHES "^nullDC [^\n]+\n$")
      problem("4096 (${name}): stdout is not the version line alone: \"${started-${name}_out}\"")
    endif()
    if("${started-${name}_err}" MATCHES "this build needs")
      problem("4096 (${name}): the start was refused: ${started-${name}_err}")
    endif()
  endforeach()

else()
  message(FATAL_ERROR "linux_arm64.cmake: no case ${CASE}")
endif()

if(problems)
  string(REPLACE ";" "\n  " problems "${problems}")
  message(FATAL_ERROR "${CASE}:\n  ${problems}\n(the runs: ${WORK}/*.log)")
endif()
message("${CASE}: passed")
