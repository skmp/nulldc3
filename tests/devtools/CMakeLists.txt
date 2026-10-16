## devtools.commit: the UI checkout the tests are given is the UI the tree pins ##
#
#	The debugger's tests read the UI of NULLDC_DEVTOOLS_DIR (the contract runner, the panel ids, the oracle of the
#	DSP playground, the ws package). The tree pins the UI it is tested with: the gitlink of its submodule devtools/
#	(dreamcast-devtools). When NULLDC_DEVTOOLS_DIR is a git checkout (the top of one) this test fails if its HEAD
#	is another commit than the gitlink, and says what it compared; uncommitted changes in its tracked files are
#	named in the output. A tree that is not a git checkout, or a UI directory that is none, has nothing to compare:
#	the test says so and passes.
#
#	Needs NULLDC_DEVTOOLS_DIR (a missing UI is missing data, like for the tests that read it).
#

if(NOT NULLDC_DEVTOOLS_DIR)
  nulldc_add_missing_data_test(devtools.commit
    "NULLDC_DEVTOOLS_DIR is empty: no UI checkout to compare with the submodule devtools/")
  return()
endif()

find_package(Git QUIET)
add_test(NAME devtools.commit
         COMMAND "${CMAKE_COMMAND}" "-DTREE=${nulldc_root_path}" "-DUI=${NULLDC_DEVTOOLS_DIR}" "-DGIT=${GIT_EXECUTABLE}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/check_commit.cmake")
set_tests_properties(devtools.commit PROPERTIES TIMEOUT 60)
