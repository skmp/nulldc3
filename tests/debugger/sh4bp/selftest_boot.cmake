# cmake -DSELFTEST=<sh4bp_selftest> -DENGINE=<jit|interpreter|fallback> [-DIDLESKIP=off] -P selftest_boot.cmake
#
# The gates on the booted guest in one configuration, as selftest.sh makes them with sessions of the emulator:
# a session of the tests sh4bpboot (a run for every case) and sessions of their twins sh4bpbase (the same runs
# with nothing of the cases in them). What they print for a run ("note: cmp boot.<gate>.<case> <state>") has to
# be equal. Two sessions of baselines agree, or two of three; a session of cases that differs from them is run
# twice more and fails when it differs again.
#
# The booted guest of this machine is made to have everything those gates stop at: a session that leaves one
# of their cases out fails here.

set(extra "")
if(IDLESKIP STREQUAL "off")
  set(extra "idleskip=off")
endif()

# session(<name> <tests>): <name> gets the states, <name>_out everything
function(session name tests)
  execute_process(COMMAND "${SELFTEST}" ${ENGINE} ${tests} ${extra} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    string(REGEX MATCHALL "SelfTest: [^\n]*FAIL[^\n]*" failed "${out}")
    message(FATAL_ERROR "the ${name} session ended with ${rc}\n${failed}")
  endif()
  if(out MATCHES "note: (the path of the booted guest has no [^\n]*)")
    message(FATAL_ERROR "the ${name} session left a case of the booted guest out: ${CMAKE_MATCH_1}")
  endif()
  string(REGEX MATCHALL "note: cmp boot\\.[^\n]*" lines "${out}")
  set(${name} "${lines}" PARENT_SCOPE)
  set(${name}_out "${out}" PARENT_SCOPE)
endfunction()

# same(<result> <a> <b>): both sessions printed the same states, and printed some
function(same result a b)
  list(LENGTH ${a} count)
  if(count GREATER 0 AND "${${a}}" STREQUAL "${${b}}")
    set(${result} TRUE PARENT_SCOPE)
  else()
    set(${result} FALSE PARENT_SCOPE)
  endif()
endfunction()

session(base sh4bpbase)
session(base2 sh4bpbase)
same(agree base base2)
if(NOT agree)
  message(STATUS "two sessions of baselines differ, a third decides:\n${base}\n${base2}")
  session(base3 sh4bpbase)
  same(agree base2 base3)
  if(agree)
    set(base "${base2}")
  else()
    same(agree base base3)
    if(NOT agree)
      message(FATAL_ERROR "no two of three sessions of baselines agree on the booted guest:\n${base}\n${base2}\n${base3}")
    endif()
  endif()
endif()

session(boot sh4bpboot)
same(agree boot base)
if(NOT agree)
  message(STATUS "the cases differ from the baselines, they are run twice more:\n${boot}\n${base}")
  session(boot2 sh4bpboot)
  session(boot3 sh4bpboot)
  same(agree2 boot2 base)
  same(agree3 boot3 base)
  if(NOT (agree2 AND agree3))
    message(FATAL_ERROR "the booted guest with the cases is not in the state of its baseline:\n${boot2}\n${boot3}\n${base}")
  endif()
  set(boot_out "${boot3_out}")
endif()

list(LENGTH base count)
message(STATUS "${ENGINE} ${extra}: ${count} runs of the booted guest, each in the state of its baseline")

# what the session of cases has to have checked
set(checks
  "an armed address that is not reached: no stop"
  "stop . resume at instructions that end no block"
  "stop . resume at two instructions in a row"
  "stop . resume at delay slots"
  "stop . resume at SLEEPs"
  "pause . resume at slice ends"
  "addresses armed and disarmed in service excursions"
  "an address armed by a call while its block is hot, disarmed in its tenth stop: armed"
  "step-outs pending from every vblank on: [0-9]+ asked for")

if(NOT ENGINE STREQUAL "interpreter")
  list(APPEND checks "stop . resume at blocks that a size limit ended")
  if(NOT IDLESKIP STREQUAL "off")
    list(APPEND checks "stop . resume at instructions in blocks of the idle class")
  endif()
endif()

if(ENGINE STREQUAL "fallback")
  list(APPEND checks "SLEEPs: the fallback loop ran the guest")
else()
  list(APPEND checks "the addresses of every case below, the category muted: no stop")
endif()

foreach(check ${checks})
  if(NOT "${boot_out}" MATCHES ": ok: booted guest, ${check}")
    message(FATAL_ERROR "the session of cases has no check that held for: ${check}")
  endif()
endforeach()

list(LENGTH checks count)
message(STATUS "${ENGINE} ${extra}: ${count} kinds of checks on the booted guest")
