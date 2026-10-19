## rebrand.names: the tree says "reicast" only as history ##
#
#	reicast/ became nulldc3/ and every live name that said reicast got a nulldc3 name; what still says it is
#	history (licences, credits, provenance notes, the records under docs/nulldc3/, the old directories the
#	system tests prove are not read). rebrand_names.py lists every line of a tracked text file that says reicast,
#	in any case (git -C <tree> grep -I -i -n reicast), and every tracked file whose name says it, and compares
#	them with allowed.txt: an occurrence that no entry allows fails, and so does a row whose line is gone.
#	README.md of this directory has the format of allowed.txt and how to change a row.
#
#	Needs git and python3, and the tree as a git checkout: a tree that is none (an archive, a copy) is skipped,
#	a failure with NULLDC_TESTS_REQUIRE_DATA.
#

find_package(Python3 COMPONENTS Interpreter)
find_package(Git QUIET)

if(NOT Python3_Interpreter_FOUND)
  nulldc_add_missing_data_test(rebrand.names "no python3 to run tests/rebrand/rebrand_names.py with")
elseif(NOT GIT_FOUND)
  nulldc_add_missing_data_test(rebrand.names "no git to list the tree's lines that say reicast with")
else()
  set(nulldc_rebrand_require "")
  if(NULLDC_TESTS_REQUIRE_DATA)
    set(nulldc_rebrand_require --require-data)
  endif()
  add_test(NAME rebrand.names
           COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/rebrand_names.py"
                   --tree "${nulldc_root_path}" --allowed "${CMAKE_CURRENT_SOURCE_DIR}/allowed.txt"
                   --git "${GIT_EXECUTABLE}" ${nulldc_rebrand_require})
  set_tests_properties(rebrand.names PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()
