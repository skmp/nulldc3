# common.guard.windows: the Windows policy of test_guard.cpp, which every suite that links it has from before main.
#
#   cmake -DWINRUN=<nulldc3-winrun.exe> -DPROGRAM=<a program that links test_guard.cpp> -DWORK=<scratch> -P guard_windows.cmake
#
# PROGRAM is run through nulldc3-winrun with NULLDC_TEST_GUARD_FAULT=<kind>, which test_guard_init raises outside of
# any guarded call: an access violation (segv), abort(), an integer divide by zero (div) and a C++ exception that
# nobody catches (throw). Each has to end the program with the line of the policy on stderr and 128 + the signal's
# number (139, 134, 136, 134), with no window. And none may leave a crash report: the crash-report places of the
# profile (%LOCALAPPDATA%\CrashDumps, the ReportQueue and ReportArchive of Windows Error Reporting under
# %LOCALAPPDATA% and %ProgramData%) are listed by name before and after, and an entry that is new behind the runs
# and names PROGRAM (by the first 12 characters of its name, which Windows Error Reporting keeps in the names of its
# folders) fails the test; other programs of the computer may report crashes meanwhile: an entry of another program
# is printed, and is no failure.

cmake_minimum_required(VERSION 3.16)

if(NOT WINRUN OR NOT PROGRAM OR NOT WORK)
  message(FATAL_ERROR "guard_windows.cmake: needs -DWINRUN= -DPROGRAM= -DWORK=")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

get_filename_component(program_name "${PROGRAM}" NAME_WE)

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

set(problems "")
foreach(case segv:139:SIGSEGV abort:134:SIGABRT div:136:SIGFPE throw:134:SIGABRT)
  string(REPLACE ":" ";" parts "${case}")
  list(GET parts 0 kind)
  list(GET parts 1 code)
  list(GET parts 2 sig)
  set(ENV{NULLDC_TEST_GUARD_FAULT} "${kind}")
  execute_process(COMMAND "${WINRUN}" --timeout 60 -- "${PROGRAM}"
                  WORKING_DIRECTORY "${WORK}" TIMEOUT 120 RESULT_VARIABLE rc
                  OUTPUT_FILE "${WORK}/${kind}.out" ERROR_FILE "${WORK}/${kind}.err")
  unset(ENV{NULLDC_TEST_GUARD_FAULT})
  file(READ "${WORK}/${kind}.err" err)
  string(REGEX MATCH "test_guard: unhandled ${sig} at address [^\n]*: terminating" line "${err}")
  string(REGEX MATCH "winrun: desktop [^\n]*: ([0-9]+) window\\(s\\) seen, ([0-9]+) visible" windows "${err}")
  set(visible "${CMAKE_MATCH_2}")
  if(NOT rc STREQUAL "${code}" OR NOT line OR NOT windows OR NOT visible STREQUAL "0")
    list(APPEND problems "${kind}: exit ${rc} (not ${code}), the line of the policy: '${line}', visible windows: '${visible}'")
  else()
    message("${kind}: exit ${rc}; ${line}; no window")
  endif()
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
    string(TOLOWER "${program_name}" lowprogram)
    string(SUBSTRING "${lowprogram}" 0 12 lowprogram)
    string(FIND "${low}" "${lowprogram}" named)
    if(named EQUAL -1)
      message("a new entry in the crash-report places, of another program: ${entry}")
    else()
      list(APPEND ours "${entry}")
    endif()
  endif()
endforeach()
message("crash-report places: ${before_count} entries before the runs, ${after_count} behind them")
if(ours)
  list(APPEND problems "crash reports of ${program_name}: ${ours}")
endif()

if(problems)
  string(REPLACE ";" "\n  " problems "${problems}")
  message(FATAL_ERROR "the Windows policy of test_guard did not hold:\n  ${problems}")
endif()
message("PASS: the four faults ended ${program_name} with the policy's line and 128 + the signal, with no window and no crash report")
