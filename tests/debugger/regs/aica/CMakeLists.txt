# Tests of the sound generator of the AICA from the emulator's own translation unit. It is compiled with the
# definitions and flags of the emulator build (reicast/cmake/config.cmake), which is why this is a directory of
# its own: they stay out of the table tests. What the unit references from the rest of the emulator is in
# aica_test.cpp. NULLDC_DEBUGGER is not defined: the events of the generator compile to nothing.

list(APPEND CMAKE_MODULE_PATH "${DEVCAST_DIR}/reicast/cmake")
include(config)

add_executable(aica_test aica_test.cpp "${LIBSWIRL_DIR}/hw/aica/sgc_if.cpp")
target_include_directories(aica_test PRIVATE "${LIBSWIRL_DIR}" "${LIBSWIRL_DIR}/deps" "${DEVCAST_DIR}")
target_compile_features(aica_test PRIVATE cxx_std_20)	# as the emulator target

if(${HOST_OS} EQUAL ${OS_WINDOWS})
  target_include_directories(aica_test PRIVATE "${LIBSWIRL_DIR}/deps/dirent")
endif()

if(REGS_SANITIZE AND NOT MSVC)
  target_compile_options(aica_test PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(aica_test PRIVATE -fsanitize=address,undefined)
endif()

# The loop flag of a channel: clear in a generator created in memory that held anything, one bit for the guest
# and for the debugger
add_test(NAME aica.channels COMMAND aica_test channels)
