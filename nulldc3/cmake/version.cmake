# The version of nullDC, made at every build:
#
#   cmake -DSOURCE_DIR=<the top of the tree> -DOUTPUT_DIR=<the build directory> [-DGIT=<git>] -P version.cmake
#
# It writes three files, each only when its text changes, so that only what reads one is built again:
#
#   <OUTPUT_DIR>/generated/nulldc3_version.h   NULLDC_VERSION and the forms below as plain #defines (no #include,
#                                              no #pragma: the Windows resource compiler reads it too); the one
#                                              C++ file that includes it is libswirl/version.cpp
#   <OUTPUT_DIR>/nulldc3-version.txt           the version, one line
#   <OUTPUT_DIR>/nulldc3-version.cmake         the same values as CMake variables, for include()
#
# The version, from git and the base, the one line of nulldc3/version-base.txt (the UI's scripts/version.ts makes
# its own by the same rule):
#
#   on the tag v<T>, clean                 <T>                     3.0.0-pre
#   N commits after it, clean              <T>+N.g<h>              3.0.0-pre+2.g1a2b3c4d5e
#   on the tag, dirty                      <T>+dirty               3.0.0-pre+dirty
#   N after it, dirty                      <T>+N.g<h>.dirty        3.0.0-pre+2.g1a2b3c4d5e.dirty
#   a checkout with no tag reachable       <base>+g<h>[.dirty]     3.0.0-pre+g1a2b3c4d5e
#   no git, or not a checkout              <base>+nogit            3.0.0-pre+nogit
#
# <h> is the first 10 characters of `git rev-parse HEAD`, always 10 (never describe's abbreviation, which is only a
# minimum: a full and a shallow clone could disagree). The tag is found only by `git describe --tags --long --match
# v[0-9]*`, whose output is read from the right as v<T>-<N>-g<hash>; an output that does not read so is no tag.
# Dirty is a change of a tracked file (`git status --porcelain --untracked-files=no` prints something; a submodule
# at another commit than its gitlink counts); untracked and ignored files never count. A checkout is a directory at
# the top of a work tree (`git rev-parse --show-prefix` prints an empty line): a clone, a worktree, a submodule; a
# copy of the tree inside another repository is none.
#
# The derived forms take X.Y.Z from the start of the version itself (the tag's when one is reachable, else the
# base's; a part that is missing is 0), and N, the commits since the tag (0 on it and without one), capped at 65535:
#
#   NULLDC_VERSION_NUMBERS   X,Y,Z,N    Windows FILEVERSION / PRODUCTVERSION
#   NULLDC_VERSION_DOTTED    X.Y.Z.N    macOS CFBundleVersion
#   NULLDC_VERSION_SHORT     X.Y.Z      macOS CFBundleShortVersionString
#   NULLDC_GIT_HASH          `git rev-parse HEAD`, "" when there is no checkout
#   NULLDC_GIT_DIRTY         1 when dirty, else 0
#
# git is run with an argument list, never through a shell, with -C <SOURCE_DIR> and GIT_OPTIONAL_LOCKS=0 (a build
# never holds index.lock against a commit made at the same time), and without the variables that would point it at
# another repository (GIT_DIR and its kin, which a git hook has set). GIT is "git", found on the PATH, by default.
#
# Included (without SOURCE_DIR) it defines its functions and does nothing else: tests/version uses them.

cmake_minimum_required(VERSION 3.10)

# nulldc_version_parse_describe(<describe output> <tag variable> <count variable>): <tag> is <T> of
# v<T>-<N>-g<hash> read from the right (<T> starts with a digit and may hold hyphens: 3.0.0-pre), <count> is <N>;
# <tag> is empty when the output does not read so
function(nulldc_version_parse_describe describe tag_var count_var)
  string(STRIP "${describe}" text)
  set(tag "")
  set(count 0)
  if(text MATCHES "^v([0-9][0-9A-Za-z.-]*)-([0-9]+)-g([0-9a-f]+)$")
    set(t "${CMAKE_MATCH_1}")
    set(n "${CMAKE_MATCH_2}")
    string(LENGTH "${CMAKE_MATCH_3}" digits)
    if(NOT digits LESS 4 AND NOT digits GREATER 64)
      set(tag "${t}")
      set(count "${n}")
    endif()
  endif()
  set(${tag_var} "${tag}" PARENT_SCOPE)
  set(${count_var} "${count}" PARENT_SCOPE)
endfunction()

