# common.guard.windows: the Windows policy of the tests project for a program that dies (win_policy.cpp), which every
# suite that links test_guard.cpp has from before main, and the programs the tests run besides.
#
#   cmake -DWINRUN=<nulldc3-winrun.exe> -DPROGRAM=<a program that links test_guard.cpp> [-DTOOLS=|<program>|...]
#         [-DKINDS=<kind>;...] -DWORK=<scratch> -P guard_windows.cmake
#
# PROGRAM and every program of TOOLS (each behind a |; empty entries are skipped) are run through nulldc3-winrun with
# NULLDC_TEST_GUARD_FAULT=<kind>, which the policy raises right behind itself, before main (the table of
# win_policy.cpp), for every kind (or those of KINDS). Each kind has to end the program with the line of the policy
# on stderr, which names the handler that took it,
#
#   <program>: unhandled <SIG> (<what>) at address <a>, pc <p>, <handler>: terminating
#
# and 128 + the signal's number, with no window. Each kind is the business of one handler:
#
#   segv      139  SIGSEGV (access violation)          the vectored handler
#   abort     134  SIGABRT (abort)                     the SIGABRT handler
#   div       136  SIGFPE (integer divide by zero)     the vectored handler
#   throw     134  SIGABRT (C++ exception)             the filter
#   trap      133  SIGTRAP (breakpoint)                the filter
#   invparam  134  SIGABRT (invalid parameter)         the invalid-parameter handler
#   purecall  134  SIGABRT (pure virtual call)         the pure-call handler
#   stack     139  SIGSEGV (access violation)          the vectored handler (on a stack that cannot be searched)
#
# Without its handler a kind fails here: segv and div go to the filter, throw to the C runtime's filter
# (std::terminate, abort(): the SIGABRT handler), purecall to abort(), abort ends with exit code 3, and trap, invparam
# and stack reach Windows Error Reporting, which writes a crash report into the profile (KINDS keeps a run of a
# program without a part of the policy to the kinds that do not).
#
# And none may leave a crash report: the crash-report places of the profile (%LOCALAPPDATA%\CrashDumps, the
# ReportQueue and ReportArchive of Windows Error Reporting under %LOCALAPPDATA% and %ProgramData%) are listed by name
# before and after, and an entry that is new behind the runs and names one of the programs (by the first 12
# characters of its name, which Windows Error Reporting keeps in the names of its folders) fails the test; other
# programs of the computer may report crashes meanwhile: an entry of another program is printed, and is no failure.

cmake_minimum_required(VERSION 3.16)

if(NOT WINRUN OR NOT PROGRAM OR NOT WORK)
  message(FATAL_ERROR "guard_windows.cmake: needs -DWINRUN= -DPROGRAM= -DWORK=")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(programs "${PROGRAM}")
if(TOOLS)
  string(REPLACE "|" ";" tools "${TOOLS}")
  foreach(tool ${tools})
    if(tool)
      list(APPEND programs "${tool}")
    endif()
  endforeach()
endif()

# the names in the crash-report places, one list
function(crash_places out)
  set(names "")
  foreach(dir "$ENV{LOCALAPPDATA}/CrashDumps" "$ENV{LOCALAPPDATA}/Microsoft/Windows/WER/ReportQueue"
              "$ENV{LOCALAPPDATA}/Microsoft/Windows/WER/ReportArchive" "$ENV{ProgramData}/Microsoft/Windows/WER/ReportQueue"
              "$ENV{ProgramData}/Microsoft/Windows/WER/ReportArchive")
    file(TO_CMAKE_PATH "${dir}" dir)
    file(GLOB entries LIST_DIRECTORIES true RELATIVE "${dir}" "${dir}/*")
    foreach(entry ${entries})
      list(APPEND names "${dir}|${entry}")
    endforeach()
  endforeach()
  set(${out} "${names}" PARENT_SCOPE)
endfunction()

crash_places(before)
list(LENGTH before before_count)

