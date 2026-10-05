# cmake -DCOST_ON=<sh4bp_cost built with the debugger> -DCOST_OFF=<built without> -P b11_compare.cmake
#
# Gate B11: what the breakpoint mechanism costs a guest that uses none of it. Runs the two programs on the
# recompiler and on the interpreter, with the debugger also with one armed address that is never reached, and
# compares host instruction counts. Every configuration runs three times and counts with its lowest numbers.
#
# The bounds are per unit of what the debugger may cost, so that they do not depend on how long the run is:
#
#   compiled code            the same blocks, guest instructions and bytes of host code in all three
#   per slice, steady state  with the debugger against without: 48 host instructions, either engine
#                            (the stop latch and the attention word in every slice, the report of an interrupt).
#                            A slice of the workload is 56 instructions of the interpreter and about 40 blocks
#                            of the recompiler: one host instruction more for each of those is out of bounds
#   per decoded instruction  with the debugger against without: 24 host instructions (the query of the decoder)
#                            one armed address against none: 128 (the query finds the page marked and searches)
#   steady state             one armed address against none: 0.01 %
#
# A decoded instruction is one of the blocks in the cache when the first window ends; what compiling cost is
# what the first window took more than the second.

cmake_minimum_required(VERSION 3.16)

set(RUNS 3)

set(PER_SLICE 48)
set(PER_OPCODE 24)
set(PER_OPCODE_ARMED 128)

set(skipped "")

# measure(<name> <program> <arguments...>): <name>_<field> for every field of the program's line
function(measure name program)
  set(fields engine armed slices blocks opcodes bytes first steady)

  foreach(run RANGE 1 ${RUNS})
    execute_process(COMMAND "${program}" ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)

    if(out MATCHES "(SKIPPED: [^\n]*)")
      set(skipped "${CMAKE_MATCH_1}" PARENT_SCOPE)
      return()
    endif()

    if(NOT rc EQUAL 0 OR NOT out MATCHES "cost: engine=([a-z]+) armed=([0-9]+) slices=([0-9]+) blocks=([0-9]+) opcodes=([0-9]+) bytes=([0-9]+) first=([0-9]+) steady=([0-9]+)")
      message(FATAL_ERROR "${program} ${ARGN} ended with ${rc}:\n${out}${err}")
    endif()

    set(i 0)
    foreach(field ${fields})
      math(EXPR i "${i} + 1")
      set(value "${CMAKE_MATCH_${i}}")

      if(run EQUAL 1)
        set(${field} "${value}")
      elseif(field STREQUAL "first" OR field STREQUAL "steady")
        if(value LESS ${field})
          set(${field} "${value}")
        endif()
      elseif(NOT value STREQUAL ${field})
        message(FATAL_ERROR "${name}: two runs differ in ${field}: ${${field}} and ${value}")
      endif()
    endforeach()
  endforeach()

  foreach(field ${fields})
    set(${name}_${field} "${${field}}" PARENT_SCOPE)
  endforeach()

  math(EXPR compile "${first} - ${steady}")
  set(${name}_compile "${compile}" PARENT_SCOPE)

  message(STATUS "${name}: ${blocks} blocks, ${opcodes} guest instructions, ${bytes} bytes of host code; first window ${first}, steady ${steady} host instructions for ${slices} slices each")
endfunction()

set(failed 0)

# bound(<what> <a> <b> <units> <per unit> <unit>): |a - b| <= units * per unit
function(bound what a b units per unit)
  math(EXPR diff "${a} - ${b}")
  math(EXPR limit "${units} * ${per}")

  if(diff LESS 0)
    math(EXPR size "0 - ${diff}")
  else()
    set(size ${diff})
  endif()

  if(size GREATER limit)
    message(STATUS "FAILED: ${what}: ${diff} host instructions for ${units} ${unit}, the bound is ${per} for each")
    set(failed 1 PARENT_SCOPE)
  else()
    message(STATUS "ok: ${what}: ${diff} host instructions for ${units} ${unit} (bound: ${per} for each)")
  endif()
endfunction()

measure(jit_off "${COST_OFF}" jit)

if(skipped)
  message("${skipped}")
  return()
endif()

measure(jit_on "${COST_ON}" jit)
measure(jit_never "${COST_ON}" jit never)
measure(interpreter_off "${COST_OFF}" interpreter)
measure(interpreter_on "${COST_ON}" interpreter)

# compiled code
foreach(other jit_on jit_never)
  if(jit_off_blocks EQUAL ${other}_blocks AND jit_off_opcodes EQUAL ${other}_opcodes AND jit_off_bytes EQUAL ${other}_bytes AND NOT jit_off_blocks EQUAL 0)
    message(STATUS "ok: ${other} compiled what jit_off compiled: ${jit_off_blocks} blocks, ${jit_off_bytes} bytes of host code")
  else()
    message(STATUS "FAILED: ${other} compiled ${${other}_blocks} blocks, ${${other}_opcodes} guest instructions, ${${other}_bytes} bytes; jit_off ${jit_off_blocks}, ${jit_off_opcodes}, ${jit_off_bytes}")
    set(failed 1)
  endif()
endforeach()

if(NOT jit_never_armed EQUAL 1 OR NOT jit_on_armed EQUAL 0)
  message(STATUS "FAILED: armed addresses: ${jit_on_armed} and, with one that is never reached, ${jit_never_armed}")
  set(failed 1)
endif()

# with the debugger against without
bound("recompiler, steady state, with the debugger against without" ${jit_on_steady} ${jit_off_steady} ${jit_off_slices} ${PER_SLICE} "slices")
bound("interpreter, steady state, with the debugger against without" ${interpreter_on_steady} ${interpreter_off_steady} ${interpreter_off_slices} ${PER_SLICE} "slices")
bound("recompiler, compiling, with the debugger against without" ${jit_on_compile} ${jit_off_compile} ${jit_off_opcodes} ${PER_OPCODE} "decoded instructions")

# one armed address that is never reached against none
bound("recompiler, compiling, one armed address against none" ${jit_never_compile} ${jit_on_compile} ${jit_on_opcodes} ${PER_OPCODE_ARMED} "decoded instructions")

math(EXPR diff "${jit_never_steady} - ${jit_on_steady}")
math(EXPR limit "${jit_on_steady} / 10000")
if(diff GREATER limit OR diff LESS -${limit})
  message(STATUS "FAILED: recompiler, steady state, one armed address against none: ${diff} of ${jit_on_steady} host instructions, more than 0.01 %")
  set(failed 1)
else()
  message(STATUS "ok: recompiler, steady state, one armed address against none: ${diff} of ${jit_on_steady} host instructions (bound: 0.01 %)")
endif()

if(failed)
  message(FATAL_ERROR "gate B11 failed")
endif()
