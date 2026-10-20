# version.binary (CMakeLists.txt of this directory): the emulator's --version says the version of the tree now.
#
#   cmake -DEMULATOR=<nulldc3> -DTREE=<the top of the tree> -DWORK=<scratch directory> -P binary.cmake
#
# nulldc3/cmake/version.cmake is run on the tree into WORK, as a build runs it, and the emulator is asked with
# --version (behind P0's keys of a headless run, which --version leaves unread): it has to exit with 0 and its last
# line on stdout has to be "nullDC <version>" exactly.
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

execute_process(COMMAND "${EMULATOR}" --config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800
                        --version
                WORKING_DIRECTORY "${WORK}" TIMEOUT 60
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
string(REGEX REPLACE "[\r\n]+$" "" text "${out}")
string(FIND "${text}" "\n" at REVERSE)
math(EXPR at "${at} + 1")
string(SUBSTRING "${text}" ${at} -1 last)
string(REGEX REPLACE "\r$" "" last "${last}")
message("version.binary: the tree says ${version}; ${EMULATOR} --version exit status ${rc}, last line \"${last}\"")
if(NOT rc EQUAL 0 OR NOT last STREQUAL "nullDC ${version}")
  message(FATAL_ERROR "version.binary: expected exit status 0 and \"nullDC ${version}\"\nstdout:\n${out}\nstderr:\n${err}")
endif()
