# version.derive (CMakeLists.txt of this directory): nulldc3/cmake/version.cmake on git repositories made below
# <work>, every case of its rule:
#
#   cmake -DWORK=<scratch directory> -DGIT=<git> -DSCRIPT=<nulldc3/cmake/version.cmake> -P derive.cmake
#
# The repositories (inline identities, no configuration of the user or of the system, git looks no further up than
# WORK; every tag is made here, in the scratch, never in a repository of the tree):
#
#   tagged     v3.0.0-pre (annotated) on its first commit, two commits after it; an untracked and an ignored file
#   notag      one commit, no tag
#   others     v3.0.0-pre, then the tags foo, release-1 and vnext on the two commits after it
#   onlyothers the tags foo and release-1 only
#   form       the tag v3.0.0+meta, whose describe output does not read as v<T>-<N>-g<hash>
#   later      v3.0.0-pre, then the lightweight tag v3.0.1 one commit later, then one commit more
#   worktree   a worktree of tagged at its tag
#   super      a repository with tagged as its submodule sub (a file:// URL, protocol.file.allow=always)
#   shallow    a clone of tagged with --depth 1 through a file:// URL (the tag is not reachable), and full, a clone
#              without depth
#   copy       the base file and no .git; inner, the same inside another repository (outer)
#
# and runs version.cmake on each as the build does (cmake -P, "git" from the PATH), with the PATH without git, and
# with GIT_DIR and GIT_WORK_TREE pointing at another repository (as in a git hook). Each case checks the version,
# the full hash and the dirty flag, the derived forms, the three files (the header's #defines, and no #include or
# #pragma in it), and that <h> is 10 hexadecimal digits and the start of `git rev-parse HEAD`; a case about an
# untracked or an ignored file first checks that `git status --porcelain` shows it (commits stage f alone, so such a
# file never goes into a commit by the way). The describe outputs a repository cannot easily make (N = 5, 70000 and
# the cap, outputs of other forms) go through the script's own functions. A second run that has nothing new
# rewrites none of the files.

if(NOT WORK OR NOT GIT OR NOT SCRIPT)
  message(FATAL_ERROR "version.derive: needs -DWORK=<scratch directory> -DGIT=<git> -DSCRIPT=<version.cmake>")
endif()
get_filename_component(WORK "${WORK}" ABSOLUTE)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# git looks no further up than WORK (the scratch may be inside another checkout), and reads no configuration of
# the user or of the system
set(ENV{GIT_CEILING_DIRECTORIES} "${WORK}")
set(ENV{GIT_CONFIG_NOSYSTEM} 1)
set(ENV{GIT_CONFIG_GLOBAL} "${WORK}/no-gitconfig")
set(ENV{HOME} "${WORK}")
foreach(name GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_PREFIX GIT_CONFIG_COUNT)
  unset(ENV{${name}})
endforeach()
# version.cmake runs "git" from the PATH: the git of this test comes first there
get_filename_component(git_bin "${GIT}" DIRECTORY)
if(CMAKE_HOST_WIN32)
  set(ENV{PATH} "${git_bin};$ENV{PATH}")
else()
  set(ENV{PATH} "${git_bin}:$ENV{PATH}")
endif()

# the script's functions, for the describe outputs no repository here makes (included without SOURCE_DIR it does
# nothing else)
include("${SCRIPT}")

set(git_c -c user.name=nulldc-test -c user.email=test@example.invalid -c commit.gpgsign=false -c tag.gpgsign=false
          -c init.defaultBranch=main -c core.autocrlf=false -c protocol.file.allow=always)

# git(<dir> <args>...): git -C <dir> <args>, stops the test when it fails; its output, without its line end, in
# git_out
function(git dir)
  execute_process(COMMAND "${GIT}" ${git_c} -C "${dir}" ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out
                  ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "version.derive: git -C ${dir} ${ARGN}: ${rc} ${out}${err}")
  endif()
  string(REGEX REPLACE "\r?\n$" "" out "${out}")
  set(git_out "${out}" PARENT_SCOPE)
endfunction()