# nulldc_version_derive(BASE <base> CHECKOUT <bool> HASH <full hash> DESCRIBE <describe output, empty when it
# failed> DIRTY <bool>): sets NULLDC_VERSION, NULLDC_VERSION_NUMBERS, NULLDC_VERSION_DOTTED, NULLDC_VERSION_SHORT,
# NULLDC_GIT_HASH and NULLDC_GIT_DIRTY in the caller's scope
function(nulldc_version_derive)
  cmake_parse_arguments(ARG "" "BASE;CHECKOUT;HASH;DESCRIBE;DIRTY" "" ${ARGN})

  set(checkout FALSE)
  if(ARG_CHECKOUT AND (ARG_HASH MATCHES "^[0-9a-f]+$"))
    string(LENGTH "${ARG_HASH}" length)
    if(length EQUAL 40 OR length EQUAL 64)
      set(checkout TRUE)
    endif()
  endif()

  set(count 0)
  set(hash "")
  set(dirty 0)
  if(NOT checkout)
    set(version "${ARG_BASE}+nogit")
  else()
    set(hash "${ARG_HASH}")
    string(SUBSTRING "${hash}" 0 10 h)
    if(ARG_DIRTY)
      set(dirty 1)
    endif()
    nulldc_version_parse_describe("${ARG_DESCRIBE}" tag count)
    if(tag STREQUAL "")
      set(version "${ARG_BASE}+g${h}")
      if(dirty)
        string(APPEND version ".dirty")
      endif()
    elseif(count EQUAL 0)
      set(version "${tag}")
      if(dirty)
        string(APPEND version "+dirty")
      endif()
    else()
      set(version "${tag}+${count}.g${h}")
      if(dirty)
        string(APPEND version ".dirty")
      endif()
    endif()
  endif()

  if(NOT version MATCHES "^[A-Za-z0-9_.+~:/ -]+$")
    message(FATAL_ERROR "version.cmake: the version \"${version}\" has a character outside [A-Za-z0-9_.+~:/ -]")
  endif()

  # X.Y.Z from the start of the version itself, a part that is missing 0, without leading zeros
  if(NOT version MATCHES "^([0-9]+)(\\.([0-9]+))?(\\.([0-9]+))?")
    message(FATAL_ERROR "version.cmake: the version \"${version}\" does not start with a number")
  endif()
  set(x "${CMAKE_MATCH_1}")
  set(y "${CMAKE_MATCH_3}")
  set(z "${CMAKE_MATCH_5}")
  foreach(part x y z)
    if("${${part}}" STREQUAL "")
      set(${part} 0)
    endif()
    string(REGEX REPLACE "^0+([0-9])" "\\1" ${part} "${${part}}")
  endforeach()
  if(count GREATER 65535)
    set(count 65535)
  endif()

  set(NULLDC_VERSION "${version}" PARENT_SCOPE)
  set(NULLDC_VERSION_NUMBERS "${x},${y},${z},${count}" PARENT_SCOPE)
  set(NULLDC_VERSION_DOTTED "${x}.${y}.${z}.${count}" PARENT_SCOPE)
  set(NULLDC_VERSION_SHORT "${x}.${y}.${z}" PARENT_SCOPE)
  set(NULLDC_GIT_HASH "${hash}" PARENT_SCOPE)
  set(NULLDC_GIT_DIRTY "${dirty}" PARENT_SCOPE)
endfunction()

# nulldc_version_git(<git> <root>): what git says of <root>, in the caller's scope: NULLDC_GIT_CHECKOUT (TRUE or
# FALSE), NULLDC_GIT_FULL_HASH, NULLDC_GIT_DESCRIBE (empty when describe failed) and NULLDC_GIT_TREE_DIRTY. Every
# failure is one of the cases of the version: no git, no checkout, no tag; a tree whose state git cannot say is
# not called clean
function(nulldc_version_git git root)
  set(ENV{GIT_OPTIONAL_LOCKS} 0)
  foreach(name GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_PREFIX)
    unset(ENV{${name}})
  endforeach()

  set(checkout FALSE)
  set(hash "")
  set(describe "")
  set(dirty FALSE)

  # no git, no repository, or a directory below the top of one
  execute_process(COMMAND "${git}" -C "${root}" rev-parse --show-prefix
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_QUIET)
  string(REGEX REPLACE "\r?\n$" "" prefix "${out}")
  if(rc EQUAL 0 AND prefix STREQUAL "")
    # a repository without a commit names none
    execute_process(COMMAND "${git}" -C "${root}" rev-parse HEAD
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_QUIET)
    string(STRIP "${out}" out)
    if(rc EQUAL 0 AND out MATCHES "^[0-9a-f]+$")
      set(checkout TRUE)
      set(hash "${out}")

      execute_process(COMMAND "${git}" -C "${root}" describe --tags --long --match "v[0-9]*"
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_QUIET)
      if(rc EQUAL 0)
        string(STRIP "${out}" describe)
      endif()

      execute_process(COMMAND "${git}" -C "${root}" status --porcelain --untracked-files=no
                      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_QUIET)
      string(STRIP "${out}" out)
      if(NOT rc EQUAL 0 OR NOT out STREQUAL "")
        set(dirty TRUE)
      endif()
    endif()
  endif()

  set(NULLDC_GIT_CHECKOUT ${checkout} PARENT_SCOPE)
  set(NULLDC_GIT_FULL_HASH "${hash}" PARENT_SCOPE)
  set(NULLDC_GIT_DESCRIBE "${describe}" PARENT_SCOPE)
  set(NULLDC_GIT_TREE_DIRTY ${dirty} PARENT_SCOPE)
