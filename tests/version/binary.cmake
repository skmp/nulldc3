# version.binary (CMakeLists.txt of this directory): the emulator's --version says the version of the tree now.
#
#   cmake -DEMULATOR=<nulldc3> -DTREE=<the top of the tree> -DWORK=<scratch directory> -P binary.cmake
#
# nulldc3/cmake/version.cmake is run on the tree into WORK, as a build runs it, and the emulator is asked with
# --version (behind P0's keys of a headless run, which --version leaves unread): it has to exit with 0, and its
# stdout has to be the line "nullDC <version>" and a line end, exactly, with nothing in front of it, on every system:
# the Linux build answers --version before common_linux_setup() of libswirl/linux/common.cpp prints its lines
# (libswirl/linux-dist/main.cpp), the macOS build prints its "Linux paging" line on stderr. (execute_process drops
# the \r of a \r\n: a carriage return in the emulator's stdout is not seen here.)
#
# An emulator of another commit says another version: NULLDC_EMULATOR built before a commit, or the system tests'
# emulator when the tests project was not built again after one. The system tests' emulator is configured again at
# every build of the tests project (tests/system/build_emulator.cmake), so after a commit and a build it says the
# new version also when the version is made at configure time: with it this test cannot tell, version.build_time
# (build_time.cmake) is the test of that. With NULLDC_EMULATOR an emulator built again after a commit without a
# configure, this test sees a version made at configure time too.

if(NOT EMULATOR OR NOT TREE OR NOT WORK)
  message(FATAL_ERROR "version.binary: needs -DEMULATOR=<nulldc3> -DTREE=<the top of the tree> -DWORK=<scratch directory>")
endif()
if(NOT EXISTS "${EMULATOR}")
  message(FATAL_ERROR "version.binary: there is no emulator ${EMULATOR}")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${TREE}" "-DOUTPUT_DIR=${WORK}"
                        -P "${TREE}/nulldc3/cmake/version.cmake"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "version.binary: version.cmake on ${TREE}: exit status ${rc}: ${out}${err}")
endif()
file(STRINGS "${WORK}/nulldc3-version.txt" version LIMIT_COUNT 1)

# Windows: through nulldc3-winrun (-DWINRUN=, tests/windows), macOS: through macrun.py (-DMACRUN=<python>|<macrun.py>,
# tests/macos), with a portable directory of the test's own
set(launcher "")
set(portable "")
if(WINRUN)
  set(launcher "${WINRUN}" --timeout 50 --)
  file(MAKE_DIRECTORY "${WORK}/portable")
  set(portable --portable "${WORK}/portable")
elseif(MACRUN)
  string(REPLACE "|" ";" launcher "${MACRUN}")
  list(APPEND launcher --name version --work "${WORK}" --timeout 50 --)
  file(MAKE_DIRECTORY "${WORK}/portable")
  set(portable --portable "${WORK}/portable")
endif()
execute_process(COMMAND ${launcher} "${EMULATOR}" ${portable}
                        --config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800
                        --version
                WORKING_DIRECTORY "${WORK}" TIMEOUT 60
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)

set(line "nullDC ${version}\n")
set(problems "")
if(NOT rc EQUAL 0)
  list(APPEND problems "exit status ${rc}, not 0")
endif()
if(NOT out STREQUAL line)
  list(APPEND problems "stdout is not the one line \"nullDC ${version}\" and a line end")
endif()

string(REGEX REPLACE "\n$" "" shown "${out}")
string(REPLACE "\n" " | " shown "${shown}")
message("version.binary: the tree says ${version}; ${EMULATOR} --version exit status ${rc}, stdout \"${shown}\"")
if(problems)
  string(REPLACE ";" "; " problems "${problems}")
  message(FATAL_ERROR "version.binary: ${problems}\nexpected exit status 0 and \"nullDC ${version}\"\nstdout:\n${out}\nstderr:\n${err}")
endif()
