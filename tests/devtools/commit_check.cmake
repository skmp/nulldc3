# devtools.commit_check (CMakeLists.txt of this directory): check_commit.cmake, the script of devtools.commit, on
# git repositories made below <work>, each outcome it has:
#
#   cmake -DWORK=<scratch directory> -DGIT=<git> -P commit_check.cmake
#
# ui/ is a UI repository with two empty commits (HEAD the second); a tree is a repository whose index has the
# gitlink devtools at the commit it pins. The tree whose gitlink is the UI's HEAD passes and says so; one that pins
# the first commit fails and names both; one without a gitlink fails; a UI directory that is no git checkout, or
# one inside a checkout but not its top, and a tree that is no git checkout are not compared and pass.

if(NOT WORK OR NOT GIT)
  message(FATAL_ERROR "devtools.commit_check: needs -DWORK=<scratch directory> -DGIT=<git>")
endif()
set(script "${CMAKE_CURRENT_LIST_DIR}/check_commit.cmake")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
# git looks no further up than WORK: the scratch may be inside another checkout
set(ENV{GIT_CEILING_DIRECTORIES} "${WORK}")
# and reads no configuration of the user or of the system
set(ENV{GIT_CONFIG_NOSYSTEM} 1)
set(ENV{GIT_CONFIG_GLOBAL} "${WORK}/no-gitconfig")
set(ENV{HOME} "${WORK}")

function(git dir)
  execute_process(COMMAND "${GIT}" -C "${dir}" ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "devtools.commit_check: git -C ${dir} ${ARGN}: ${out}${err}")
  endif()
  set(git_out "${out}" PARENT_SCOPE)
endfunction()

# the UI: two commits
file(MAKE_DIRECTORY "${WORK}/ui/sub")
git("${WORK}/ui" init -q)
foreach(n 1 2)
  git("${WORK}/ui" -c user.name=t -c user.email=t@t -c commit.gpgsign=false commit -q --allow-empty -m "commit ${n}")
  git("${WORK}/ui" rev-parse HEAD)
  set(ui_${n} "${git_out}")
endforeach()

# trees: pinned to the UI's HEAD, to its first commit, to nothing
foreach(name head old none)
  file(MAKE_DIRECTORY "${WORK}/tree_${name}")
  git("${WORK}/tree_${name}" init -q)
endforeach()
git("${WORK}/tree_head" update-index --add --cacheinfo "160000,${ui_2},devtools")
git("${WORK}/tree_old" update-index --add --cacheinfo "160000,${ui_1},devtools")
file(MAKE_DIRECTORY "${WORK}/plain" "${WORK}/tree_plain")

set(failed 0)
# expect(<what> <tree> <ui> <0|fail> <text>): the script's exit status, and a text its output has (with every run
# of blanks and line ends as one blank: CMake wraps the lines of an error)
function(expect what tree ui status wanted)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DTREE=${tree}" "-DUI=${ui}" "-DGIT=${GIT}" -P "${script}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REGEX REPLACE "[ \t\r\n]+" " " text "${out}${err}")
  string(STRIP "${text}" text)
  string(FIND "${text}" "${wanted}" at)
  if(status STREQUAL "0")
    set(status_ok FALSE)
    if(rc EQUAL 0)
      set(status_ok TRUE)
    endif()
  else()
    set(status_ok TRUE)
    if(rc EQUAL 0)
      set(status_ok FALSE)
    endif()
  endif()
  if(status_ok AND at GREATER -1)
    message("devtools.commit_check: ${what}: exit status ${rc}: ${text}")
  else()
    message("devtools.commit_check: FAIL ${what}: exit status ${rc} (expected ${status}), output without '${wanted}', or the wrong status: ${text}")
    math(EXPR count "${failed} + 1")
    set(failed ${count} PARENT_SCOPE)
  endif()
endfunction()

expect("the UI at the pinned commit" "${WORK}/tree_head" "${WORK}/ui" 0
       "devtools.commit: ${WORK}/ui is at ${ui_2}, the commit the submodule devtools/ of ${WORK}/tree_head pins")
expect("the UI at another commit than the pinned one" "${WORK}/tree_old" "${WORK}/ui" fail
       "the UI checkout ${WORK}/ui is at ${ui_2}, the submodule devtools/ of ${WORK}/tree_old pins ${ui_1}")
expect("a tree without the gitlink" "${WORK}/tree_none" "${WORK}/ui" fail
       "${WORK}/tree_none has no submodule devtools/")
expect("a UI directory that is no git checkout" "${WORK}/tree_head" "${WORK}/plain" 0
       "${WORK}/plain is not a git checkout: its commit is not compared with the submodule's, ${ui_2}")
expect("a UI directory inside a checkout" "${WORK}/tree_head" "${WORK}/ui/sub" 0
       "${WORK}/ui/sub is inside the git checkout ${WORK}/ui, not the top of one: not compared")
expect("a tree that is no git checkout" "${WORK}/tree_plain" "${WORK}/ui" 0
       "${WORK}/tree_plain is not a git checkout and pins no commit for devtools/: not compared")

file(REMOVE_RECURSE "${WORK}")
if(failed)
  message(FATAL_ERROR "devtools.commit_check: ${failed} outcomes are not what check_commit.cmake has to say")
endif()
message("devtools.commit_check: every outcome of check_commit.cmake as it should be")