# repo(<name> <base>): an empty repository <WORK>/<name> with nulldc3/version-base.txt and .gitignore staged
function(repo name base)
  file(MAKE_DIRECTORY "${WORK}/${name}")
  git("${WORK}/${name}" init -q)
  file(WRITE "${WORK}/${name}/nulldc3/version-base.txt" "${base}\n")
  file(WRITE "${WORK}/${name}/.gitignore" "ignored.txt\n")
  git("${WORK}/${name}" add -A)
endfunction()

# commit(<dir> <text>): <text> into the tracked file f, and a commit of f alone (repo() staged the base and
# .gitignore): a file a case leaves untracked stays untracked through the commits after it
function(commit dir text)
  file(WRITE "${dir}/f" "${text}\n")
  git("${dir}" add f)
  git("${dir}" commit -q -m "${text}")
endfunction()

# head(<dir> <var>): git rev-parse HEAD of <dir>
function(head dir var)
  git("${dir}" rev-parse HEAD)
  set(${var} "${git_out}" PARENT_SCOPE)
endfunction()

# file_url(<path> <var>): file:///C:/... for a path with a drive letter, file:///... else, forward slashes
function(file_url path var)
  file(TO_CMAKE_PATH "${path}" p)
  if(p MATCHES "^[A-Za-z]:/")
    set(${var} "file:///${p}" PARENT_SCOPE)
  else()
    set(${var} "file://${p}" PARENT_SCOPE)
  endif()
endfunction()

set(failed 0)
set(cases 0)

# fail(<case> <text>)
macro(fail what text)
  message("version.derive: FAIL ${what}: ${text}")
  math(EXPR failed "${failed} + 1")
endmacro()

# shows(<case> <dir> <line> [IGNORED]): `git status --porcelain` of <dir> with its untracked files (and with
# IGNORED its ignored ones) has the line <line>, so that a case about such a file has the file it is about
function(shows what dir line)
  cmake_parse_arguments(ARG "IGNORED" "" "" ${ARGN})
  set(args status --porcelain --untracked-files=normal)
  if(ARG_IGNORED)
    list(APPEND args --ignored)
  endif()
  git("${dir}" ${args})
  string(REPLACE "\n" ";" lines "${git_out}")
  if(NOT line IN_LIST lines)
    string(REPLACE ";" " " args "${args}")
    fail("${what}" "git ${args} of ${dir} has no line \"${line}\": \"${git_out}\"")
    set(failed ${failed} PARENT_SCOPE)
  endif()
endfunction()

# forms(<case> <version> <numbers> <dotted> <short> <expected numbers X,Y,Z,N>): the derived forms are those of
# X,Y,Z,N
function(forms what numbers dotted short expected)
  string(REPLACE "," "." expected_dotted "${expected}")
  string(REGEX REPLACE "^([0-9]+,[0-9]+,[0-9]+),[0-9]+$" "\\1" expected_short "${expected}")
  string(REPLACE "," "." expected_short "${expected_short}")
  if(NOT numbers STREQUAL expected OR NOT dotted STREQUAL expected_dotted OR NOT short STREQUAL expected_short)
    fail("${what}" "numbers ${numbers}, dotted ${dotted}, short ${short}; expected ${expected}, ${expected_dotted}, ${expected_short}")
    set(failed ${failed} PARENT_SCOPE)
  endif()
endfunction()

