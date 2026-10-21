# common.policy.windows: every program of the tests project has the Windows policy for a program that dies
# (win_policy.cpp: no box, no crash report, one line and 128 + the signal's number), read in the file of each built
# program.
#
#   cmake -DPROGRAMS=|<program>|<program>... [-DCONTROLS=|<program>...] -P policy_windows.cmake
#
# A program has the policy when its file holds the mark of win_policy.cpp, the text
#
#   nulldc_win_policy: no box, no crash report, one line and 128 + the signal's number
#
# which the policy's constructor names (a program that links win_policy.cpp, or test_guard.cpp, which pulls it out of
# nulldc_test_common, has it in its image; a program without the policy has not). Every program of PROGRAMS (each
# behind a |; empty entries are skipped) has to have it, and every program of CONTROLS has to lack it: a control is a
# program built without the policy, which the reading has to tell from the others. The test of the tests project
# (tests/common/CMakeLists.txt) gives PROGRAMS every executable target of the project, found by a walk of its
# directories when all of them are made, but nulldc3-winrun and the control; its second test,
# common.policy.windows.catches, gives it a program with the policy and the control as PROGRAMS, and passes when the
# control alone is named as a program without it.
#
# The lines:
#
#   <name>: the policy
#   <name>: lacks the policy
#   <name>: the control, without the policy
#   <n> programs have the policy (<names>)
#   <f> of <n> programs lack the policy: <names>                  and the script fails
#   the control <name> has the policy: the reading tells nothing   and the script fails

cmake_minimum_required(VERSION 3.16)

if(NOT PROGRAMS)
  message(FATAL_ERROR "policy_windows.cmake: needs -DPROGRAMS=")
endif()

set(mark "nulldc_win_policy: no box, no crash report, one line and 128 \\+ the signal's number")

# out: TRUE when the file of the program holds the mark
function(policy_in program out)
  if(NOT EXISTS "${program}")
    message(FATAL_ERROR "policy_windows.cmake: no file ${program}")
  endif()
  file(STRINGS "${program}" found REGEX "${mark}" LIMIT_COUNT 1)
  if(found)
    set(${out} TRUE PARENT_SCOPE)
  else()
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(entries text out)
  string(REPLACE "|" ";" list "${text}")
  set(result "")
  foreach(entry ${list})
    if(entry)
      list(APPEND result "${entry}")
    endif()
  endforeach()
  set(${out} "${result}" PARENT_SCOPE)
endfunction()

entries("${PROGRAMS}" programs)
entries("${CONTROLS}" controls)

set(have "")
set(lack "")
foreach(program ${programs})
  get_filename_component(name "${program}" NAME_WE)
  policy_in("${program}" has)
  if(has)
    message("${name}: the policy")
    list(APPEND have "${name}")
  else()
    message("${name}: lacks the policy")
    list(APPEND lack "${name}")
  endif()
endforeach()

set(problems "")
foreach(control ${controls})
  get_filename_component(name "${control}" NAME_WE)
  policy_in("${control}" has)
  if(has)
    list(APPEND problems "the control ${name} has the policy: the reading tells nothing")
  else()
    message("${name}: the control, without the policy")
  endif()
endforeach()

list(LENGTH programs count)
list(LENGTH have count_have)
list(LENGTH lack count_lack)
list(JOIN have ", " have_text)
message("${count_have} programs have the policy (${have_text})")
if(lack)
  list(JOIN lack ", " lack_text)
  list(APPEND problems "${count_lack} of ${count} programs lack the policy: ${lack_text}")
endif()

if(problems)
  foreach(problem ${problems})
    message("${problem}")
  endforeach()
  message(FATAL_ERROR "the Windows policy of the tests project is not in every program")
endif()
message("PASS: every one of the ${count} programs has the policy for a program that dies")
