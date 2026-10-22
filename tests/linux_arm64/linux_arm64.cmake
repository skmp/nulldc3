# The cases of tests/linux_arm64 (CMakeLists.txt of this directory has the list), as a script:
#
#   cmake -DCASE=<hashes|pages> -DEMULATOR=<nulldc3> -DGUEST=<dsptest.elf> -DWORK=<scratch directory>
#         [-DELF_DIR=<the directory of 2ndmix.elf>] [-DTEST_TIMEOUT=<the test's ctest TIMEOUT, s>] -P linux_arm64.cmake
#
# Every emulator run carries the keys of an automated run (a portable directory under WORK, config:Headless=yes,
# audio:backend=none, config:Dreamcast.RTC=946684800), runs without DISPLAY and WAYLAND_DISPLAY in its environment
# and ends within its timeout (timeout(1): TERM, then KILL 10 s later; exit status 124 when it was ended so).

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
find_program(timeout_program timeout)
if(NOT timeout_program)
  message(FATAL_ERROR "linux_arm64.cmake: no timeout(1) to run the emulator under")
endif()

# run(<name> [TIMEOUT <s>] [ENV <name=value>...] COMMAND <program> <argument>...): headless, under timeout(1);
# <name>_rc, <name>_out, <name>_err
function(run name)
  cmake_parse_arguments(ARG "" "TIMEOUT" "ENV;COMMAND" ${ARGN})
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 90)
  endif()
  math(EXPR outer "${ARG_TIMEOUT} + 30")
  string(TIMESTAMP t0 "%s")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY ${ARG_ENV}
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

if(CASE STREQUAL "hashes")
  # The state hashes of the SH4 on the recompiler of a Linux arm64 host (config:Dynarec.Enabled=yes; the ARM7 on
  # the interpreter, PL-D46): the arm64 host's own references, as P0's are Linux x86_64's and macos.hashes.jit's
  # the Mac's. LA64's gate read them twice equal on the default and the -DNULLDC_DEBUGGER=OFF builds (the
  # interpreter's there are P0's); they are the Mac's as well. A hash is good for one host (design 5.6): these are
  # no comparison with another host's, and a host whose recompiler computes otherwise gets references of its own
  set(ref_guest c9bb6e60601ab606)
  set(ref_2ndmix 6ab1933550d37163)
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
  # kernel's own 4 KiB, given or not, starts and answers --version
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
    if(NOT "${started-${name}_out}" MATCHES "\nnullDC [^\n]+\n$")
      problem("4096 (${name}): stdout does not end in the version line: \"${started-${name}_out}\"")
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
