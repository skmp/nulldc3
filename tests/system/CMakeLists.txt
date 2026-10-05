## System tests ##
#
#	The emulator binary itself, headless, on guest programs: system_test.py runs one case per test, README.md
#	has the cases. Two builds of the emulator are tested, the default one (system.*) and the one without the
#	TA of lxdream, -DHAS_TA_LLE=OFF (system.hle.*).
#
#	NULLDC_EMULATOR / NULLDC_EMULATOR_TA_HLE    binaries to test. Empty: this project builds the emulator of the
#	                                            tree, into <build dir>/system/emulator and emulator-ta-hle
#	NULLDC_ELF_DIR                              directory with 2ndmix.elf and hello.elf, the KallistiOS examples.
#	                                            Empty: the cases that boot them are skipped (missing data)
#	NULLDC_SYSTEM_TESTS=OFF                     no system tests and no build of the emulator
#

option(NULLDC_SYSTEM_TESTS "Build the emulator and run the system tests (system.*)" ON)
set(NULLDC_EMULATOR "" CACHE FILEPATH "Emulator binary for the system tests, empty: built from this tree")
set(NULLDC_EMULATOR_TA_HLE "" CACHE FILEPATH "Emulator binary of a -DHAS_TA_LLE=OFF build for the system tests, empty: built from this tree")
set(NULLDC_ELF_DIR "" CACHE PATH "Directory with 2ndmix.elf and hello.elf (KallistiOS examples), empty: the system tests that boot them are skipped")

# case, and what it needs besides the emulator: ELF (the programs of NULLDC_ELF_DIR), SHIM (fault_shim),
# LLE (the renderers of the default build)
set(nulldc_system_cases
  "help:"
  "guest_exit.jit:"
  "guest_exit.interp:"
  "exit.vblanks.jit:"
  "exit.vblanks.interp:"
  "exit.seconds:"
  "rtc:"
  "cfg.untouched:"
  "headless.no_program:"
  "headless.gl_renderer:"
  "fault.segv:SHIM"
  "fault.bus:SHIM"
  "fault.ill:SHIM"
  "fault.block:SHIM"
  "boot.2ndmix.jit:ELF"
  "boot.2ndmix.interp:ELF"
  "boot.hello.jit:ELF"
  "boot.hello.interp:ELF"
  "frame.refsw2:ELF,LLE"
)

find_package(Python3 COMPONENTS Interpreter)

set(nulldc_system_skip "")
if(NOT NULLDC_SYSTEM_TESTS)
  set(nulldc_system_skip "NULLDC_SYSTEM_TESTS is OFF")
elseif(NOT ${HOST_OS} EQUAL ${OS_LINUX})
  set(nulldc_system_skip "headless mode is there on Linux hosts only")
elseif(NOT Python3_Interpreter_FOUND)
  set(nulldc_system_skip "no python3 to run tests/system/system_test.py with")
endif()

if(NOT NULLDC_ELF_DIR OR NOT EXISTS "${NULLDC_ELF_DIR}/2ndmix.elf" OR NOT EXISTS "${NULLDC_ELF_DIR}/hello.elf")
  set(nulldc_system_have_elf OFF)
  set(nulldc_system_elf_missing
      "no 2ndmix.elf and hello.elf in NULLDC_ELF_DIR (\"${NULLDC_ELF_DIR}\"), configure with -DNULLDC_ELF_DIR=<directory with the two KallistiOS examples>")
  if(NOT nulldc_system_skip)
    message(STATUS "${nulldc_system_elf_missing}")
    message(STATUS "The system tests that boot them do not run.")
  endif()
else()
  set(nulldc_system_have_elf ON)
endif()

if(NOT nulldc_system_skip)
  # Faults the emulator on request, see fault_shim.cpp
  add_library(nulldc_fault_shim MODULE fault_shim.cpp)
  find_package(Threads REQUIRED)
  target_link_libraries(nulldc_fault_shim PRIVATE Threads::Threads)
  set_target_properties(nulldc_fault_shim PROPERTIES PREFIX "" OUTPUT_NAME fault_shim)
endif()

# nulldc_system_emulator(<prefix> <given binary> <build dir name> <TA_HLE ON|OFF>): the binary, built here unless given
function(nulldc_system_emulator prefix given dir ta_hle)
  if(given)
    set(binary "${given}")
  else()
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/${dir}/devcast")
    # In the console pool: one build of the emulator at a time, and it has all the cores for itself
    add_custom_target(nulldc_${dir} ALL
      COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${nulldc_root_path}" "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/${dir}"
              "-DGENERATOR=${CMAKE_GENERATOR}" "-DTA_HLE=${ta_hle}" -P "${CMAKE_CURRENT_SOURCE_DIR}/build_emulator.cmake"
      BYPRODUCTS "${binary}"
      USES_TERMINAL
      COMMENT "The emulator for the system tests: ${dir}"
      VERBATIM)
  endif()

  foreach(entry ${nulldc_system_cases})
    string(REPLACE ":" ";" parts "${entry}:")
    list(GET parts 0 case)
    list(GET parts 1 needs)
    set(name "${prefix}.${case}")

    if(needs MATCHES "LLE" AND ta_hle)
      continue()
    endif()

    if(needs MATCHES "ELF" AND NOT nulldc_system_have_elf)
      nulldc_add_missing_data_test(${name} "${nulldc_system_elf_missing}")
      continue()
    endif()

    set(command "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/system_test.py"
                --emulator "${binary}" --case ${case} --work "${CMAKE_CURRENT_BINARY_DIR}/work/${name}"
                --shim "$<TARGET_FILE:nulldc_fault_shim>")
    if(nulldc_system_have_elf)
      list(APPEND command --elf-dir "${NULLDC_ELF_DIR}")
    endif()
    if(ta_hle)
      list(APPEND command --ta-hle)
    endif()

    add_test(NAME ${name} COMMAND ${command})
    # system_test.py has a watchdog per run of the emulator, this is the one behind it
    set_tests_properties(${name} PROPERTIES TIMEOUT 900)
  endforeach()
endfunction()

if(nulldc_system_skip)
  foreach(prefix system system.hle)
    foreach(entry ${nulldc_system_cases})
      string(REPLACE ":" ";" parts "${entry}:")
      list(GET parts 0 case)
      list(GET parts 1 needs)
      if(NOT (needs MATCHES "LLE" AND prefix STREQUAL "system.hle"))
        nulldc_add_skipped_test(${prefix}.${case} "${nulldc_system_skip}")
      endif()
    endforeach()
  endforeach()
else()
  nulldc_system_emulator(system     "${NULLDC_EMULATOR}"        emulator        OFF)
  nulldc_system_emulator(system.hle "${NULLDC_EMULATOR_TA_HLE}" emulator-ta-hle ON)
endif()
