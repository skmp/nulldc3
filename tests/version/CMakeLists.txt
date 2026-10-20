## The version of the emulator ##
#
#	version.derive   nulldc3/cmake/version.cmake, the rule that makes the version at every build, on git
#	                 repositories the test makes below its build directory (derive.cmake has every case): on the tag,
#	                 after it, dirty, untracked and ignored files, no tag, tags that are no version tags, a describe
#	                 output of another form, a later tag than the base, a worktree, a submodule and its
#	                 superproject, a shallow clone, a copy without .git and one inside another repository, no git on
#	                 the PATH, GIT_DIR of another repository in the environment; the derived forms for N = 0, 5 and
#	                 70000; the three files, written only when their text changes. Needs git.
#	version.build_time
#	                 the version is made when the emulator is built, not when it is configured: a clone of the tree
#	                 as it is now, configured once, its target nulldc3_version built, an empty commit, the target
#	                 built again: the new <h>, CMake not run again, nothing written into the clone
#	                 (build_time.cmake; nothing is compiled). Needs git and a tree that is a git checkout.
#	version.cl       --version of cfg/cl.cpp (in nulldc_cfg of tests/cfg): rv_cli_finish, stdout exactly
#	                 "nullDC <version>" and a line end, and cl_FinishText() that same text (what the Windows build
#	                 shows where stdout cannot be read); for --help both the usage text (cl_test.cpp). Needs no data.
#	version.binary   the emulator under test (NULLDC_EMULATOR, else the one the system tests build) prints
#	                 "nullDC <version>" for --version, <version> what version.cmake makes of the tree now, as the
#	                 last line of its stdout, with nothing in front of it but the three setup lines of the Linux
#	                 and macOS builds (binary.cmake): a binary of another commit fails (one not built again since a
#	                 commit). The system tests' emulator is configured again at every build of this project
#	                 (tests/system/build_emulator.cmake), so with it this test cannot tell a version made at
#	                 configure time from one made at the build; version.build_time can, in every configuration.
#	                 Skipped when there is no emulator to ask.
#

find_package(Git QUIET)
if(GIT_FOUND)
  add_test(NAME version.derive
           COMMAND "${CMAKE_COMMAND}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/derive" "-DGIT=${GIT_EXECUTABLE}"
                   "-DSCRIPT=${nulldc_root_path}/nulldc3/cmake/version.cmake" -P "${CMAKE_CURRENT_SOURCE_DIR}/derive.cmake")
  set_tests_properties(version.derive PROPERTIES TIMEOUT 300)
else()
  nulldc_add_missing_data_test(version.derive "no git to make the repositories of the test with")
endif()

if(GIT_FOUND)
  # a clone needs a checkout to be made of: the top of a work tree (as version.cmake asks)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${nulldc_root_path}" rev-parse --show-prefix
                  RESULT_VARIABLE nulldc_version_rc OUTPUT_VARIABLE nulldc_version_prefix ERROR_QUIET
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(nulldc_version_rc EQUAL 0 AND nulldc_version_prefix STREQUAL "")
    add_test(NAME version.build_time
             COMMAND "${CMAKE_COMMAND}" "-DTREE=${nulldc_root_path}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/build_time"
                     "-DGIT=${GIT_EXECUTABLE}" "-DGENERATOR=${CMAKE_GENERATOR}" "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/build_time.cmake")
    set_tests_properties(version.build_time PROPERTIES TIMEOUT 300)
  else()
    nulldc_add_skipped_test(version.build_time "the tree is no git checkout: there is no commit to make a clone of")
  endif()
else()
  nulldc_add_missing_data_test(version.build_time "no git to make the clone of the test with")
endif()

add_executable(version_cl cl_test.cpp)
target_link_libraries(version_cl PRIVATE nulldc_cfg)
if(WIN32)
  # the policy of the tests project for a program that dies (../common/win_policy.cpp: no box, no crash report, one line and 128 + the signal's number)
  target_sources(version_cl PRIVATE "${nulldc_tests_path}/common/win_policy.cpp")
endif()
add_test(NAME version.cl COMMAND version_cl "${CMAKE_CURRENT_BINARY_DIR}/cl_work")
set_tests_properties(version.cl PROPERTIES TIMEOUT 60)

set(nulldc_version_emulator "")
if(NULLDC_EMULATOR)
  set(nulldc_version_emulator "${NULLDC_EMULATOR}")
elseif(TARGET nulldc_emulator)
  # tests/system builds it (nulldc_system_emulator)
  set(nulldc_version_emulator "${CMAKE_BINARY_DIR}/system/emulator/nulldc3")
endif()
if(nulldc_version_emulator)
  # on Windows the emulator runs through nulldc3-winrun (tests/windows), as every program the tests start there
  set(nulldc_version_winrun "")
  if(WIN32)
    set(nulldc_version_winrun "-DWINRUN=${nulldc_winrun}")
  endif()
  add_test(NAME version.binary
           COMMAND "${CMAKE_COMMAND}" "-DEMULATOR=${nulldc_version_emulator}" "-DTREE=${nulldc_root_path}"
                   "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/binary" ${nulldc_version_winrun}
                   -P "${CMAKE_CURRENT_SOURCE_DIR}/binary.cmake")
  set_tests_properties(version.binary PROPERTIES TIMEOUT 120)
else()
  nulldc_add_skipped_test(version.binary "no emulator to ask: NULLDC_EMULATOR is empty and the system tests build none")
endif()
