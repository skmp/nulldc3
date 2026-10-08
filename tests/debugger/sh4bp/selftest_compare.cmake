# cmake -DSELFTEST=<sh4bp_selftest> -P selftest_compare.cmake
#
# Runs the selftest on the three engines and compares what the sessions print for one another
# ("note: cmp <key> <value>"), as selftest.sh does for sessions of the emulator.

function(session name out_var)
  execute_process(COMMAND "${SELFTEST}" ${name} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    string(REGEX MATCHALL "SelfTest: [^\n]*FAIL[^\n]*" failed "${out}")
    message(FATAL_ERROR "the ${name} session ended with ${rc}\n${failed}")
  endif()
  string(REGEX MATCHALL "note: cmp [^\n]*" lines "${out}")
  set(${out_var} "${lines}" PARENT_SCOPE)
endfunction()

function(compare a b pattern)
  set(va "")
  set(vb "")
  foreach(line ${${a}})
    if(line MATCHES "^note: cmp (${pattern}) ")
      list(APPEND va "${line}")
    endif()
  endforeach()
  foreach(line ${${b}})
    if(line MATCHES "^note: cmp (${pattern}) ")
      list(APPEND vb "${line}")
    endif()
  endforeach()
  list(LENGTH va count)
  if(count EQUAL 0 OR NOT "${va}" STREQUAL "${vb}")
    message(FATAL_ERROR "${a} and ${b} differ in ${pattern}:\n${va}\n${vb}")
  endif()
  message(STATUS "${a} = ${b}: ${count} values (${pattern})")
endfunction()

session(jit jit)
session(interpreter interpreter)
session(fallback fallback)

compare(jit interpreter "b1\\.[^ ]*|b4\\.[^ ]*|b7\\.end")
compare(fallback interpreter "b1\\.[^ ]*|b4\\.[^ ]*|b7\\.end")
compare(fallback jit "b2\\.time\\.blocks|b3\\.time\\.blocks|b12\\.blocks")