# expect(<case> <dir> <version> <numbers X,Y,Z,N> [NOGIT] [DIRTY] [ENV <var=value>...]): version.cmake on <dir>
# gives <version>, in which @H@ stands for the first 10 characters of git rev-parse HEAD of <dir> (asked here);
# NOGIT: no hash, DIRTY: the dirty flag; ENV: the environment of the run (cmake -E env)
function(expect what dir template expected_numbers)
  cmake_parse_arguments(ARG "NOGIT;DIRTY" "" "ENV" ${ARGN})
  math(EXPR n "${cases} + 1")
  set(cases ${n} PARENT_SCOPE)

  set(hash "")
  set(h "")
  if(NOT ARG_NOGIT)
    head("${dir}" hash)
    string(SUBSTRING "${hash}" 0 10 h)
  endif()
  string(REPLACE "@H@" "${h}" expected "${template}")
  set(expected_dirty 0)
  if(ARG_DIRTY)
    set(expected_dirty 1)
  endif()

  set(out "${WORK}/out/${what}")
  file(REMOVE_RECURSE "${out}")
  set(run "${CMAKE_COMMAND}")
  if(ARG_ENV)
    set(run "${CMAKE_COMMAND}" -E env ${ARG_ENV} "${CMAKE_COMMAND}")
  endif()
  execute_process(COMMAND ${run} "-DSOURCE_DIR=${dir}" "-DOUTPUT_DIR=${out}" -P "${SCRIPT}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
  if(NOT rc EQUAL 0)
    fail("${what}" "version.cmake: exit status ${rc}: ${o}${e}")
    set(failed ${failed} PARENT_SCOPE)
    return()
  endif()

  set(problems "")
  # the CMake file
  unset(NULLDC_VERSION)
  include("${out}/nulldc3-version.cmake")
  if(NOT NULLDC_VERSION STREQUAL expected)
    list(APPEND problems "version \"${NULLDC_VERSION}\", expected \"${expected}\"")
  endif()
  if(NOT NULLDC_GIT_HASH STREQUAL hash)
    list(APPEND problems "hash \"${NULLDC_GIT_HASH}\", expected \"${hash}\"")
  endif()
  if(NOT NULLDC_GIT_DIRTY STREQUAL expected_dirty)
    list(APPEND problems "dirty ${NULLDC_GIT_DIRTY}, expected ${expected_dirty}")
  endif()
  # <h>: exactly 10 hexadecimal digits, the start of the commit (never describe's abbreviation)
  if(template MATCHES "@H@")
    set(got_h "")
    if(NULLDC_VERSION MATCHES "[+.]g([0-9A-Za-z]+)(\\.dirty)?$")
      set(got_h "${CMAKE_MATCH_1}")
    endif()
    string(LENGTH "${got_h}" length)
    string(FIND "${hash}" "${got_h}" at)
    if(NOT length EQUAL 10 OR NOT got_h MATCHES "^[0-9a-f]+$" OR NOT at EQUAL 0)
      list(APPEND problems "<h> \"${got_h}\" is not the first 10 hexadecimal digits of ${hash}")
    endif()
  endif()
  # the text file: the version and a line end
  file(READ "${out}/nulldc3-version.txt" text)
  if(NOT text STREQUAL "${expected}\n")
    list(APPEND problems "nulldc3-version.txt is \"${text}\"")
  endif()
  # the header: the same values, plain defines only
  file(READ "${out}/generated/nulldc3_version.h" header)
  foreach(line
      "#define NULLDC_VERSION \"${expected}\""
      "#define NULLDC_VERSION_NUMBERS ${NULLDC_VERSION_NUMBERS}"
      "#define NULLDC_VERSION_DOTTED \"${NULLDC_VERSION_DOTTED}\""
      "#define NULLDC_VERSION_SHORT \"${NULLDC_VERSION_SHORT}\""
      "#define NULLDC_GIT_HASH \"${hash}\""
      "#define NULLDC_GIT_DIRTY ${expected_dirty}")
    string(FIND "${header}" "${line}\n" at)
    if(at EQUAL -1)
      list(APPEND problems "the header has no line ${line}")
    endif()
  endforeach()
  if(header MATCHES "(^|\n)[ \t]*#[ \t]*(include|pragma)")
    list(APPEND problems "the header has an #include or a #pragma")
  endif()

  if(problems)
    string(REPLACE ";" "; " problems "${problems}")
    fail("${what}" "${problems}")
    set(failed ${failed} PARENT_SCOPE)
    return()
  endif()
  forms("${what}" "${NULLDC_VERSION_NUMBERS}" "${NULLDC_VERSION_DOTTED}" "${NULLDC_VERSION_SHORT}" "${expected_numbers}")
  set(failed ${failed} PARENT_SCOPE)
  message("version.derive: ${what}: ${NULLDC_VERSION} (${NULLDC_VERSION_NUMBERS}, ${NULLDC_VERSION_DOTTED}, ${NULLDC_VERSION_SHORT}; dirty ${NULLDC_GIT_DIRTY})")
endfunction()

# derive(<case> <describe output> <version> <numbers> [DIRTY]): the script's own derivation on a describe output,
# in a checkout whose HEAD is 0123456789abcdef0123456789abcdef01234567 (@H@ is 0123456789), base 3.0.0-pre
function(derive what describe template expected_numbers)
  cmake_parse_arguments(ARG "DIRTY" "BASE" "" ${ARGN})
  math(EXPR n "${cases} + 1")
  set(cases ${n} PARENT_SCOPE)
  set(base 3.0.0-pre)
  if(ARG_BASE)
    set(base "${ARG_BASE}")
  endif()
  set(dirty FALSE)
  if(ARG_DIRTY)
    set(dirty TRUE)
  endif()
  string(REPLACE "@H@" "0123456789" expected "${template}")
  nulldc_version_derive(BASE "${base}" CHECKOUT TRUE HASH 0123456789abcdef0123456789abcdef01234567
                        DESCRIBE "${describe}" DIRTY ${dirty})
  if(NOT NULLDC_VERSION STREQUAL expected)
    fail("${what}" "describe \"${describe}\": version \"${NULLDC_VERSION}\", expected \"${expected}\"")
  else()
    forms("${what}" "${NULLDC_VERSION_NUMBERS}" "${NULLDC_VERSION_DOTTED}" "${NULLDC_VERSION_SHORT}" "${expected_numbers}")
    message("version.derive: ${what}: \"${describe}\" -> ${NULLDC_VERSION} (${NULLDC_VERSION_NUMBERS})")
  endif()
  set(failed ${failed} PARENT_SCOPE)
endfunction()


## tagged: on the tag, after it, dirty, untracked and ignored files
repo(tagged 3.0.0-pre)
commit("${WORK}/tagged" "one")
git("${WORK}/tagged" tag -a v3.0.0-pre -m "nullDC 3.0.0-pre")
head("${WORK}/tagged" tagged_1)
expect(on_tag "${WORK}/tagged" "3.0.0-pre" 3,0,0,0)

file(WRITE "${WORK}/tagged/untracked.txt" "untracked\n")
file(WRITE "${WORK}/tagged/ignored.txt" "ignored\n")
shows(on_tag.untracked_and_ignored "${WORK}/tagged" "?? untracked.txt")
shows(on_tag.untracked_and_ignored "${WORK}/tagged" "!! ignored.txt" IGNORED)
expect(on_tag.untracked_and_ignored "${WORK}/tagged" "3.0.0-pre" 3,0,0,0)

file(WRITE "${WORK}/tagged/f" "changed\n")
expect(on_tag.dirty "${WORK}/tagged" "3.0.0-pre+dirty" 3,0,0,0 DIRTY)
git("${WORK}/tagged" add f)
expect(on_tag.dirty_staged "${WORK}/tagged" "3.0.0-pre+dirty" 3,0,0,0 DIRTY)
git("${WORK}/tagged" reset -q --hard)

commit("${WORK}/tagged" "two")
commit("${WORK}/tagged" "three")
head("${WORK}/tagged" tagged_3)
expect(after_two "${WORK}/tagged" "3.0.0-pre+2.g@H@" 3,0,0,2)
file(WRITE "${WORK}/tagged/f" "changed\n")
expect(after_two.dirty "${WORK}/tagged" "3.0.0-pre+2.g@H@.dirty" 3,0,0,2 DIRTY)
file(REMOVE "${WORK}/tagged/f")
expect(after_two.deleted "${WORK}/tagged" "3.0.0-pre+2.g@H@.dirty" 3,0,0,2 DIRTY)
git("${WORK}/tagged" checkout -q -- f)
# untracked.txt and ignored.txt are still there, and still neither tracked nor in a commit
shows(after_two.untracked_and_ignored "${WORK}/tagged" "?? untracked.txt")
shows(after_two.untracked_and_ignored "${WORK}/tagged" "!! ignored.txt" IGNORED)
expect(after_two.untracked_and_ignored "${WORK}/tagged" "3.0.0-pre+2.g@H@" 3,0,0,2)

# the files are written when their text changes, and only then
set(out "${WORK}/out/rewrite")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${WORK}/tagged" "-DOUTPUT_DIR=${out}" -P "${SCRIPT}" OUTPUT_QUIET)
set(names generated/nulldc3_version.h nulldc3-version.txt nulldc3-version.cmake)
foreach(name ${names})
  file(TIMESTAMP "${out}/${name}" before_${name} "%Y%m%d%H%M%S" UTC)
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.5)
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${WORK}/tagged" "-DOUTPUT_DIR=${out}" -P "${SCRIPT}" OUTPUT_QUIET)
math(EXPR cases "${cases} + 1")
foreach(name ${names})
  file(TIMESTAMP "${out}/${name}" after "%Y%m%d%H%M%S" UTC)
  if(NOT after STREQUAL before_${name})
    fail("unchanged" "a second run with nothing new rewrote ${name}")
  endif()