set(cases
  "segv:139:SIGSEGV:access violation:the vectored handler"
  "abort:134:SIGABRT:abort:the SIGABRT handler"
  "div:136:SIGFPE:integer divide by zero:the vectored handler"
  "throw:134:SIGABRT:C\\+\\+ exception:the filter"
  "trap:133:SIGTRAP:breakpoint:the filter"
  "invparam:134:SIGABRT:invalid parameter:the invalid-parameter handler"
  "purecall:134:SIGABRT:pure virtual call:the pure-call handler"
  "stack:139:SIGSEGV:access violation:the vectored handler")

set(problems "")
set(names "")
set(runs 0)
foreach(program ${programs})
  get_filename_component(name "${program}" NAME_WE)
  list(APPEND names "${name}")
  foreach(case ${cases})
    string(REPLACE ":" ";" parts "${case}")
    list(GET parts 0 kind)
    list(GET parts 1 code)
    list(GET parts 2 sig)
    list(GET parts 3 what)
    list(GET parts 4 handler)
    if(KINDS AND NOT kind IN_LIST KINDS)
      continue()
    endif()
    set(ENV{NULLDC_TEST_GUARD_FAULT} "${kind}")
    execute_process(COMMAND "${WINRUN}" --timeout 60 -- "${program}"
                    WORKING_DIRECTORY "${WORK}" TIMEOUT 120 RESULT_VARIABLE rc
                    OUTPUT_FILE "${WORK}/${name}-${kind}.out" ERROR_FILE "${WORK}/${name}-${kind}.err")
    unset(ENV{NULLDC_TEST_GUARD_FAULT})
    math(EXPR runs "${runs} + 1")
    file(READ "${WORK}/${name}-${kind}.err" err)
    string(REGEX MATCH "${name}: unhandled ${sig} \\(${what}\\) at address [^\n]*, pc [^\n]*, ${handler}: terminating" line "${err}")
    string(REGEX MATCH "winrun: desktop [^\n]*: ([0-9]+) window\\(s\\) seen, ([0-9]+) visible" windows "${err}")
    set(visible "${CMAKE_MATCH_2}")
    if(NOT rc STREQUAL "${code}" OR NOT line OR NOT windows OR NOT visible STREQUAL "0")
      string(REGEX MATCH "[^\n]*: unhandled [^\n]*" said "${err}")
      string(REPLACE "\\" "" shown "${what}")
      list(APPEND problems "${name} ${kind}: exit ${rc} (to be ${code}), the line of ${handler} for ${sig} (${shown}): '${said}', visible windows: '${visible}'")
    else()
      message("${name} ${kind}: exit ${rc}; ${line}; no window")
    endif()
  endforeach()
endforeach()

# Windows Error Reporting writes its report behind the process's end: wait a little for one
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 3)
crash_places(after)
list(LENGTH after after_count)

set(ours "")
foreach(entry ${after})
  list(FIND before "${entry}" at)
  if(at EQUAL -1)
    # Windows Error Reporting shortens the program's name in the names of its folders: its first 12 characters
    string(TOLOWER "${entry}" low)
    set(owner "")
    foreach(name ${names})
      string(TOLOWER "${name}" lowname)
      string(SUBSTRING "${lowname}" 0 12 lowname)
      string(FIND "${low}" "${lowname}" named)
      if(NOT named EQUAL -1)
        set(owner "${name}")
      endif()
    endforeach()
    if(owner)
      list(APPEND ours "${entry} (${owner})")
    else()
      message("a new entry in the crash-report places, of another program: ${entry}")
    endif()
  endif()
endforeach()
message("crash-report places: ${before_count} entries before the runs, ${after_count} behind them")
if(ours)
  list(APPEND problems "crash reports of the programs: ${ours}")
endif()

if(problems)
  string(REPLACE ";" "\n  " problems "${problems}")
  message(FATAL_ERROR "the Windows policy of the tests project did not hold:\n  ${problems}")
endif()
list(LENGTH names count)
list(JOIN names ", " names)
message("PASS: ${runs} faults of ${count} programs (${names}) ended them with the policy's line and 128 + the signal, with no window and no crash report")
