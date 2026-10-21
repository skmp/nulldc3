## macOS: the run envelope and the emulator's headless mode, fatal ends and bundle on macOS ##
#
#	Only on macOS. Every test of this directory that starts a program starts it through macrun.py (the run
#	envelope of the Mac: the sandbox profile no-windowserver.sb, which denies the window server, the window
#	manager, the Dock and LaunchServices; a process group of its own and a timeout; README.md). macrun.py applies
#	the profile when ctest runs outside one, and runs the program under the inherited one when ctest itself runs
#	under macrun.py (a gate's long step); without /usr/bin/sandbox-exec every one of these tests fails, unless CI
#	is set. The emulator is NULLDC_EMULATOR, the executable of an emulator build's bundle
#	(<build>/nulldc3.app/Contents/MacOS/nulldc3); the guest is the sounding guest of the tree
#	(tests/debugger/guest/dsptest/dsptest.elf). The cases are in macos.cmake; README.md has what each checks.
#
#	macos.sandbox.canary        canary.c through macrun.py: it cannot reach the window server, and the kernel
#	                            says sandboxed and the window server's lookup denied
#	macos.headless.nowindow     the guest headless with NULLDC_TEST_UI_REPORT=1: app=0 windows=0, exit 0, the
#	                            exit line, the ARM7 on the interpreter
#	macos.fatal.<kind>          NULLDC_TEST_FATAL=<kind> for segv ill trap abort: the fault_handler line (for trap
#	                            die's line and no fault_handler line), 128 + the signal by an exit, not a signal,
#	                            nothing new in ~/Library/Logs/DiagnosticReports
#	macos.refused               a refused command line with stderr to a pipe and to /dev/null: exit 2, no
#	                            NSApplication, the reason on stderr where it goes
#	macos.bundle.codesign       the emulator's bundle: its signature (codesign --verify --strict --deep), its
#	                            Info.plist (plutil -lint, every key of the template, the three version keys
#	                            against the build's nulldc3-version.cmake), the icon and the licences that match
#	                            whether the build embeds the UI
#	macos.icns.iconutil         Apple's iconutil reads nulldc3/apple/nulldc3.icns into an iconset, and makes an
#	                            .icns of the PNGs of nulldc3/icon/png (which elements it writes: the test's output)
#
#	NULLDC_MACRUN, what version.binary (tests/version) starts the emulator through on macOS, is set for the
#	directories behind this one.
#

if(NOT APPLE)
  return()
endif()

enable_language(C)

find_package(Python3 COMPONENTS Interpreter)

set(nulldc_macos_guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")
set(nulldc_macos_script "${CMAKE_CURRENT_SOURCE_DIR}/macos.cmake")

add_executable(macos_canary canary.c)
target_link_libraries(macos_canary PRIVATE "-framework CoreGraphics" "-framework CoreFoundation")

if(Python3_Interpreter_FOUND)
  set(nulldc_macrun "${Python3_EXECUTABLE}" -I -B "${CMAKE_CURRENT_SOURCE_DIR}/macrun.py")
else()
  set(nulldc_macrun "")
endif()

# macos_case(<test> <case> [EMULATOR] [TIMEOUT <s>])
function(macos_case test case)
  cmake_parse_arguments(ARG "EMULATOR" "TIMEOUT" "" ${ARGN})
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 120)
  endif()
  if(NOT nulldc_macrun)
    nulldc_add_missing_data_test(${test} "no python3 to run tests/macos/macrun.py with")
    return()
  endif()
  if(ARG_EMULATOR AND NOT NULLDC_EMULATOR)
    nulldc_add_missing_data_test(${test} "no emulator to run: configure with -DNULLDC_EMULATOR=<build>/nulldc3.app/Contents/MacOS/nulldc3")
    return()
  endif()
  string(REPLACE ";" "|" macrun "${nulldc_macrun}")
  add_test(NAME ${test}
           COMMAND "${CMAKE_COMMAND}" "-DCASE=${case}" "-DMACRUN=${macrun}" "-DEMULATOR=${NULLDC_EMULATOR}"
                   "-DCANARY=$<TARGET_FILE:macos_canary>" "-DGUEST=${nulldc_macos_guest}" "-DTREE=${nulldc_root_path}"
                   "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/work/${test}" -P "${nulldc_macos_script}")
  set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
endfunction()

macos_case(macos.sandbox.canary canary TIMEOUT 60)
macos_case(macos.headless.nowindow headless EMULATOR)
foreach(kind segv ill trap abort)
  macos_case(macos.fatal.${kind} fatal.${kind} EMULATOR)
endforeach()
macos_case(macos.refused refused EMULATOR)
macos_case(macos.bundle.codesign bundle EMULATOR)
macos_case(macos.icns.iconutil icns TIMEOUT 60)

# what version.binary starts the emulator through (tests/version)
string(REPLACE ";" "|" nulldc_macrun_arg "${nulldc_macrun}")
set(NULLDC_MACRUN "${nulldc_macrun_arg}" PARENT_SCOPE)
