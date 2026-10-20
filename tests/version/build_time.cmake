# version.build_time (CMakeLists.txt of this directory): the emulator's version is made when it is built, not when
# it is configured.
#
#   cmake -DTREE=<the top of the tree> -DWORK=<scratch directory> -DGIT=<git> -DGENERATOR=<generator>
#         [-DMAKE_PROGRAM=<make program>] -P build_time.cmake
#
# A clone of the tree (git clone --shared, at the tree's HEAD; the tracked files the tree has changed since are
# copied over and committed there, so that it is the tree as it is now) is configured once, as the emulator's build
# is, with -DNULLDC_DEVTOOLS_DIST=OFF, and only its target nulldc3_version is built; nothing is compiled. Then an
# empty commit is made in the clone and nulldc3_version is built again. After each build:
#
#   <build>/nulldc3-version.txt is what version.cmake makes of the clone at that moment, and the header's
#   NULLDC_GIT_HASH is the clone's `git rev-parse HEAD` (so after the commit the new <h>);
#   CMake did not run again (CMAKE_PROJECT_INCLUDE counts every configure: one, the first);
#   the clone's `git status --porcelain --ignored` is empty (nothing was written into the source tree).
#
# A version made at configure time stays the first commit's after the second build; one that is made again because
# the configure depends on the files of .git runs CMake again; either fails here, whatever emulator the tests
# project asks otherwise. (version.binary asks a binary, and the tests project's own emulator is configured again at
# every build, tests/system/build_emulator.cmake: there it cannot tell.)

if(NOT TREE OR NOT WORK OR NOT GIT OR NOT GENERATOR)
  message(FATAL_ERROR "version.build_time: needs -DTREE=<the top of the tree> -DWORK=<scratch directory> -DGIT=<git> -DGENERATOR=<generator>")
endif()
get_filename_component(WORK "${WORK}" ABSOLUTE)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# git asks the tree without taking its index.lock, and no variable of a git hook points it at another repository
set(ENV{GIT_OPTIONAL_LOCKS} 0)
foreach(name GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_PREFIX GIT_OBJECT_DIRECTORY
             GIT_ALTERNATE_OBJECT_DIRECTORIES)
  unset(ENV{${name}})
endforeach()
# version.cmake runs "git" from the PATH: the git of this test comes first there
get_filename_component(git_bin "${GIT}" DIRECTORY)
if(CMAKE_HOST_WIN32)
  set(ENV{PATH} "${git_bin};$ENV{PATH}")
else()
  set(ENV{PATH} "${git_bin}:$ENV{PATH}")
endif()

set(git_c -c user.name=nulldc-test -c user.email=test@example.invalid -c commit.gpgsign=false)

# git(<dir> <args>...): git -C <dir> <args>, stops the test when it fails; its output, without its line end, in
# git_out
function(git dir)
  execute_process(COMMAND "${GIT}" ${git_c} -C "${dir}" ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out
                  ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "version.build_time: git -C ${dir} ${ARGN}: ${rc} ${out}${err}")
  endif()
  string(REGEX REPLACE "\r?\n$" "" out "${out}")
  set(git_out "${out}" PARENT_SCOPE)
endfunction()

set(clone "${WORK}/clone")
set(build "${WORK}/build")
set(count_file "${WORK}/configures.txt")

## the clone: the tree's HEAD, and what the tree has changed in its tracked files since
git("${TREE}" rev-parse HEAD)
set(tree_head "${git_out}")
git("${WORK}" clone -q --shared --no-checkout "${TREE}" "${clone}")
git("${clone}" checkout -q --detach "${tree_head}")
git("${TREE}" -c core.quotePath=false diff --name-only --no-renames --ignore-submodules HEAD --)
set(changed "")
if(NOT git_out STREQUAL "")
  string(REPLACE "\n" ";" changed "${git_out}")
