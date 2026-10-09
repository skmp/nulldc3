## config module 
#
#	get luserx0 to doc this shit or something, vars in all caps are to be exported as defs if they aren't in build.h already
#		handle options for FEAT per platform, so rec isn't built for targets w.o one...
#		*TODO* setup git version like it's done in VS/make and configure header so --version works still
#		*TODO* lots of other shit to improve build, add enabling/disabling libs/features, setting 3rd party libs as either built in  static, dynamic  or shared/system
#
#


set(ZBUILD Off)



set(BUILD_LIBS OFF)         ## Scope:Local If set will build libs { dreamcast, osd, ... }
set(BUILD_LIB_TYPE STATIC)  ## Scope:Local If BUILD_LIBS is set, will use this as type of lib to use { STATIC, SHARED, MODULE (plugin) } *TODO*
set(BUILD_SHARED_LIBS OFF)  ## Scope:CMAKE If type is not specified in add_library, use SHARED




## Build flags ##
#

set(DC_PLATFORM_MASK        7)  # Z: Uh, not a bitset
set(DC_PLATFORM_DREAMCAST   0)  # /* Works, for the most part */
set(DC_PLATFORM_DEV_UNIT    1)  # /* This is missing hardware */
set(DC_PLATFORM_NAOMI       2)  # /* Works, for the most part */ 
set(DC_PLATFORM_NAOMI2      3)  # /* Needs to be done, 2xsh4 + 2xpvr + custom TNL */
set(DC_PLATFORM_ATOMISWAVE  4)  # /* Needs to be done, DC-like hardware with possibly more ram */
set(DC_PLATFORM_HIKARU      5)  # /* Needs to be done, 2xsh4, 2x aica , custom vpu */
set(DC_PLATFORM_AURORA      6)  # /* Needs to be done, Uses newer 300 mhz sh4 + 150 mhz pvr mbx SoC */



set(OS_WINDOWS     0x10000001)  # HOST_OS
set(OS_LINUX       0x10000002)
set(OS_DARWIN      0x10000003)




set(CPU_X64        0x20000004)  # HOST_CPU
set(CPU_A64        0x20000008)
set(CPU_GENERIC    0x20000005)  # used for emscripten, etc

set(DYNAREC_NONE   0x40000001)  # FEAT_SHREC, FEAT_AREC, FEAT_DSPREC
set(DYNAREC_JIT    0x40000002)

set(COMPILER_VC    0x30000001)  # BUILD_COMPILER
set(COMPILER_GCC   0x30000002)
set(COMPILER_CLANG 0x30000003)
set(COMPILER_INTEL 0x30000004)

set(TA_HLE 0x60000001)
set(TA_LLE 0x60000002)


# options
# on by default
option(HAS_TA_LLE "Use TA LLE" ON)

# set(FEAT_TA ${TA_LLE})

if(HAS_TA_LLE)
  set(FEAT_TA ${TA_LLE})
else()
  set(FEAT_TA ${TA_HLE})
endif()


## These default to host, but are used for cross so make sure not to contaminate
#
#	CMAKE_SYSTEM		${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_VERSION}.
#	CMAKE_SYSTEM_NAME		CMAKE_HOST_SYSTEM_NAME			uname -s	Linux, Windows, and Darwin
#	CMAKE_SYSTEM_VERSION	CMAKE_HOST_SYSTEM_VERSION		uname -r
#	CMAKE_SYSTEM_PROCESSOR	CMAKE_HOST_SYSTEM_PROCESSOR		uname -p
#
#
#
#	BOOL: CMAKE_HOST_UNIX	CMAKE_HOST_WIN32  CMAKE_HOST_APPLE
#
#
#	CMAKE_LIBRARY_ARCHITECTURE	CMAKE_<LANG>_LIBRARY_ARCHITECTURE	<prefix>/lib/<arch>
#
#

if(MSVC)
    set(CMAKE_SYSTEM_PROCESSOR ${MSVC_CXX_ARCHITECTURE_ID})
endif()



## Native builds are for x86_64 and arm64. A 32-bit x86 or arm host has no recompiler, no fault context and no FP mode
#  code in this tree, and the configure stops instead of building what is left. Any other processor has no recompiler
#  and runs the interpreters.
#
function(nulldc_no_32bit_host what)
  message(FATAL_ERROR "32-bit x86 and arm hosts are not supported (${what}): nullDC builds for x86_64 and arm64.")
endfunction()

## strings are used to append to path/file names, and to filter multiple possibilities down to one 
#		AMD64/x86_64:x64 etc 
#
if(EMSCRIPTEN)	# its toolchain file calls the processor "x86": the host is WebAssembly
  set(host_arch "wasm")
  set(HOST_CPU ${CPU_GENERIC})
#
elseif(("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64") OR
       ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64") OR
       ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x64"))
  set(host_arch "x64")
  set(HOST_CPU ${CPU_X64}) 
#
elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "aarch64")
  set(host_arch "arm64")
  set(HOST_CPU ${CPU_A64})
elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "arm64")
  set(host_arch "arm64")
  set(HOST_CPU ${CPU_A64})
#
elseif(("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^i[3-6]86$") OR
       ("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^[xX]86$") OR
       ("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "arm") OR			# has to be done after arm64
       ("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^ARM(V[0-9A-Z]+)?$"))	# the names MSVC has for it
  nulldc_no_32bit_host("CMAKE_SYSTEM_PROCESSOR is \"${CMAKE_SYSTEM_PROCESSOR}\"")
#
else()
  message("Warning: Unknown Host System Processor: \"${CMAKE_SYSTEM_PROCESSOR}\"")
  set(host_arch "${CMAKE_SYSTEM_PROCESSOR}")
  set(HOST_CPU ${CPU_GENERIC})
endif()

# The same host with a 32-bit compiler (-m32, a 32-bit userland on a 64-bit kernel)
if(((${HOST_CPU} EQUAL ${CPU_X64}) OR (${HOST_CPU} EQUAL ${CPU_A64})) AND ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4"))
  nulldc_no_32bit_host("the compiler has 4 byte pointers on \"${CMAKE_SYSTEM_PROCESSOR}\"")
endif()

string(TOLOWER ${CMAKE_SYSTEM_NAME} host_os)

#message(" - testing cmake host_os: \"${host_os}\"")

## HOST_* is not TARGET_*  ;;  change ndc-e internal naming it's wrong , then change this  ;;

if(CMAKE_HOST_WIN32)
  set(HOST_OS ${OS_WINDOWS}) 

elseif(CMAKE_HOST_APPLE)

  if("${host_arch}" MATCHES "arm")
    set(HOST_OS ${OS_DARWIN})
    set(TARGET_OSX On)
    add_definitions(-DTARGET_OSX)
  else()
    set(HOST_OS ${OS_DARWIN})
    set(TARGET_OSX On)
    add_definitions(-DTARGET_OSX)
  endif()
  
elseif(CMAKE_HOST_UNIX) # GP UNIX MUST BE AFTER OTHER UNIX'ish options such as APPLE , it matches both 

    set(HOST_OS ${OS_LINUX})
endif()



option(TARGET_NO_REC  "Interpreters only: no SH4, ARM7 or DSP recompiler, on any host" OFF)
option(TARGET_NO_AREC "No ARM7 and no DSP recompiler" OFF)



## Main CPU Dynarec
if((${HOST_CPU} EQUAL ${CPU_X64}) OR (${HOST_CPU} EQUAL ${CPU_A64}))
  message("SH4 Dynarec Features Available")
  set(FEAT_SHREC  ${DYNAREC_JIT})
else()
  message("SH4 Dynarec Features Missing")
  set(FEAT_SHREC  ${DYNAREC_NONE})
endif()

# Sound CPU dynarec
if(${HOST_CPU} EQUAL ${CPU_A64})
  message("ARM7 Dynarec Features Available")
  set(FEAT_AREC  ${DYNAREC_JIT})
else()
  message("ARM7 Dynarec Features Missing")
  set(FEAT_AREC ${DYNAREC_NONE})
endif()

# Sound DSP dynarec
if(${HOST_CPU} EQUAL ${CPU_A64})
  message("DSP Dynarec Features Available")
  set(FEAT_DSPREC  ${DYNAREC_JIT})
#
else()
  message("DSP Dynarec Features Missing")
  set(FEAT_DSPREC ${DYNAREC_NONE})
endif()

## Handle TARGET_* to FEAT_  *FIXME* stupid use one or the other and propogate : part of build cleanup , TARGET_ will only be for platform specifics and FEAT_ as OPTIONS
#
if(TARGET_NO_REC)
  set(FEAT_SHREC  ${DYNAREC_NONE})
  set(FEAT_AREC   ${DYNAREC_NONE})
  set(FEAT_DSPREC ${DYNAREC_NONE})
endif()

if(TARGET_NO_AREC)
  set(FEAT_AREC   ${DYNAREC_NONE})
  set(FEAT_DSPREC ${DYNAREC_NONE})
endif()

## A generic host has no fault context (oslib/context.h) and no fault trampoline (linux/common.cpp): it is built without
#  the fault handler, so without nvmem and without write protection of VRAM, and without the sampling profiler.
#
if(${HOST_CPU} EQUAL ${CPU_GENERIC})
  add_definitions(-DTARGET_NO_EXCEPTIONS -DTARGET_NO_NIXPROF)
endif()





######## Looks like something to delete, but if we're going to handle options here and NOT in libosd/lib* #########

# FindNativeCompilers()
## options BUILD_COMPILER { GCC, Clang, Intel, RealView? }


#set(CMAKE_C_COMPILER clang)
#set(CMAKE_C_COMPILER_TARGET ${triple})
#set(CMAKE_CXX_COMPILER clang++)
#set(CMAKE_CXX_COMPILER_TARGET ${triple})



if(${HOST_OS} EQUAL ${OS_LINUX})
# option SUPPORT_X11
# option FEAT_HAS_NIXPROF
# option EMSCripten
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
#
  set(BUILD_COMPILER ${COMPILER_VC})
  message("MSVC Platform: ${CMAKE_VS_PLATFORM_NAME}")
  message("MSVC Toolset:  ${CMAKE_VS_PLATFORM_TOOLSET}")
  
  
  add_definitions(/D_CRT_SECURE_NO_WARNINGS /D_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1)

#
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU") 
  set(BUILD_COMPILER ${COMPILER_GCC})
#
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")  # AppleClang ffs
  set(BUILD_COMPILER ${COMPILER_CLANG})
#
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
  set(BUILD_COMPILER ${COMPILER_INTEL})
#
else()
  message("Unknown Compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()



## Setup some common flags 
#
if ((${BUILD_COMPILER} EQUAL ${COMPILER_VC}) OR
	(${BUILD_COMPILER} EQUAL ${COMPILER_CLANG}) AND (${HOST_OS} STREQUAL ${OS_WINDOWS}))

		
  add_definitions(/D_CRT_SECURE_NO_WARNINGS /D_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1)

  if(${BUILD_COMPILER} EQUAL ${COMPILER_CLANG})
    add_definitions(/DXBYAK_NO_OP_NAMES /DTARGET_NO_OPENMP)  #*FIXME* check openmp on clang-cl
    remove_definitions(/U_HAS_STD_BYTE)
    set(_CXX_FLAGS "/std:c++14")	# /U_HAS_STD_BYTE not working, have to use c++14 not 17 :|
    set(_C_FLAGS "-Wno-unused-function -Wno-unused-variable")
  endif()


elseif ((${BUILD_COMPILER} EQUAL ${COMPILER_GCC}) OR
		(${BUILD_COMPILER} EQUAL ${COMPILER_CLANG})) # AND NOT ${HOST_OS} EQUAL ${OS_WINDOWS}))
  
  if(${HOST_CPU} EQUAL ${CPU_X64})
    set(_C_FLAGS "${_C_FLAGS} -msse4")
  endif()
  
    
  set(_CXX_FLAGS "${_CXX_FLAGS} -fno-operator-names -fpermissive -fno-strict-aliasing") # -fcxx-exceptions") ## xbyak needs exceptions

  set(CMAKE_CXX_STANDARD 11)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()


set(_CXX_FLAGS "${_CXX_FLAGS} ${_C_FLAGS}")



set(CMAKE_C_FLAGS " ${_C_FLAGS}") # ${CMAKE_C_FLAGS}   -- these hold default VC flags for non VC Build ?
set(CMAKE_CXX_FLAGS " ${_CXX_FLAGS}") # ${CMAKE_CXX_FLAGS}



#if defined(TARGET_NAOMI)
	#define DC_PLATFORM DC_PLATFORM_NAOMI
	#undef TARGET_NAOMI
#endif



#if defined(TARGET_NO_NIXPROF)
#define FEAT_HAS_NIXPROF 0
#endif

#if defined(TARGET_NO_COREIO_HTTP)
#define FEAT_HAS_COREIO_HTTP 0
#endif

#if defined(TARGET_SOFTREND)    # need -fopenmp
	#define FEAT_HAS_SOFTREND 1
#endif




if(ZBUILD)
  set(DEBUG_CMAKE ON)
  add_definitions(-D_Z_)  # Get rid of some warnings and internal dev testing
  
  if(NOT TARGET_OSX)
     set(USE_QT ON)
  endif()
endif()



# configure options for osd/ui 
# osd_default, osd_qt
# ui_default, ui_sdl, ui_qt
# USE_NATIVE , USE_SDL , USE_QT  -- these (can) define multiple

option(USE_QT False "Use Qt5 for UI and support OS Deps.")




#option TARGET_NO_WEBUI





#option(BUILD_TESTS "Build tests" OFF)	# todo: luserx0 this is your arena, you want tests add em


add_definitions(-DCMAKE_BUILD)




add_definitions(-DHOST_OS=${HOST_OS})
add_definitions(-DHOST_CPU=${HOST_CPU})

add_definitions(-DFEAT_AREC=${FEAT_AREC})
add_definitions(-DFEAT_SHREC=${FEAT_SHREC})
add_definitions(-DFEAT_DSPREC=${FEAT_DSPREC})

add_definitions(-DBUILD_COMPILER=${BUILD_COMPILER})

add_definitions(-DFEAT_TA=${FEAT_TA})

add_definitions(-DTARGET_NO_WEBUI)
add_definitions(-DTARGET_NO_OPENMP)
add_definitions(-DDEF_CONSOLE)


set(RE_CMAKE_CONFIGURED 1)
#add_definitions(-D=${})





