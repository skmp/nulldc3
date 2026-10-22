## Linux arm64: the state hashes of the arm64 recompiler there, and the start a kernel with other pages refuses ##
#
#	Only on a Linux arm64 host (HOST_OS Linux, HOST_CPU arm64 of nulldc3/cmake/config.cmake): on every other host this
#	directory adds nothing. The emulator is the one the system tests build (<build dir>/system/emulator/nulldc3), or
#	NULLDC_EMULATOR. Every run is headless, with the keys of an automated run (a portable directory under the test's
#	work directory, config:Headless=yes, audio:backend=none, config:Dreamcast.RTC=946684800), DISPLAY and
#	WAYLAND_DISPLAY taken out of its environment, with a core file size limit of 1 byte (as common/emurun.py starts
#	an emulator on a POSIX host: a crash is not piped to the system's core handler), under a timeout; each case
#	checks that envelope on a shell before it starts an emulator. The cases are in linux_arm64.cmake; the output of
#	each run is in <build dir>/linux_arm64/work/<test>/*.log.
#
#	linux_arm64.hashes.jit     the state hashes of the SH4 on the recompiler of a Linux arm64 host: the guest of the
#	                           tree (tests/debugger/guest/dsptest/dsptest.elf) at 90 vblanks and 2ndmix.elf
#	                           (NULLDC_ELF_DIR) at 600, each the reference linux_arm64.cmake has, which the arm64
#	                           host's runs gave (and which equal the Mac's of macos.hashes.jit): a change of what
#	                           the arm64 recompiler computes, or of the Linux fault context it runs on
#	                           (libswirl/linux/context.cpp), is red. A hash is good for one host (design 5.6)
#	linux_arm64.pages.refused  the refusal of a kernel whose pages are not 4 KiB (libswirl/linux/common.cpp,
#	                           PL-D52), through its test hook NULLDC_TEST_PAGE_SIZE: 16384 and 65536 end --version
#	                           with exit status 1, the line "nulldc3: this build needs 4 KiB pages; this kernel uses
#	                           <n> KiB" as the whole of stderr and nothing on stdout; 4096, and no hook, print the
#	                           version and exit 0
#

if(NOT ${HOST_OS} EQUAL ${OS_LINUX} OR NOT ${HOST_CPU} EQUAL ${CPU_A64})
  return()
endif()

set(nulldc_la64_script "${CMAKE_CURRENT_SOURCE_DIR}/linux_arm64.cmake")
set(nulldc_la64_guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")

set(nulldc_la64_skip "")
if(NULLDC_EMULATOR)
  set(nulldc_la64_emulator "${NULLDC_EMULATOR}")
elseif(NULLDC_SYSTEM_TESTS)
  # what tests/system builds, every time the project is built
  set(nulldc_la64_emulator "${CMAKE_BINARY_DIR}/system/emulator/nulldc3")
else()
  set(nulldc_la64_skip "NULLDC_SYSTEM_TESTS is OFF and no NULLDC_EMULATOR is given: there is no emulator to run")
endif()

# nulldc_la64_case(<test> <case> [TIMEOUT <s>])
function(nulldc_la64_case test case)
  cmake_parse_arguments(ARG "" "TIMEOUT" "" ${ARGN})
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 120)
  endif()
  if(nulldc_la64_skip)
    nulldc_add_skipped_test(${test} "${nulldc_la64_skip}")
    return()
  endif()
  add_test(NAME ${test}
           COMMAND "${CMAKE_COMMAND}" "-DCASE=${case}" "-DEMULATOR=${nulldc_la64_emulator}" "-DGUEST=${nulldc_la64_guest}"
                   "-DELF_DIR=${NULLDC_ELF_DIR}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/work/${test}"
                   "-DTEST_TIMEOUT=${ARG_TIMEOUT}" -P "${nulldc_la64_script}")
  set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
endfunction()

if(NOT NULLDC_ELF_DIR OR NOT EXISTS "${NULLDC_ELF_DIR}/2ndmix.elf")
  nulldc_add_missing_data_test(linux_arm64.hashes.jit
    "no 2ndmix.elf in NULLDC_ELF_DIR (\"${NULLDC_ELF_DIR}\"), configure with -DNULLDC_ELF_DIR=<directory with the KallistiOS examples>")
else()
  nulldc_la64_case(linux_arm64.hashes.jit hashes TIMEOUT 300)
endif()
nulldc_la64_case(linux_arm64.pages.refused pages TIMEOUT 120)
