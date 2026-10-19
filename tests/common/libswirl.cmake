## libswirl for the tests ##
#
#	The suites link libraries made of hand-picked libswirl translation units. They are compiled with the
#	definitions and flags of the emulator build (nulldc3/cmake/config.cmake), so a test runs the same code
#	the emulator runs.
#

list(APPEND CMAKE_MODULE_PATH "${nulldc_root_path}/nulldc3/cmake")
include(config)

# The recompiler selection belongs to each libswirl library (nulldc_add_libswirl_library), not to the project
remove_definitions(-DFEAT_SHREC=${FEAT_SHREC} -DFEAT_AREC=${FEAT_AREC})

add_library(nulldc_libswirl_headers INTERFACE)
target_include_directories(nulldc_libswirl_headers INTERFACE
  "${nulldc_core_path}"
  "${nulldc_core_path}/deps"
  "${nulldc_root_path}"
)
target_compile_features(nulldc_libswirl_headers INTERFACE cxx_std_20)	# as the emulator target

if(${HOST_OS} EQUAL ${OS_WINDOWS})
  target_include_directories(nulldc_libswirl_headers INTERFACE "${nulldc_core_path}/deps/dirent")
endif()

# nulldc_add_libswirl_library(<target>
#                             [SHREC <DYNAREC_NONE|DYNAREC_JIT>] [AREC <...>]
#                             SOURCES <file relative to libswirl>...)
#
# Object library of libswirl translation units. Linking it adds every object to the executable (their static
# initialisers register opcode tables and recompiler backends) and passes the FEAT_* selection on to the
# sources of the executable. Without SHREC/AREC the selection is the one of the emulator build.
function(nulldc_add_libswirl_library target)
  cmake_parse_arguments(ARG "" "SHREC;AREC" "SOURCES" ${ARGN})

  set(shrec ${FEAT_SHREC})
  set(arec ${FEAT_AREC})
  if(ARG_SHREC)
    set(shrec ${${ARG_SHREC}})
  endif()
  if(ARG_AREC)
    set(arec ${${ARG_AREC}})
  endif()

  set(srcs "")
  foreach(src ${ARG_SOURCES})
    list(APPEND srcs "${nulldc_core_path}/${src}")
  endforeach()

  add_library(${target} OBJECT ${srcs})
  target_link_libraries(${target} PUBLIC nulldc_libswirl_headers)
  target_compile_definitions(${target} PUBLIC FEAT_SHREC=${shrec} FEAT_AREC=${arec})
endfunction()

# The SH4 interpreter: opcode handlers, opcode tables, register banks, exceptions and the memory map tables.
# What these reference from the rest of the emulator is in tests/common/sh4_stubs*.cpp
set(NULLDC_SH4_INTERP_SOURCES
  hw/sh4/interpr/sh4_opcodes.cpp
  hw/sh4/interpr/sh4_fpu.cpp
  hw/sh4/interpr/sh4_interpreter.cpp
  hw/sh4/sh4_opcode_list.cpp
  hw/sh4/sh4_core_regs.cpp
  hw/sh4/sh4_interrupts.cpp
  hw/sh4/sh4_rom.cpp
  hw/mem/_vmem.cpp
)
