# Where the tests look for the UI (dreamcast-devtools) when they are not given one, the one rule for every place
# that looks: tests/CMakeLists.txt (NULLDC_DEVTOOLS_DIR) and the projects of tests/debugger/{disasm,net,regs}
# configured on their own. The emulator's embed step has its own look-up, dbg_webui.cmake, which looks for a built
# bundle; devtools.lookup tests this one (lookup_check.cmake).
#
#   nulldc_find_devtools(<out_var> <tree> <needed>...)
#
# <out_var> is the first of the submodule <tree>/devtools and the checkout <tree>/../dreamcast-devtools beside the
# tree that has every <needed> path (relative to it; one with a / at its end has to be a directory), as an
# absolute path, or empty when neither has them all. An uninitialised submodule is an empty directory, and a
# checkout in which npm ci was not run has no node_modules: neither is taken.

function(nulldc_find_devtools out_var tree)
  if(NOT ARGN)
    message(FATAL_ERROR "nulldc_find_devtools: no needed path: an empty directory would be taken")
  endif()
  set(found "")
  foreach(candidate "${tree}/devtools" "${tree}/../dreamcast-devtools")
    set(complete TRUE)
    foreach(needed ${ARGN})
      if(needed MATCHES "/$")
        if(NOT IS_DIRECTORY "${candidate}/${needed}")
          set(complete FALSE)
        endif()
      elseif(NOT EXISTS "${candidate}/${needed}")
        set(complete FALSE)
      endif()
    endforeach()
    if(complete)
      get_filename_component(found "${candidate}" ABSOLUTE)
      break()
    endif()
  endforeach()
  set(${out_var} "${found}" PARENT_SCOPE)
endfunction()
