## System tests ##
#
#	The emulator binary itself, headless, on guest programs: system_test.py runs one case per test, and
#	vmem_test.py the cases of the memory layout (system.vmem.*); README.md has the cases. Two builds of the
#	emulator are tested, the default one (system.*) and the one without the TA of lxdream, -DHAS_TA_LLE=OFF
#	(system.hle.*).
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
  "rtc.game:"
  "cfg.untouched:"
  "headless.no_program:"
  "headless.gl_renderer:"
  "fault.segv:SHIM"
  "fault.bus:SHIM"
  "fault.ill:SHIM"
  "fault.on_request:SHIM"
  "fault.block:SHIM"
  "fault.sent:SHIM"
  "fault.guest:SHIM"
  "boot.2ndmix.jit:ELF"
  "boot.2ndmix.interp:ELF"
  "boot.hello.jit:ELF"
  "boot.hello.interp:ELF"
  "frame.refsw2:ELF,LLE"
  "determinism:ELF"
  "perf.script:"
  "dirs.default:"
  "dirs.portable:"
  "dirs.shared:"
  "dirs.errors:"
)

find_package(Python3 COMPONENTS Interpreter)

set(nulldc_system_skip "")
if(NOT NULLDC_SYSTEM_TESTS)
  set(nulldc_system_skip "NULLDC_SYSTEM_TESTS is OFF")
elseif(${HOST_OS} EQUAL ${OS_WINDOWS} OR ${HOST_OS} EQUAL ${OS_DARWIN})
  set(nulldc_system_skip "the Linux-only system-test harness")
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
  target_link_libraries(nulldc_fault_shim PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  set_target_properties(nulldc_fault_shim PROPERTIES PREFIX "" OUTPUT_NAME fault_shim)

  # A host without memfd_create, for system.vmem.taken and vmem.none, see vmem_shim.cpp
  add_library(nulldc_vmem_shim MODULE vmem_shim.cpp)
  target_link_libraries(nulldc_vmem_shim PRIVATE ${CMAKE_DL_LIBS})
  set_target_properties(nulldc_vmem_shim PROPERTIES PREFIX "" OUTPUT_NAME vmem_shim)

  # work.link -> work: the scratch of the dirs.* cases is reached through a symbolic link, as a build directory
  # in a linked home is. The emulator names its directory with every link resolved (/proc/self/exe, getcwd), and
  # the cases expect the names it says from the directory as it is, not as --work spells it
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/work")
  file(CREATE_LINK "work" "${CMAKE_CURRENT_BINARY_DIR}/work.link" SYMBOLIC)
endif()

# The cases of vmem_test.py: the memory of every emulator is its own, also when emulators start together; and an
# emulator that cannot have the layout runs without it. All boot 2ndmix.
set(nulldc_vmem_cases vmem.concurrent vmem.none vmem.taken)

# nulldc_system_emulator(<prefix> <given binary> <build dir name> <TA_HLE ON|OFF>): the binary, built here unless given
function(nulldc_system_emulator prefix given dir ta_hle)
  if(given)
    set(binary "${given}")
  else()
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/${dir}/nulldc3")
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

    # the dirs.* cases are given their scratch through a symbolic link, see work.link above
    set(work "${CMAKE_CURRENT_BINARY_DIR}/work")
    if(case MATCHES "^dirs\\.")
      set(work "${CMAKE_CURRENT_BINARY_DIR}/work.link")
    endif()
    set(command "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/system_test.py"
                --emulator "${binary}" --case ${case} --work "${work}/${name}"
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

  foreach(case ${nulldc_vmem_cases})
    set(name "${prefix}.${case}")
    if(NOT nulldc_system_have_elf)
      nulldc_add_missing_data_test(${name} "${nulldc_system_elf_missing}")
      continue()
    endif()

    set(command "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/vmem_test.py"
                --emulator "${binary}" --case ${case} --work "${CMAKE_CURRENT_BINARY_DIR}/work/${name}"
                --elf-dir "${NULLDC_ELF_DIR}" --shim "$<TARGET_FILE:nulldc_vmem_shim>")
    if(ta_hle)
      list(APPEND command --ta-hle)
    endif()

    add_test(NAME ${name} COMMAND ${command})
    # vmem_test.py imports system_test.py and writes no bytecode into the source tree; a host without a user and
    # mount namespace of its own skips vmem.taken and vmem.none
    set_tests_properties(${name} PROPERTIES TIMEOUT 900 ENVIRONMENT PYTHONDONTWRITEBYTECODE=1
                                            SKIP_REGULAR_EXPRESSION "SKIPPED:")
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
    foreach(case ${nulldc_vmem_cases})
      nulldc_add_skipped_test(${prefix}.${case} "${nulldc_system_skip}")
    endforeach()
  endforeach()
else()
  nulldc_system_emulator(system     "${NULLDC_EMULATOR}"        emulator        OFF)
  nulldc_system_emulator(system.hle "${NULLDC_EMULATOR_TA_HLE}" emulator-ta-hle ON)
endif()
