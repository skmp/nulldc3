# devtools.commit (CMakeLists.txt of this directory):
#
#   cmake -DTREE=<emulator tree> -DUI=<UI checkout> -DGIT=<git> -P check_commit.cmake

if(NOT EXISTS "${TREE}/.git")
  message("devtools.commit: ${TREE} is not a git checkout and pins no commit for devtools/: not compared")
  return()
endif()
if(NOT IS_DIRECTORY "${UI}")
  message(FATAL_ERROR "devtools.commit: the UI checkout ${UI} is not a directory")
endif()
if(NOT GIT)
  message(FATAL_ERROR "devtools.commit: no git, the commit of ${UI} cannot be compared with the submodule devtools/")
endif()

execute_process(COMMAND "${GIT}" -C "${TREE}" ls-files --stage -- devtools
                RESULT_VARIABLE rc OUTPUT_VARIABLE stage ERROR_VARIABLE error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT rc EQUAL 0 OR NOT stage MATCHES "^160000 ([0-9a-f]+) ")
  message(FATAL_ERROR "devtools.commit: ${TREE} has no submodule devtools/ (no gitlink in its index): ${stage}${error}")
endif()
set(pin "${CMAKE_MATCH_1}")

execute_process(COMMAND "${GIT}" -C "${UI}" rev-parse --show-toplevel
                RESULT_VARIABLE rc OUTPUT_VARIABLE top ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT rc EQUAL 0)
  message("devtools.commit: ${UI} is not a git checkout: its commit is not compared with the submodule's, ${pin}")
  return()
endif()
get_filename_component(ui_real "${UI}" REALPATH)
get_filename_component(top_real "${top}" REALPATH)
if(NOT ui_real STREQUAL top_real)
  message("devtools.commit: ${UI} is inside the git checkout ${top}, not the top of one: not compared with the submodule's commit, ${pin}")
  return()
endif()

execute_process(COMMAND "${GIT}" -C "${UI}" rev-parse HEAD
                RESULT_VARIABLE rc OUTPUT_VARIABLE head ERROR_VARIABLE error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "devtools.commit: git cannot say the commit of ${UI}: ${error}")
endif()
if(NOT head STREQUAL pin)
  message(FATAL_ERROR "devtools.commit: the UI checkout ${UI} is at ${head}, the submodule devtools/ of ${TREE} pins ${pin}. "
                      "Test with the UI the tree pins (git -C ${UI} checkout ${pin}), or pin the UI that was tested.")
endif()

execute_process(COMMAND "${GIT}" -C "${UI}" status --porcelain --untracked-files=no
                RESULT_VARIABLE rc OUTPUT_VARIABLE changes ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT changes STREQUAL "")
  string(REPLACE "\n" "; " changes "${changes}")
  message("devtools.commit: ${UI} is at ${head}, the commit the submodule devtools/ of ${TREE} pins, with uncommitted changes in tracked files: ${changes}")
else()
  message("devtools.commit: ${UI} is at ${head}, the commit the submodule devtools/ of ${TREE} pins")
endif()