endforeach()
file(WRITE "${WORK}/tagged/f" "changed\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${WORK}/tagged" "-DOUTPUT_DIR=${out}" -P "${SCRIPT}" OUTPUT_QUIET)
file(STRINGS "${out}/nulldc3-version.txt" text)
if(NOT text MATCHES "\\.dirty$")
  fail("unchanged" "a run after a change of a tracked file did not write .dirty: ${text}")
else()
  message("version.derive: unchanged: a second run rewrote none of the three files; a change made it ${text}")
endif()
git("${WORK}/tagged" checkout -q -- f)

## notag: no tag reachable
repo(notag 3.0.0-pre)
commit("${WORK}/notag" "one")
expect(no_tag "${WORK}/notag" "3.0.0-pre+g@H@" 3,0,0,0)
file(WRITE "${WORK}/notag/f" "changed\n")
expect(no_tag.dirty "${WORK}/notag" "3.0.0-pre+g@H@.dirty" 3,0,0,0 DIRTY)
git("${WORK}/notag" checkout -q -- f)

## others: tags that are not v<digit>... are no version tags, also when they are nearer
repo(others 3.0.0-pre)
commit("${WORK}/others" "one")
git("${WORK}/others" tag -a v3.0.0-pre -m "nullDC 3.0.0-pre")
commit("${WORK}/others" "two")
git("${WORK}/others" tag foo)
commit("${WORK}/others" "three")
git("${WORK}/others" tag -a release-1 -m "release 1")
git("${WORK}/others" tag vnext)
expect(other_tags "${WORK}/others" "3.0.0-pre+2.g@H@" 3,0,0,2)

