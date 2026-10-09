cmake_minimum_required(VERSION 3.16)
project(dbg_symbols_tests CXX)

# Standalone tests for the symbol table of libswirl/debugger (dbg_symbols.cpp), which links without the
# emulator. Nothing here is part of the emulator build.
#
#   cmake -S tests/debugger/symbols -B <builddir> -G Ninja && cmake --build <builddir> && ctest --test-dir <builddir>
#
# The tests project (tests/CMakeLists.txt) adds this directory as well. What it sets is optional here:
#   NULLDC_TEST_PREFIX          put in front of every test name (debugger.symbols.)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SYMBOLS_SANITIZE "Build with -fsanitize=address,undefined" OFF)

get_filename_component(DEVCAST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(LIBSWIRL_DIR "${DEVCAST_DIR}/libswirl")

add_executable(symbols_test symbols_test.cpp "${LIBSWIRL_DIR}/debugger/dbg_symbols.cpp")
target_include_directories(symbols_test PRIVATE "${LIBSWIRL_DIR}" "${LIBSWIRL_DIR}/debugger")

if(MSVC)
  target_compile_options(symbols_test PRIVATE /W4 /permissive-)
  target_compile_definitions(symbols_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(symbols_test PRIVATE -Wall -Wextra -Wshadow -Wconversion -pedantic-errors)
  if(SYMBOLS_SANITIZE)
    target_compile_options(symbols_test PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(symbols_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()

enable_testing()

# The .symtab walk on a fixture ELF, lookup by address and by name, the text form of an address
add_test(NAME ${NULLDC_TEST_PREFIX}table COMMAND symbols_test table)

# Images that are cut, that are no ELF, and that name offsets outside of themselves
add_test(NAME ${NULLDC_TEST_PREFIX}damaged COMMAND symbols_test damaged)
