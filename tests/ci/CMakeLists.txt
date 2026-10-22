## ci.*: the emulator's CI files ##
#
#	ci.hygiene     nulldc3/ci/hygiene.py on the tree: no tracked file names a home directory of Linux, macOS or
#	               Windows, a path of WSL's network or drive mounts, a private IPv4 address or a .local host name,
#	               but the lines nulldc3/ci/hygiene-allow.txt allows, and every row of it still allows a line.
#	               Skipped when the tree is not a git checkout, a failure with NULLDC_TESTS_REQUIRE_DATA.
#	ci.workflows   nulldc3/ci/check_workflows.py on .github/workflows/ci.yml, the rules of the workflow that
#	               actionlint does not check, with its self-test (mutations of the workflow that each have to fail a
#	               rule); then, on Linux and macOS, release_test.py: nulldc3/ci/release.sh against a stand-in for gh
#	               in a scratch repository below the build directory (check creates nothing; publish only on the
#	               push of a tag). On Windows the stand-in part is not run (release.sh is a bash script, and no test
#	               there runs a POSIX shell): the target says so.
#	ci.scripts     scripts_test.py: nulldc3/ci/zipcheck.py on zips made here (the entry names of the central
#	               directory: no "\", the folder's own entry, exactly the files below it).
#	README.md of this directory has more.
#

find_package(Python3 COMPONENTS Interpreter)
find_package(Git QUIET)

if(NOT Python3_Interpreter_FOUND)
  nulldc_add_missing_data_test(ci.hygiene "no python3 to run nulldc3/ci/hygiene.py with")
  nulldc_add_missing_data_test(ci.workflows "no python3 to run nulldc3/ci/check_workflows.py with")
  nulldc_add_missing_data_test(ci.scripts "no python3 to run tests/ci/scripts_test.py with")
  return()
endif()

if(NOT GIT_FOUND)
  nulldc_add_missing_data_test(ci.hygiene "no git to list the tree's tracked files with")
else()
  add_test(NAME ci.hygiene
           COMMAND "${Python3_EXECUTABLE}" -B "${nulldc_root_path}/nulldc3/ci/hygiene.py" --tree "${nulldc_root_path}")
  set_tests_properties(ci.hygiene PROPERTIES TIMEOUT 120)
  if(NOT NULLDC_TESTS_REQUIRE_DATA)
    set_tests_properties(ci.hygiene PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()

set(nulldc_ci_release --release-test)
if(${HOST_OS} EQUAL ${OS_WINDOWS})
  set(nulldc_ci_release "")
endif()
add_test(NAME ci.workflows
         COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/workflows_test.py"
                 --tree "${nulldc_root_path}" --work "${CMAKE_CURRENT_BINARY_DIR}/work" ${nulldc_ci_release})
set_tests_properties(ci.workflows PROPERTIES TIMEOUT 300 ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)

add_test(NAME ci.scripts
         COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/scripts_test.py"
                 --tree "${nulldc_root_path}" --work "${CMAKE_CURRENT_BINARY_DIR}/scripts")
set_tests_properties(ci.scripts PROPERTIES TIMEOUT 300 ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