endif()
foreach(path ${changed})
  if(EXISTS "${TREE}/${path}" AND NOT IS_DIRECTORY "${TREE}/${path}")
    get_filename_component(parent "${clone}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${TREE}/${path}" "${clone}/${path}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "version.build_time: ${TREE}/${path} could not be copied into the clone")
    endif()
  else()
    file(REMOVE "${clone}/${path}")
  endif()
endforeach()
list(LENGTH changed changed_count)
if(changed_count GREATER 0)
  git("${clone}" add -A -- ${changed})
  git("${clone}" commit -q --no-verify --allow-empty -m "the tree's changes since its HEAD")
endif()
message("version.build_time: a clone of ${TREE} at ${tree_head}; tracked files the tree has changed since: ${changed_count}")

# CMake includes this after every project() of the configure: a line per configure
file(WRITE "${WORK}/count.cmake" "file(APPEND \"${count_file}\" \"\${PROJECT_NAME}\\n\")\n")

# configures(<var>): how many lines count.cmake has written
function(configures var)
  set(n 0)
  if(EXISTS "${count_file}")
    file(STRINGS "${count_file}" lines)
    list(LENGTH lines n)
  endif()
  set(${var} ${n} PARENT_SCOPE)
endfunction()

set(failed 0)
macro(fail text)
  message("version.build_time: FAIL ${text}")
  math(EXPR failed "${failed} + 1")
endmacro()

## configured once
set(args -S "${clone}" -B "${build}" -G "${GENERATOR}" -Wno-dev -DNULLDC_DEVTOOLS_DIST=OFF
         "-DCMAKE_PROJECT_INCLUDE=${WORK}/count.cmake")
if(MAKE_PROGRAM)
  list(APPEND args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${args} RESULT_VARIABLE rc OUTPUT_FILE "${WORK}/configure.log"
                ERROR_FILE "${WORK}/configure.log")
if(NOT rc EQUAL 0)
  file(READ "${WORK}/configure.log" text)
  message(FATAL_ERROR "${text}\nversion.build_time: the clone did not configure (${WORK}/configure.log)")
endif()
configures(configured)
if(NOT configured GREATER 0)
  message(FATAL_ERROR "version.build_time: the configure did not include ${WORK}/count.cmake")
endif()

# built(<step>): builds nulldc3_version, and checks the three things of the header against the clone now
function(built step)
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" --target nulldc3_version
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  file(WRITE "${WORK}/build-${step}.log" "${out}${err}")
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "version.build_time: ${step}: the target nulldc3_version did not build: ${out}${err}")
  endif()

  # what version.cmake makes of the clone now
  set(expected_dir "${WORK}/expected-${step}")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${clone}" "-DOUTPUT_DIR=${expected_dir}"
                          -P "${clone}/nulldc3/cmake/version.cmake"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "version.build_time: version.cmake on the clone: exit status ${rc}: ${o}${e}")
  endif()
  file(STRINGS "${expected_dir}/nulldc3-version.txt" expected LIMIT_COUNT 1)
  set(version "")
  if(EXISTS "${build}/nulldc3-version.txt")
    file(STRINGS "${build}/nulldc3-version.txt" version LIMIT_COUNT 1)
  endif()
  git("${clone}" rev-parse HEAD)
  set(hash "${git_out}")
  set(header "")
  if(EXISTS "${build}/generated/nulldc3_version.h")
    file(READ "${build}/generated/nulldc3_version.h" header)
  endif()
  configures(n)
  git("${clone}" status --porcelain --ignored)
  set(status "${git_out}")

  message("version.build_time: ${step}: HEAD ${hash}, the build says ${version}, configured ${n} times")
  if(NOT version STREQUAL expected)
    fail("${step}: <build>/nulldc3-version.txt says \"${version}\", the clone is \"${expected}\"")
  endif()
  string(FIND "${header}" "#define NULLDC_GIT_HASH \"${hash}\"\n" at)
  if(at EQUAL -1)
    fail("${step}: <build>/generated/nulldc3_version.h has no line #define NULLDC_GIT_HASH \"${hash}\"")
  endif()
  if(NOT n EQUAL configured)
    fail("${step}: CMake ran again: ${n} configures, ${configured} before the build")
  endif()
  if(NOT status STREQUAL "")
    fail("${step}: the build wrote into the clone: git status --porcelain --ignored says \"${status}\"")
  endif()
  set(failed ${failed} PARENT_SCOPE)
  set(built_version "${version}" PARENT_SCOPE)
endfunction()

built(configured)
set(first "${built_version}")

## an empty commit, and the next build of the version target
git("${clone}" commit -q --no-verify --allow-empty -m "an empty commit of version.build_time")
built(after_a_commit)
if(built_version STREQUAL first)
  fail("after_a_commit: the version is still \"${first}\"")
endif()

if(failed GREATER 0)
  message(FATAL_ERROR "version.build_time: ${failed} checks failed")
endif()
message("version.build_time: \"${first}\", then after a commit \"${built_version}\", with one configure")
