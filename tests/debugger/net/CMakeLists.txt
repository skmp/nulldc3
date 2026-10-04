# Tests of the debugger transport (libswirl/debugger/dbg_net), built outside the emulator:
#
#   cmake -S tests/debugger/net -B <build dir> [-DDBG_NET_SANITIZE=ON]
#   cmake --build <build dir> && ctest --test-dir <build dir>

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

set_tests_properties(test_ws test_http test_fuzz test_e2e PROPERTIES TIMEOUT 300)