repo(onlyothers 3.0.0-pre)
commit("${WORK}/onlyothers" "one")
git("${WORK}/onlyothers" tag foo)
git("${WORK}/onlyothers" tag release-1)
expect(only_other_tags "${WORK}/onlyothers" "3.0.0-pre+g@H@" 3,0,0,0)

## form: a v tag whose describe output does not read as v<T>-<N>-g<hash> is no tag
repo(form 3.0.0-pre)
commit("${WORK}/form" "one")
git("${WORK}/form" tag v3.0.0+meta)
git("${WORK}/form" describe --tags --long --match "v[0-9]*")
set(form_describe "${git_out}")
expect(tag_of_another_form "${WORK}/form" "3.0.0-pre+g@H@" 3,0,0,0)
message("version.derive: (tag_of_another_form: describe says ${form_describe})")

## later: a tag v3.0.1 over the base 3.0.0-pre: the version and every number are the tag's
repo(later 3.0.0-pre)
commit("${WORK}/later" "one")
git("${WORK}/later" tag -a v3.0.0-pre -m "nullDC 3.0.0-pre")
commit("${WORK}/later" "two")
git("${WORK}/later" tag v3.0.1)
expect(later_tag "${WORK}/later" "3.0.1" 3,0,1,0)
commit("${WORK}/later" "three")
expect(later_tag.after_one "${WORK}/later" "3.0.1+1.g@H@" 3,0,1,1)

## worktree: a worktree of tagged at its tag has its own HEAD (.git is a file there)
git("${WORK}/tagged" worktree add -q --detach "${WORK}/worktree" v3.0.0-pre)
if(NOT EXISTS "${WORK}/worktree/.git" OR IS_DIRECTORY "${WORK}/worktree/.git")
  fail("worktree" "${WORK}/worktree/.git is not a file")
