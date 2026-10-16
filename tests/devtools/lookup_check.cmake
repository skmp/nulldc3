# devtools.lookup (CMakeLists.txt of this directory): nulldc_find_devtools of lookup.cmake on scratch layouts of a
# tree and a checkout beside it, each below <work>/<layout>/:
#
#   cmake -DWORK=<scratch directory> -P lookup_check.cmake
#
# tree/devtools is the submodule (empty when it is not initialised), dreamcast-devtools the checkout beside the tree.
# Every layout is looked up with what the tests project needs (package.json and the directory node_modules/), and
# one with two paths below the place (what tests/debugger/disasm needs).

include("${CMAKE_CURRENT_LIST_DIR}/lookup.cmake")

if(NOT WORK)
  message(FATAL_ERROR "devtools.lookup: no -DWORK=<scratch directory>")
endif()
file(REMOVE_RECURSE "${WORK}")

set(failed 0)

# A UI checkout below dir, with every path of the list (one with a / at its end a directory)
function(make_place dir)
  file(MAKE_DIRECTORY "${dir}")
  foreach(path ${ARGN})
    if(path MATCHES "/$")
      file(MAKE_DIRECTORY "${dir}/${path}")
    else()
      get_filename_component(parent "${dir}/${path}" DIRECTORY)
      file(MAKE_DIRECTORY "${parent}")
      file(WRITE "${dir}/${path}" "")
    endif()
  endforeach()
endfunction()

# expected: submodule, sibling or none
function(expect layout expected)
  set(top "${WORK}/${layout}")
  nulldc_find_devtools(found "${top}/tree" ${ARGN})
  if(expected STREQUAL "submodule")
    set(want "${top}/tree/devtools")
  elseif(expected STREQUAL "sibling")
    set(want "${top}/dreamcast-devtools")
  else()
    set(want "")
  endif()
  if(found STREQUAL want)
    message("devtools.lookup: ${layout} (needs ${ARGN}): '${found}'")
  else()
    message("devtools.lookup: FAIL ${layout} (needs ${ARGN}): took '${found}', expected the ${expected} ('${want}')")
    math(EXPR count "${failed} + 1")
    set(failed ${count} PARENT_SCOPE)
  endif()
endfunction()

set(tests package.json node_modules/)
set(disasm node_modules/.bin/tsx src/app/dsp/dspUtils.ts)

# an uninitialised submodule, a complete checkout beside the tree
file(MAKE_DIRECTORY "${WORK}/empty_submodule/tree/devtools")
make_place("${WORK}/empty_submodule/dreamcast-devtools" ${tests} ${disasm})
expect(empty_submodule sibling ${tests})
expect(empty_submodule sibling ${disasm})

# a submodule where npm ci was not run, a complete checkout beside the tree
make_place("${WORK}/no_node_modules/tree/devtools" package.json src/app/dsp/dspUtils.ts)
make_place("${WORK}/no_node_modules/dreamcast-devtools" ${tests} ${disasm})
expect(no_node_modules sibling ${tests})
expect(no_node_modules sibling ${disasm})

# node_modules that is a file, not a directory
make_place("${WORK}/node_modules_file/tree/devtools" package.json node_modules)
make_place("${WORK}/node_modules_file/dreamcast-devtools" ${tests})
expect(node_modules_file sibling ${tests})

# a complete submodule is taken first
make_place("${WORK}/submodule/tree/devtools" ${tests} ${disasm})
make_place("${WORK}/submodule/dreamcast-devtools" ${tests} ${disasm})
expect(submodule submodule ${tests})
expect(submodule submodule ${disasm})

# no submodule directory at all, a checkout beside the tree
file(MAKE_DIRECTORY "${WORK}/sibling_only/tree")
make_place("${WORK}/sibling_only/dreamcast-devtools" ${tests})
expect(sibling_only sibling ${tests})

# neither
file(MAKE_DIRECTORY "${WORK}/nothing/tree")
expect(nothing none ${tests})

# an uninitialised submodule and nothing beside the tree
file(MAKE_DIRECTORY "${WORK}/empty_only/tree/devtools")
expect(empty_only none ${tests})

# an uninitialised submodule, and beside the tree a checkout where npm ci was not run
file(MAKE_DIRECTORY "${WORK}/both_incomplete/tree/devtools")
make_place("${WORK}/both_incomplete/dreamcast-devtools" package.json)
expect(both_incomplete none ${tests})

# a submodule with one of the two paths, both beside the tree
make_place("${WORK}/half/tree/devtools" node_modules/.bin/tsx)
make_place("${WORK}/half/dreamcast-devtools" ${disasm})
expect(half sibling ${disasm})

file(REMOVE_RECURSE "${WORK}")
if(failed)
  message(FATAL_ERROR "devtools.lookup: ${failed} layouts took the wrong place")
endif()
message("devtools.lookup: every layout took the place it should")
