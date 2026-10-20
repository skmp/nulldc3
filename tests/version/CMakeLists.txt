## The version of the emulator ##
#
#	version.derive   nulldc3/cmake/version.cmake, the rule that makes the version at every build, on git
#	                 repositories the test makes below its build directory (derive.cmake has every case): on the tag,
#	                 after it, dirty, untracked and ignored files, no tag, tags that are no version tags, a describe
#	                 output of another form, a later tag than the base, a worktree, a submodule and its
#	                 superproject, a shallow clone, a copy without .git and one inside another repository, no git on
#	                 the PATH, GIT_DIR of another repository in the environment; the derived forms for N = 0, 5 and
#	                 70000; the three files, written only when their text changes. Needs git.
#	version.binary   the emulator under test (NULLDC_EMULATOR, else the one the system tests build) prints
#	                 "nullDC <version>" for --version, <version> what version.cmake makes of the tree now
#	                 (binary.cmake). Skipped when there is no emulator to ask.
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

set(nulldc_version_emulator "")
if(NULLDC_EMULATOR)
  set(nulldc_version_emulator "${NULLDC_EMULATOR}")
elseif(TARGET nulldc_emulator)
  # tests/system builds it (nulldc_system_emulator)
  set(nulldc_version_emulator "${CMAKE_BINARY_DIR}/system/emulator/nulldc3")
endif()
if(nulldc_version_emulator)
  add_test(NAME version.binary
           COMMAND "${CMAKE_COMMAND}" "-DEMULATOR=${nulldc_version_emulator}" "-DTREE=${nulldc_root_path}"
                   "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/binary" -P "${CMAKE_CURRENT_SOURCE_DIR}/binary.cmake")
  set_tests_properties(version.binary PROPERTIES TIMEOUT 120)
else()
  nulldc_add_skipped_test(version.binary "no emulator to ask: NULLDC_EMULATOR is empty and the system tests build none")
endif()