endif()
expect(worktree "${WORK}/worktree" "3.0.0-pre" 3,0,0,0)
file(WRITE "${WORK}/worktree/f" "changed\n")
expect(worktree.dirty "${WORK}/worktree" "3.0.0-pre+dirty" 3,0,0,0 DIRTY)
git("${WORK}/worktree" checkout -q -- f)
# the repository itself is still two after the tag
expect(worktree.main "${WORK}/tagged" "3.0.0-pre+2.g@H@" 3,0,0,2)

## super: tagged as the submodule sub of a repository
file_url("${WORK}/tagged" tagged_url)
repo(super 3.0.0-pre)
commit("${WORK}/super" "one")
git("${WORK}/super" submodule add -q "${tagged_url}" sub)
git("${WORK}/super" commit -q -m "the submodule")
expect(submodule "${WORK}/super/sub" "3.0.0-pre+2.g@H@" 3,0,0,2)
expect(superproject "${WORK}/super" "3.0.0-pre+g@H@" 3,0,0,0)
# a file of the submodule's own that is untracked there: the superproject's status with untracked files says that
# sub has changed (" M sub"), the one without them says nothing
file(WRITE "${WORK}/super/sub/untracked-in-sub.txt" "untracked\n")
shows(superproject.sub_untracked "${WORK}/super/sub" "?? untracked-in-sub.txt")
shows(superproject.sub_untracked "${WORK}/super" " M sub")
expect(superproject.sub_untracked "${WORK}/super" "3.0.0-pre+g@H@" 3,0,0,0)
file(REMOVE "${WORK}/super/sub/untracked-in-sub.txt")
git("${WORK}/super/sub" checkout -q "${tagged_1}")
expect(superproject.sub_moved "${WORK}/super" "3.0.0-pre+g@H@.dirty" 3,0,0,0 DIRTY)
expect(submodule.moved "${WORK}/super/sub" "3.0.0-pre" 3,0,0,0)
git("${WORK}/super/sub" checkout -q "${tagged_3}")
file(WRITE "${WORK}/super/sub/f" "changed\n")
expect(superproject.sub_changed "${WORK}/super" "3.0.0-pre+g@H@.dirty" 3,0,0,0 DIRTY)
git("${WORK}/super/sub" checkout -q -- f)

## shallow: a clone of depth 1 through a file:// URL reaches no tag; <h> is the full clone's
execute_process(COMMAND "${GIT}" ${git_c} clone -q --depth 1 "${tagged_url}" "${WORK}/shallow" RESULT_VARIABLE rc
                ERROR_VARIABLE err)
execute_process(COMMAND "${GIT}" ${git_c} clone -q "${tagged_url}" "${WORK}/full" RESULT_VARIABLE rc2 ERROR_VARIABLE err2)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0)
  message(FATAL_ERROR "version.derive: the clones of ${tagged_url}: ${err}${err2}")
endif()
git("${WORK}/shallow" rev-parse --is-shallow-repository)
if(NOT git_out STREQUAL "true")
  fail("shallow" "the clone of depth 1 is not shallow (${git_out})")
endif()
expect(full_clone "${WORK}/full" "3.0.0-pre+2.g@H@" 3,0,0,2)
string(SUBSTRING "${tagged_3}" 0 10 h3)
expect(shallow_clone "${WORK}/shallow" "3.0.0-pre+g${h3}" 3,0,0,0)

## copy: no .git, and a copy inside another repository: no version from git
file(WRITE "${WORK}/copy/nulldc3/version-base.txt" "3.0.0-pre\n")
file(WRITE "${WORK}/copy/f" "a copy\n")
expect(copy_without_git "${WORK}/copy" "3.0.0-pre+nogit" 3,0,0,0 NOGIT)
repo(outer 9.9.9)
file(WRITE "${WORK}/outer/inner/nulldc3/version-base.txt" "3.0.0-pre\n")
git("${WORK}/outer" add inner)
commit("${WORK}/outer" "a tree inside")
git("${WORK}/outer" tag v9.9.9)
expect(copy_inside_another_repository "${WORK}/outer/inner" "3.0.0-pre+nogit" 3,0,0,0 NOGIT)