endfunction()

# nulldc_version_read_base(<file> <variable>): the base, the first line of <file>: X.Y.Z, then what a SemVer
# version may have before build metadata (3.0.0-pre)
function(nulldc_version_read_base file var)
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "version.cmake: there is no ${file}, the base of the version")
  endif()
  file(STRINGS "${file}" lines LIMIT_COUNT 1)
  string(STRIP "${lines}" base)
  if(NOT base MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+[0-9A-Za-z.-]*$")
    message(FATAL_ERROR "version.cmake: ${file} says \"${base}\", which is no version X.Y.Z[-pre-release]")
  endif()
  set(${var} "${base}" PARENT_SCOPE)
endfunction()

# nulldc_version_update(<file> <text>): writes <file> when its text is another; NULLDC_VERSION_WRITTEN in the
# caller's scope is TRUE when it did
function(nulldc_version_update file text)
  if(EXISTS "${file}")
    file(READ "${file}" old)
    if(old STREQUAL text)
      return()
    endif()
  endif()
  get_filename_component(dir "${file}" DIRECTORY)
  file(MAKE_DIRECTORY "${dir}")
  file(WRITE "${file}" "${text}")
  set(NULLDC_VERSION_WRITTEN TRUE PARENT_SCOPE)
endfunction()

if(NOT DEFINED SOURCE_DIR)
  return()
endif()
if(NOT SOURCE_DIR OR NOT OUTPUT_DIR)
  message(FATAL_ERROR "version.cmake: needs -DSOURCE_DIR=<the top of the tree> -DOUTPUT_DIR=<the build directory>")
endif()
if(NOT GIT)
  set(GIT git)
endif()

nulldc_version_read_base("${SOURCE_DIR}/nulldc3/version-base.txt" base)
nulldc_version_git("${GIT}" "${SOURCE_DIR}")
nulldc_version_derive(BASE "${base}" CHECKOUT ${NULLDC_GIT_CHECKOUT} HASH "${NULLDC_GIT_FULL_HASH}"
                      DESCRIBE "${NULLDC_GIT_DESCRIBE}" DIRTY ${NULLDC_GIT_TREE_DIRTY})

set(NULLDC_VERSION_WRITTEN FALSE)
nulldc_version_update("${OUTPUT_DIR}/generated/nulldc3_version.h"
"/* nulldc3_version.h, written by nulldc3/cmake/version.cmake at every build: the version of nullDC. Plain defines
   only, the Windows resource compiler reads it too; libswirl/version.cpp is the one C++ file that includes it. */
#define NULLDC_VERSION \"${NULLDC_VERSION}\"
#define NULLDC_VERSION_NUMBERS ${NULLDC_VERSION_NUMBERS}
#define NULLDC_VERSION_DOTTED \"${NULLDC_VERSION_DOTTED}\"
#define NULLDC_VERSION_SHORT \"${NULLDC_VERSION_SHORT}\"
#define NULLDC_GIT_HASH \"${NULLDC_GIT_HASH}\"
#define NULLDC_GIT_DIRTY ${NULLDC_GIT_DIRTY}
")
nulldc_version_update("${OUTPUT_DIR}/nulldc3-version.txt" "${NULLDC_VERSION}\n")
nulldc_version_update("${OUTPUT_DIR}/nulldc3-version.cmake"
"# nulldc3-version.cmake, written by nulldc3/cmake/version.cmake at every build: the version of nullDC
set(NULLDC_VERSION \"${NULLDC_VERSION}\")
set(NULLDC_VERSION_NUMBERS \"${NULLDC_VERSION_NUMBERS}\")
set(NULLDC_VERSION_DOTTED \"${NULLDC_VERSION_DOTTED}\")
set(NULLDC_VERSION_SHORT \"${NULLDC_VERSION_SHORT}\")
set(NULLDC_GIT_HASH \"${NULLDC_GIT_HASH}\")
set(NULLDC_GIT_DIRTY ${NULLDC_GIT_DIRTY})
")
if(NULLDC_VERSION_WRITTEN)
  message(STATUS "nullDC ${NULLDC_VERSION}")
endif()
