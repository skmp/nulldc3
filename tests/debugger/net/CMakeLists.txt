# Tests of the debugger transport (libswirl/debugger/dbg_net, dbg_webui and the UI embed step), built
# outside the emulator:
#
#   cmake -S tests/debugger/net -B <build dir> [-DDBG_NET_SANITIZE=ON] [-DNULLDC_DEVTOOLS_DIST=<bundle>|OFF]
#   cmake --build <build dir> && ctest --test-dir <build dir>
#
# The bundle embedded by default is the small one in bundle/; NULLDC_DEVTOOLS_DIST works as in the emulator build.

cmake_minimum_required(VERSION 3.19)
project(dbg_net_tests C CXX)

get_filename_component(tree "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(d_core "${tree}/libswirl")
set(d_debugger "${d_core}/debugger")

# The emulator builds as C++20, with -fpermissive on GCC and /permissive- on MSVC; the debugger sources
# must not need the former, so the tests build without it and with warnings on.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(DBG_NET_SANITIZE "Build the tests with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
set(DBG_NET_SANITIZERS "address,undefined" CACHE STRING "Sanitizers DBG_NET_SANITIZE turns on")
if(DBG_NET_SANITIZE)
  add_compile_options(-fsanitize=${DBG_NET_SANITIZERS} -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${DBG_NET_SANITIZERS})
endif()

find_package(Threads REQUIRED)
include_directories("${d_core}")
set(platform_libs Threads::Threads)
if(WIN32)
  list(APPEND platform_libs ws2_32)
endif()

set(warnings "")
if(NOT MSVC)
  set(warnings -Wall -Wextra -Wshadow)
endif()

# The transport alone links against nothing but SHA-1.
add_library(dbg_net STATIC "${d_debugger}/dbg_net.cpp" "${d_core}/deps/crypto/sha1.cpp")
target_compile_options(dbg_net PRIVATE ${warnings})
target_link_libraries(dbg_net PUBLIC ${platform_libs})

# The UI: dbg_webui.cpp registers itself with dbg_net from a static initialiser, so its object has to be
# linked as such; out of a static library the linker would drop it.
set(NULLDC_DEVTOOLS_DIST "${CMAKE_CURRENT_SOURCE_DIR}/bundle" CACHE STRING "Devtools UI bundle to embed, or OFF")
include("${d_debugger}/cmake/dbg_webui.cmake")
dbg_webui_data("${tree}" webui_data)
set(zlib "${d_core}/deps/zlib")
add_library(dbg_webui OBJECT
  "${d_debugger}/dbg_webui.cpp" "${webui_data}"
  "${zlib}/inflate.c" "${zlib}/inftrees.c" "${zlib}/inffast.c" "${zlib}/zutil.c" "${zlib}/adler32.c" "${zlib}/crc32.c")
set_source_files_properties("${d_debugger}/dbg_webui.cpp" PROPERTIES COMPILE_OPTIONS "${warnings}")

enable_testing()

# White-box tests: each includes dbg_net.cpp to reach the parsers.
foreach(name test_ws test_http test_fuzz)
  add_executable(${name} ${name}.cpp "${d_core}/deps/crypto/sha1.cpp")
  target_compile_options(${name} PRIVATE ${warnings})
  target_link_libraries(${name} ${platform_libs})
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # dbg_net.cpp is not the main file of these translation units, which makes GCC report its internal types.
    target_compile_options(${name} PRIVATE -Wno-subobject-linkage)
  endif()
  add_test(NAME ${name} COMMAND ${name})
endforeach()

add_executable(test_e2e test_e2e.cpp)
target_compile_options(test_e2e PRIVATE ${warnings})
target_link_libraries(test_e2e dbg_net)
add_test(NAME test_e2e COMMAND test_e2e)

add_executable(test_webui test_webui.cpp $<TARGET_OBJECTS:dbg_webui>)
target_compile_options(test_webui PRIVATE ${warnings})
target_link_libraries(test_webui dbg_net)
add_test(NAME test_webui COMMAND test_webui "${DBG_WEBUI_BUNDLE_DIR}")

add_executable(dbg_net_demo demo.cpp $<TARGET_OBJECTS:dbg_webui>)
target_compile_options(dbg_net_demo PRIVATE ${warnings})
target_link_libraries(dbg_net_demo dbg_net)

set_tests_properties(test_ws test_http test_fuzz test_e2e test_webui PROPERTIES TIMEOUT 300)

# Interoperability with the "ws" package of the devtools tree. Skipped without node or without the package.
find_program(NODE_EXECUTABLE NAMES node nodejs)
set(DBG_NET_NODE_MODULES "" CACHE PATH "node_modules directory that has the ws package (default: the devtools tree next to or inside this one)")
set(node_modules "${DBG_NET_NODE_MODULES}")
foreach(candidate "${tree}/devtools/node_modules" "${tree}/../dreamcast-devtools/node_modules")
  if(node_modules STREQUAL "" AND EXISTS "${candidate}/ws")
    set(node_modules "${candidate}")
  endif()
endforeach()
if(NODE_EXECUTABLE AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME interop_ws COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/interop_ws.js" $<TARGET_FILE:dbg_net_demo> "${node_modules}")
  set_tests_properties(interop_ws PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
else()
  add_test(NAME interop_ws COMMAND "${CMAKE_COMMAND}" -E echo "SKIP: no node to run interop_ws.js with")
  set_tests_properties(interop_ws PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
endif()

# The embed step itself: which bundle is picked, what stops the configure step, what a rebuilt bundle does,
# which files of a bundle are embedded.
foreach(case off auto mismatch incomplete rebuilt corrupt names)
  add_test(NAME embed_${case} COMMAND "${CMAKE_COMMAND}"
    "-DCASE=${case}" "-DTREE=${tree}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/embed_check/${case}" "-DGENERATOR=${CMAKE_GENERATOR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_check/run.cmake")
  set_tests_properties(embed_${case} PROPERTIES TIMEOUT 300)
endforeach()