## no git on the PATH
file(MAKE_DIRECTORY "${WORK}/no-git-bin")
expect(no_git "${WORK}/tagged" "3.0.0-pre+nogit" 3,0,0,0 NOGIT ENV "PATH=${WORK}/no-git-bin")

## GIT_DIR and GIT_WORK_TREE of another repository in the environment, as a git hook has them: the version is still
## the one of the tree it is asked about
expect(git_dir_of_another "${WORK}/tagged" "3.0.0-pre+2.g@H@" 3,0,0,2
       ENV "GIT_DIR=${WORK}/notag/.git" "GIT_WORK_TREE=${WORK}/notag" "GIT_INDEX_FILE=${WORK}/notag/.git/index")

## the base: missing or not a version stops the step
foreach(bad missing "3.0" "v3.0.0" "3.0.0+meta")
  math(EXPR cases "${cases} + 1")
  file(REMOVE_RECURSE "${WORK}/badbase")
  file(MAKE_DIRECTORY "${WORK}/badbase")
  if(NOT bad STREQUAL "missing")
    file(WRITE "${WORK}/badbase/nulldc3/version-base.txt" "${bad}\n")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${WORK}/badbase" "-DOUTPUT_DIR=${WORK}/out/badbase" -P "${SCRIPT}"
                  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
  if(rc EQUAL 0)
    fail("base ${bad}" "version.cmake took it")
  else()
    message("version.derive: base ${bad}: refused")
  endif()
endforeach()

## describe outputs through the script's own functions: N = 0, 5 and 70000 (capped at 65535), the cap's edge, and
## outputs of other forms, which are no tag
derive(n_0 "v3.0.0-pre-0-g1234567" "3.0.0-pre" 3,0,0,0)
derive(n_0.dirty "v3.0.0-pre-0-g1234567" "3.0.0-pre+dirty" 3,0,0,0 DIRTY)
derive(n_5 "v3.0.0-pre-5-g1234567" "3.0.0-pre+5.g@H@" 3,0,0,5)
derive(n_5.dirty "v3.0.0-pre-5-g1234567" "3.0.0-pre+5.g@H@.dirty" 3,0,0,5 DIRTY)
derive(n_65535 "v3.0.0-pre-65535-g1234567" "3.0.0-pre+65535.g@H@" 3,0,0,65535)
derive(n_65536 "v3.0.0-pre-65536-g1234567" "3.0.0-pre+65536.g@H@" 3,0,0,65535)
derive(n_70000 "v3.0.0-pre-70000-g1234567" "3.0.0-pre+70000.g@H@" 3,0,0,65535)
derive(later_tag "v3.0.1-5-g1234567" "3.0.1+5.g@H@" 3,0,1,5)
derive(two_part_tag "v4.5-rc-1-g1234567" "4.5-rc+1.g@H@" 4,5,0,1)
derive(base_numbers "" "3.1.2-pre+g@H@" 3,1,2,0 BASE 3.1.2-pre)
foreach(other
    "v3.0.0-pre-2"
    "3.0.0-pre-2-g1234567"
    "v3.0.0+meta-0-g1234567"
    "v3.0.0-pre-x-g1234567"
    "v3.0.0-pre-2-g123"
    "v3.0.0-pre-2-g123456G"
    "v3.0.0-pre-2-1234567"
    "vfoo-0-g1234567"
    "foo-1-g1234567"
    "release-1-0-g1234567"
    "v3.0.0-pre-2-g1234567 trailing"
    "")
  derive("another_form" "${other}" "3.0.0-pre+g@H@" 3,0,0,0)
endforeach()

## no checkout: a hash that is no full hash is none
math(EXPR cases "${cases} + 1")
nulldc_version_derive(BASE 3.0.0-pre CHECKOUT TRUE HASH 1234567 DESCRIBE "v3.0.0-pre-0-g1234567" DIRTY FALSE)
if(NOT NULLDC_VERSION STREQUAL "3.0.0-pre+nogit" OR NOT NULLDC_GIT_HASH STREQUAL "")
  fail("short_hash" "${NULLDC_VERSION} [${NULLDC_GIT_HASH}]")
endif()

if(failed GREATER 0)
  message(FATAL_ERROR "version.derive: ${failed} of ${cases} cases failed")
endif()
message("version.derive: ${cases} cases passed")
