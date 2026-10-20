## Guest programs of the debugger's tests ##
#
#	One directory per guest program that is part of the tree: its sources, its committed image, and the tests
#	that run the emulator on it, headless, on the emulator the system tests build (the default build,
#	<build dir>/system/emulator, or NULLDC_EMULATOR). This file is the one place they are registered: every
#	subdirectory that has a CMakeLists.txt is added, in the order of the names, so a package puts its directory
#	here and edits no shared file.
#
#	What a subdirectory gets from here:
#
#	    Python3_EXECUTABLE       python, as it was found here
#	    nulldc_guest_emulator    the emulator binary its tests run
#	    nulldc_guest_skip        empty, or the reason why no emulator can be run: a test is then registered
#	                             as skipped (nulldc_add_skipped_test)
#
#	and from the project: nulldc_tests_path, nulldc_add_skipped_test, nulldc_add_missing_data_test and the
#	cache variables (NULLDC_ELF_DIR, NULLDC_TESTS_REQUIRE_DATA, ...).
#
#	A subdirectory may define a function for those behind it. A CMake function runs in the scope of its
#	caller: a variable that the CMakeLists.txt of its own directory sets does not exist in a sibling. Such a
#	function therefore uses the variables above and nothing else.
#
#	tests/CMakeLists.txt adds this directory behind the system tests, which create NULLDC_SYSTEM_TESTS,
#	NULLDC_EMULATOR and NULLDC_ELF_DIR.
#

find_package(Python3 COMPONENTS Interpreter)

set(nulldc_guest_skip "")
if(NOT NULLDC_SYSTEM_TESTS AND NOT NULLDC_EMULATOR)
  set(nulldc_guest_skip "NULLDC_SYSTEM_TESTS is OFF and no NULLDC_EMULATOR is given: there is no emulator to run")
elseif(${HOST_OS} EQUAL ${OS_WINDOWS})
  set(nulldc_guest_skip "W64b")
elseif(NOT ${HOST_OS} EQUAL ${OS_LINUX})
  set(nulldc_guest_skip "headless mode is there on Linux hosts only")
elseif(NOT Python3_Interpreter_FOUND)
  set(nulldc_guest_skip "no python3 to run the scripts of tests/debugger/guest with")
endif()

if(NULLDC_EMULATOR)
  set(nulldc_guest_emulator "${NULLDC_EMULATOR}")
else()
  # what tests/system builds, every time the project is built
  set(nulldc_guest_emulator "${CMAKE_BINARY_DIR}/system/emulator/nulldc3")
endif()

# CONFIGURE_DEPENDS: a directory that is added to a tree whose build is configured already is found at the next
# build, which runs cmake again, and not only at the next configure by hand
file(GLOB nulldc_guest_lists RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/CMakeLists.txt")
list(SORT nulldc_guest_lists)

foreach(list ${nulldc_guest_lists})
  get_filename_component(entry "${list}" DIRECTORY)
  add_subdirectory(${entry})
endforeach()
