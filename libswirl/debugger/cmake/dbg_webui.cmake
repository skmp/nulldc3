# Configure-time half of the devtools UI embed (dbg_webui_embed.cmake is the build-time half).
#
#   dbg_webui_data(<tree> <out_var>)
#
# <tree> is the root of the emulator tree. <out_var> receives the path of dbg_webui_data.cpp, which the
# build writes into CMAKE_CURRENT_BINARY_DIR: the UI bundle chosen by NULLDC_DEVTOOLS_DIST, or an empty table.
# DBG_WEBUI_BUNDLE_DIR is set to the directory that gets embedded, empty when none is.
#
# A bundle is a dist-native directory with index.html and bundle-info.json
# ({"version", "protocol", "gitRev", "files"}). One built for another protocol version than
# dbg::kProtocolVersion would load and then show nothing, so it stops the configure step instead.

# STRING, not PATH: a PATH entry given as -DNULLDC_DEVTOOLS_DIST=OFF would be turned into <cwd>/OFF.
set(NULLDC_DEVTOOLS_DIST "" CACHE STRING
  "Devtools UI bundle (dist-native directory) to embed in the debugger. Empty: use <tree>/devtools/dist-native or <tree>/../dreamcast-devtools/dist-native when present. OFF: embed nothing.")

set(DBG_WEBUI_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(dbg_webui_data tree out_var)
  set(script "${DBG_WEBUI_CMAKE_DIR}/dbg_webui_embed.cmake")
  set(header "${DBG_WEBUI_CMAKE_DIR}/../dbg_webui.h")
  set(out "${CMAKE_CURRENT_BINARY_DIR}/dbg_webui_data.cpp")

  file(STRINGS "${header}" protocol REGEX "kProtocolVersion[ \t]*=[ \t]*[0-9]+")
  string(REGEX REPLACE "^.*kProtocolVersion[ \t]*=[ \t]*([0-9]+).*$" "\\1" protocol "${protocol}")
  if(NOT protocol MATCHES "^[0-9]+$")
    message(FATAL_ERROR "nullDC devtools UI: cannot read kProtocolVersion from ${header}")
  endif()

  set(dir "")
  set(reason "")
  set(explicit OFF)
  if("${NULLDC_DEVTOOLS_DIST}" STREQUAL "")
    foreach(candidate "${tree}/devtools/dist-native" "${tree}/../dreamcast-devtools/dist-native")
      if(dir STREQUAL "" AND EXISTS "${candidate}/index.html")
        get_filename_component(dir "${candidate}" ABSOLUTE)
      endif()
    endforeach()
    if(dir STREQUAL "")
      set(reason "no bundle found, set NULLDC_DEVTOOLS_DIST to a dist-native directory")
    elseif(NOT EXISTS "${dir}/bundle-info.json")
      set(reason "${dir} has no bundle-info.json")
      set(dir "")
    endif()
  elseif(NOT NULLDC_DEVTOOLS_DIST)
    set(reason "NULLDC_DEVTOOLS_DIST=${NULLDC_DEVTOOLS_DIST}")
  else()
    set(explicit ON)
    get_filename_component(dir "${NULLDC_DEVTOOLS_DIST}" ABSOLUTE BASE_DIR "${tree}")
    if(NOT EXISTS "${dir}/index.html" OR NOT EXISTS "${dir}/bundle-info.json")
      message(FATAL_ERROR "nullDC devtools UI: NULLDC_DEVTOOLS_DIST=${NULLDC_DEVTOOLS_DIST} is not a bundle, ${dir} needs index.html and bundle-info.json")
    endif()
  endif()

  if(NOT dir STREQUAL "" AND CMAKE_VERSION VERSION_LESS 3.19)
    if(explicit)
      message(FATAL_ERROR "nullDC devtools UI: embedding needs CMake 3.19 or newer, this is ${CMAKE_VERSION}")
    endif()
    set(reason "embedding needs CMake 3.19 or newer, this is ${CMAKE_VERSION}")
    set(dir "")
  endif()

  set(depends "${script}")
  if(dir STREQUAL "")
    message(STATUS "nullDC devtools UI: not embedded (${reason})")
  else()
    file(READ "${dir}/bundle-info.json" info)
    string(JSON bundle_version ERROR_VARIABLE error1 GET "${info}" version)
    string(JSON bundle_protocol ERROR_VARIABLE error2 GET "${info}" protocol)
    if(NOT error1 STREQUAL "NOTFOUND" OR NOT error2 STREQUAL "NOTFOUND")
      message(FATAL_ERROR "nullDC devtools UI: ${dir}/bundle-info.json does not give \"version\" and \"protocol\"")
    endif()
    if(NOT "${bundle_protocol}" STREQUAL "${protocol}")
      message(FATAL_ERROR "nullDC devtools UI: ${dir} is built for protocol ${bundle_protocol}, the debugger speaks protocol ${protocol}. Rebuild the UI, or configure with -DNULLDC_DEVTOOLS_DIST=OFF to build without it.")
    endif()
    message(STATUS "nullDC devtools UI: embedding ${dir} (${bundle_version}, protocol ${bundle_protocol})")
    list(APPEND depends "${dir}/index.html" "${dir}/bundle-info.json")
    # The checks above have to run again when the bundle is rebuilt.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dir}/bundle-info.json")
  endif()

  # Generators that do not track command lines (Makefiles) learn about another bundle through this file.
  set(stamp "${out}.args")
  set(args "${dir}\n${protocol}\n")
  set(previous "")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" previous)
  endif()
  if(NOT "${previous}" STREQUAL "${args}")
    file(WRITE "${stamp}" "${args}")
  endif()

  add_custom_command(
    OUTPUT "${out}"
    COMMAND "${CMAKE_COMMAND}" "-DDIST=${dir}" "-DOUT=${out}" "-DPROTOCOL=${protocol}" -P "${script}"
    DEPENDS ${depends} "${stamp}"
    COMMENT "Embedding the nullDC devtools UI"
    VERBATIM)
  set(${out_var} "${out}" PARENT_SCOPE)
  set(DBG_WEBUI_BUNDLE_DIR "${dir}" PARENT_SCOPE)
endfunction()
