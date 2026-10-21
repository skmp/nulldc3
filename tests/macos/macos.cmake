# The cases of tests/macos (CMakeLists.txt of this directory has the list), as a script:
#
#   cmake -DCASE=<case> "-DMACRUN=<python>|<macrun.py>" -DEMULATOR=<nulldc3.app/Contents/MacOS/nulldc3>
#         -DCANARY=<macos_canary> -DGUEST=<dsptest.elf> -DTREE=<the top of the tree> -DWORK=<scratch directory>
#         -P macos.cmake
#
# Every program built in the round is started through macrun.py (the sandbox profile, its own process group, a
# timeout, TMPDIR under WORK), and every emulator run carries P0's keys (a portable directory under WORK,
# config:Headless=yes, audio:backend=none, config:Dreamcast.RTC=946684800); the refused command lines leave
# config:Headless out on purpose, as the alert rule's other conditions are what they check, with a command line
# that is refused before anything runs. macrun.py's last line on stderr says how the program ended; a case fails on
# a timeout (124) and on a program that was not run (126).

cmake_minimum_required(VERSION 3.16)

if(NOT CASE OR NOT MACRUN OR NOT WORK)
  message(FATAL_ERROR "macos.cmake: needs -DCASE= -DMACRUN= -DWORK=")
endif()
string(REPLACE "|" ";" MACRUN "${MACRUN}")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(problems "")
macro(problem text)
  list(APPEND problems "${text}")
endmacro()

# run(<name> [TIMEOUT <s>] [ENV <name=value>...] [NULL] COMMAND <program> <argument>...): through macrun.py;
# <name>_rc, <name>_out, <name>_err, <name>_seconds. With NULL the program's stderr (and macrun.py's) goes to
# /dev/null.
function(run name)
  cmake_parse_arguments(ARG "NULL" "TIMEOUT" "ENV;COMMAND" ${ARGN})
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 90)
  endif()
  foreach(e ${ARG_ENV})
    string(REGEX MATCH "^([^=]+)=(.*)$" _ "${e}")
    set(ENV{${CMAKE_MATCH_1}} "${CMAKE_MATCH_2}")
  endforeach()
  math(EXPR outer "${ARG_TIMEOUT} + 60")
  string(TIMESTAMP t0 "%s")
  set(err "")
  if(ARG_NULL)
    execute_process(COMMAND ${MACRUN} --name ${name} --work "${WORK}" --timeout ${ARG_TIMEOUT} -- ${ARG_COMMAND}
                    WORKING_DIRECTORY "${WORK}" TIMEOUT ${outer} RESULT_VARIABLE rc OUTPUT_VARIABLE out
                    ERROR_FILE /dev/null)
  else()
    execute_process(COMMAND ${MACRUN} --name ${name} --work "${WORK}" --timeout ${ARG_TIMEOUT} -- ${ARG_COMMAND}
                    WORKING_DIRECTORY "${WORK}" TIMEOUT ${outer} RESULT_VARIABLE rc OUTPUT_VARIABLE out
                    ERROR_VARIABLE err)
  endif()
  string(TIMESTAMP t1 "%s")
  foreach(e ${ARG_ENV})
    string(REGEX MATCH "^([^=]+)=" _ "${e}")
    unset(ENV{${CMAKE_MATCH_1}})
  endforeach()
  math(EXPR seconds "${t1} - ${t0}")
  file(WRITE "${WORK}/${name}.log" "exit ${rc} after ${seconds} s\n--- stdout\n${out}\n--- stderr\n${err}\n")
  message("${name}: exit ${rc} after ${seconds} s")
  set(${name}_rc "${rc}" PARENT_SCOPE)
  set(${name}_out "${out}" PARENT_SCOPE)
  set(${name}_err "${err}" PARENT_SCOPE)
  set(${name}_seconds "${seconds}" PARENT_SCOPE)
endfunction()

# How macrun.py says the program ended: <name>_end is "exit <n>", "signal <n>", "timeout", "not run" or ""
macro(macrun_end name)
  set(${name}_end "")
  if("${${name}_err}" MATCHES "macrun: ${name}: exit status ([0-9]+)\n")
    set(${name}_end "exit ${CMAKE_MATCH_1}")
  elseif("${${name}_err}" MATCHES "macrun: ${name}: killed by signal ([0-9]+)")
    set(${name}_end "signal ${CMAKE_MATCH_1}")
  elseif("${${name}_err}" MATCHES "macrun: ${name}: timeout after")
    set(${name}_end "timeout")
  elseif("${${name}_err}" MATCHES "not run")
    set(${name}_end "not run")
  endif()
  if("${${name}_err}" MATCHES "macrun: inside the profile already")
    set(${name}_shape "under the caller's profile")
  elseif("${${name}_err}" MATCHES "macrun: ${name}: under the profile ")
    set(${name}_shape "under the profile macrun.py applied")
  else()
    set(${name}_shape "without a profile")
  endif()
endmacro()

# The run ended by an exit with <code>, under a profile
macro(ended name code)
  macrun_end(${name})
  if(NOT "${${name}_end}" STREQUAL "exit ${code}")
    problem("${name}: macrun.py says \"${${name}_end}\", not \"exit ${code}\" (exit status ${${name}_rc})")
  endif()
  if(NOT "${${name}_rc}" STREQUAL "${code}")
    problem("${name}: exit status ${${name}_rc}, not ${code}")
  endif()
  if("${${name}_shape}" STREQUAL "without a profile")
    problem("${name}: the program ran without a profile")
  endif()
  message("${name}: ${${name}_shape}")
endmacro()

set(keys --portable "${WORK}/portable" --config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800)
file(MAKE_DIRECTORY "${WORK}/portable")
set(guest_run , config:Exit.AfterVblanks=90 , config:Exit.StateHash=yes , config:Dynarec.Enabled=yes "${GUEST}")

# ~/Library/Logs/DiagnosticReports, where macOS keeps a crash report: the names only
function(crash_reports out)
  if(NOT DEFINED ENV{HOME})
    message(FATAL_ERROR "macos.cmake: no HOME, no crash-report directory to look at")
  endif()
  file(GLOB entries LIST_DIRECTORIES true RELATIVE "$ENV{HOME}/Library/Logs/DiagnosticReports"
       "$ENV{HOME}/Library/Logs/DiagnosticReports/*")
  set(${out} "${entries}" PARENT_SCOPE)
endfunction()

set(fault_re "fault_handler: unhandled ([A-Z]+) at address (0x[0-9a-f]+), pc (0x[0-9a-f]+), not a guest memory fault: terminating")

if(CASE STREQUAL "canary")
  # Whether ctest, and this script, run under a profile already, asked of the kernel apart from macrun.py: the
  # Python of MACRUN, a child of this script, is sandboxed exactly when the script is
  list(GET MACRUN 0 python)
  execute_process(COMMAND "${python}" -I -B -c
                  "import ctypes, os; f = ctypes.CDLL('/usr/lib/libSystem.B.dylib').sandbox_check; f.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]; f.restype = ctypes.c_int; print(f(os.getpid(), None, 0))"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE caller ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc EQUAL 0 OR NOT caller MATCHES "^[01]$")
    message(FATAL_ERROR "macos.cmake: the kernel could not be asked whether the caller is sandboxed: ${caller}${err}")
  endif()
  message("the caller (ctest): sandboxed=${caller}")

  run(canary TIMEOUT 30 COMMAND "${CANARY}")
  macrun_end(canary)
  message("canary: ${canary_shape}")
  # macrun.py applies the profile for a caller that has none, and runs the program under the caller's otherwise
  # (macOS 26 takes a second profile inside one as well, so that a profile applied again shows here only)
  if(caller STREQUAL "1" AND NOT canary_shape STREQUAL "under the caller's profile")
    problem("the caller is sandboxed, and macrun.py did not run the canary under the caller's profile (${canary_shape})")
  elseif(caller STREQUAL "0" AND NOT canary_shape STREQUAL "under the profile macrun.py applied")
    problem("the caller is not sandboxed, and macrun.py did not apply the profile (${canary_shape})")
  endif()
  if(NOT canary_out MATCHES "canary: sandboxed=([0-9-]+) windowserver-lookup=([a-z]+) windowserver=([a-z]+)")
    problem("no canary line")
  else()
    message("canary: sandboxed=${CMAKE_MATCH_1} windowserver-lookup=${CMAKE_MATCH_2} windowserver=${CMAKE_MATCH_3}")
    if(NOT CMAKE_MATCH_1 STREQUAL "1")
      problem("the kernel says the canary is not sandboxed (sandboxed=${CMAKE_MATCH_1})")
    endif()
    if(NOT CMAKE_MATCH_2 STREQUAL "denied")
      problem("the kernel does not deny the window server's lookup (${CMAKE_MATCH_2})")
    endif()
    if(NOT CMAKE_MATCH_3 STREQUAL "unreached")
      problem("the canary reached the window server")
    endif()
  endif()
  ended(canary 0)

elseif(CASE STREQUAL "headless")
  run(headless ENV NULLDC_TEST_UI_REPORT=1 COMMAND "${EMULATOR}" ${keys} ${guest_run})
  ended(headless 0)
  if(NOT headless_out MATCHES "\nnulldc-exit: reason=vblanks ")
    problem("no exit line")
  endif()
  if(NOT headless_out MATCHES "\nnulldc-ui: app=([0-9]) windows=([0-9]+)\n")
    problem("no nulldc-ui line")
  else()
    message("nulldc-ui: app=${CMAKE_MATCH_1} windows=${CMAKE_MATCH_2}")
    if(NOT CMAKE_MATCH_1 EQUAL 0)
      problem("app=${CMAKE_MATCH_1}: the run made an NSApplication")
    endif()
    if(NOT CMAKE_MATCH_2 EQUAL 0)
      problem("windows=${CMAKE_MATCH_2}")
    endif()
  endif()
  # Dynarec.ScpuEnabled is on by default; there is no ARM7 recompiler on any host (PL-D46), so the ARM7 runs on the
  # interpreter
  if(NOT headless_out MATCHES "\nUsing SCPU Interpreter\n")
    problem("the ARM7 does not run on the interpreter (no \"Using SCPU Interpreter\")")
  endif()

elseif(CASE MATCHES "^fatal\\.(.+)$")
  set(kind "${CMAKE_MATCH_1}")
  # segv writes to address 16, which no mapping of the process holds: SIGSEGV (139) on macOS as on Linux, with that
  # address in the line (a write to a page mapped read-only or without access is a SIGBUS there). The fault handler
  # first takes it for a possible guest memory fault, through its thunk on the trap page (linux/common.cpp), and
  # has to come back to the faulting write with the registers of the fault: a SIGBUS at the trap page's address is
  # the handler reporting its own thunk
  set(sig_segv SIGSEGV)
  set(addr_segv 0x10)
  set(sig_ill SIGILL)
  set(sig_abort SIGABRT)
  set(code_SIGBUS 138)
  set(code_SIGSEGV 139)
  set(code_SIGILL 132)
  set(code_SIGABRT 134)
  if(kind STREQUAL "trap")
    set(want 133)
  else()
    set(sig "${sig_${kind}}")
    if(NOT sig)
      message(FATAL_ERROR "macos.cmake: no fatal kind ${kind}")
    endif()
    set(want ${code_${sig}})
  endif()

  crash_reports(before)
  run(fatal ENV NULLDC_TEST_FATAL=${kind} TIMEOUT 60 COMMAND "${EMULATOR}" ${keys} ${guest_run})
  # ReportCrash writes its report a moment after the process has ended
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 5)
  crash_reports(after)

  # an exit with 128 + N, not the signal: a process a signal ends leaves a crash report
  ended(fatal ${want})
  if(NOT fatal_err MATCHES "NULLDC_TEST_FATAL: ${kind}\n")
    problem("the hook did not say NULLDC_TEST_FATAL: ${kind} on stderr")
  endif()
  if(kind STREQUAL "trap")
    if(NOT fatal_out MATCHES "Fatal error : NULLDC_TEST_FATAL")
      problem("no \"Fatal error : NULLDC_TEST_FATAL\" on stdout")
    endif()
    if(fatal_out MATCHES "fault_handler" OR fatal_err MATCHES "fault_handler")
      problem("a fault_handler line after die()")
    endif()
  else()
    if(NOT fatal_err MATCHES "${fault_re}")
      problem("no fault_handler line on stderr")
    else()
      set(line_sig "${CMAKE_MATCH_1}")
      set(line_addr "${CMAKE_MATCH_2}")
      set(line_pc "${CMAKE_MATCH_3}")
      message("the fault_handler line: ${line_sig} at ${line_addr}, pc ${line_pc}")
      if(NOT line_sig STREQUAL sig)
        problem("the fault_handler line says ${line_sig}, not ${sig}")
      endif()
      if(DEFINED addr_${kind} AND NOT line_addr STREQUAL addr_${kind})
        problem("the fault_handler line says address ${line_addr}, not ${addr_${kind}}")
      endif()
      # an illegal instruction's address is the instruction, the pc the line names
      if(kind STREQUAL "ill" AND NOT line_addr STREQUAL line_pc)
        problem("the fault_handler line of ill says address ${line_addr} and pc ${line_pc}, not the instruction twice")
      endif()
    endif()
  endif()
  set(new "")
  foreach(entry ${after})
    if(NOT entry IN_LIST before)
      list(APPEND new "${entry}")
    endif()
  endforeach()
  if(new)
    problem("new in ~/Library/Logs/DiagnosticReports: ${new}")
  endif()

elseif(CASE STREQUAL "refused")
  # Not headless, and refused before anything runs: --bogus. The alert is for a start by LaunchServices whose stderr
  # goes nowhere; macrun.py's program is no such start (it takes __CFBundleIdentifier out, its parent is
  # macrun.py), so there is never one here, whatever stderr is
  set(nohead --portable "${WORK}/portable" --config audio:backend=none , config:Dreamcast.RTC=946684800 --bogus)

  # (1) stderr to a pipe: the reason there
  run(pipe ENV NULLDC_TEST_UI_REPORT=1 TIMEOUT 30 COMMAND "${EMULATOR}" ${nohead})
  ended(pipe 2)
  if(NOT pipe_err MATCHES "unknown switch --bogus")
    problem("pipe: the reason is not on stderr")
  endif()
  if(NOT pipe_out MATCHES "nulldc-ui: app=0 windows=0\n")
    problem("pipe: no \"nulldc-ui: app=0 windows=0\" (an NSApplication was made, or no report)")
  endif()

  # (2) stderr to /dev/null: still no alert, exit 2 at once
  run(devnull NULL ENV NULLDC_TEST_UI_REPORT=1 TIMEOUT 30 COMMAND "${EMULATOR}" ${nohead})
  if(NOT devnull_rc EQUAL 2)
    problem("/dev/null: exit ${devnull_rc}, not 2")
  endif()
  if(NOT devnull_out MATCHES "nulldc-ui: app=0 windows=0\n")
    problem("/dev/null: no \"nulldc-ui: app=0 windows=0\" (an NSApplication was made, or no report)")
  endif()
  if(devnull_seconds GREATER 10)
    problem("/dev/null: ${devnull_seconds} s")
  endif()

  # (3) headless (P0's keys), stderr to /dev/null: the same
  run(headless NULL ENV NULLDC_TEST_UI_REPORT=1 TIMEOUT 30 COMMAND "${EMULATOR}" ${keys} --bogus)
  if(NOT headless_rc EQUAL 2)
    problem("headless, /dev/null: exit ${headless_rc}, not 2")
  endif()
  if(NOT headless_out MATCHES "nulldc-ui: app=0 windows=0\n")
    problem("headless, /dev/null: no \"nulldc-ui: app=0 windows=0\"")
  endif()

elseif(CASE STREQUAL "bundle")
  # The bundle of the emulator: EMULATOR is <build>/nulldc3.app/Contents/MacOS/nulldc3
  get_filename_component(macos_dir "${EMULATOR}" DIRECTORY)
  get_filename_component(contents "${macos_dir}" DIRECTORY)
  get_filename_component(bundle "${contents}" DIRECTORY)
  get_filename_component(build "${bundle}" DIRECTORY)
  get_filename_component(bundle_name "${bundle}" NAME)
  if(NOT bundle_name STREQUAL "nulldc3.app" OR NOT EXISTS "${contents}/Info.plist")
    message(FATAL_ERROR "macos.cmake: ${EMULATOR} is not the executable of a bundle nulldc3.app")
  endif()
  set(plist "${contents}/Info.plist")
  message("bundle: ${bundle}")

  execute_process(COMMAND codesign --verify --strict --deep --verbose=2 "${bundle}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  message("codesign --verify --strict --deep: exit ${rc}: ${out}${err}")
  if(NOT rc EQUAL 0)
    problem("codesign --verify --strict --deep: exit ${rc}: ${err}")
  endif()
  execute_process(COMMAND codesign --display --verbose=2 "${bundle}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT err MATCHES "Signature=adhoc")
    problem("the signature is not ad hoc")
  endif()
  if(NOT err MATCHES "Identifier=org.emudev.nulldc3\n")
    problem("the signature's identifier is not org.emudev.nulldc3")
  endif()
  if(err MATCHES "flags=[^\n]*runtime")
    problem("the signature has the hardened runtime")
  endif()

  execute_process(COMMAND plutil -lint "${plist}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    problem("plutil -lint: ${out}${err}")
  endif()

  # the version the build made, nulldc3/cmake/version.cmake
  if(NOT EXISTS "${build}/nulldc3-version.cmake")
    problem("no ${build}/nulldc3-version.cmake beside the bundle")
  else()
    include("${build}/nulldc3-version.cmake")
  endif()

  # every key of nulldc3/apple/Info.plist.in, and the three the build writes
  set(keys_strings
    "CFBundleDevelopmentRegion=en"
    "CFBundleDisplayName=nullDC"
    "CFBundleExecutable=nulldc3"
    "CFBundleIconFile=nulldc3"
    "CFBundleIdentifier=org.emudev.nulldc3"
    "CFBundleInfoDictionaryVersion=6.0"
    "CFBundleName=nulldc3"
    "CFBundlePackageType=APPL"
    "LSMinimumSystemVersion=11.0"
    "NSHumanReadableCopyright=Copyright © 2026 nullDC Team"
    "NSPrincipalClass=NSApplication"
    "CFBundleShortVersionString=${NULLDC_VERSION_SHORT}"
    "CFBundleVersion=${NULLDC_VERSION_DOTTED}"
    "NullDCVersion=${NULLDC_VERSION}")
  foreach(entry ${keys_strings})
    string(REGEX MATCH "^([^=]+)=(.*)$" _ "${entry}")
    set(key "${CMAKE_MATCH_1}")
    set(want "${CMAKE_MATCH_2}")
    execute_process(COMMAND plutil -extract "${key}" raw -o - "${plist}"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE value ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0)
      problem("Info.plist has no ${key}")
    elseif(NOT value STREQUAL want)
      problem("Info.plist: ${key} is \"${value}\", not \"${want}\"")
    endif()
    message("Info.plist: ${key} = ${value}")
  endforeach()
  execute_process(COMMAND plutil -extract NSHighResolutionCapable raw -o - "${plist}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE value OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT value STREQUAL "true")
    problem("Info.plist: NSHighResolutionCapable is \"${value}\", not true")
  endif()
  execute_process(COMMAND plutil -convert json -o - "${plist}" RESULT_VARIABLE rc OUTPUT_VARIABLE json ERROR_QUIET)
  string(JSON nkeys LENGTH "${json}")
  list(LENGTH keys_strings nwant)
  math(EXPR nwant "${nwant} + 2")	# NSHighResolutionCapable and CFBundleSignature
  if(NOT nkeys EQUAL nwant)
    problem("Info.plist has ${nkeys} keys, not ${nwant}")
  endif()

  # Resources: the icon, and the licences of the release (PL-D80): LICENSE-devtools only when the build embeds the
  # UI. Whether it does is in the build's dbg_webui_data.cpp.args (libswirl/debugger/cmake/dbg_webui.cmake): the
  # directory it embeds, its first line, empty for none; no such file, no debugger and so no UI
  set(embed "")
  if(EXISTS "${build}/dbg_webui_data.cpp.args")
    file(STRINGS "${build}/dbg_webui_data.cpp.args" args)
    list(GET args 0 embed)
  endif()
  set(want_resources LICENSE LICENSE-GPL LICENSE-LGPL LICENSE-shell nulldc3.icns)
  set(source_LICENSE "${TREE}/LICENSE")
  set(source_LICENSE-GPL "${TREE}/LICENSE-GPL")
  set(source_LICENSE-LGPL "${TREE}/LICENSE-LGPL")
  set(source_LICENSE-shell "${TREE}/nulldc3/LICENSE")
  set(source_nulldc3.icns "${TREE}/nulldc3/apple/nulldc3.icns")
  if(embed)
    list(APPEND want_resources LICENSE-devtools)
    set(source_LICENSE-devtools "${TREE}/devtools/LICENSE")
    message("the build embeds the UI (${embed}): five licences")
  else()
    message("the build embeds no UI: four licences")
  endif()
  list(SORT want_resources)
  file(GLOB have_resources LIST_DIRECTORIES true RELATIVE "${contents}/Resources" "${contents}/Resources/*")
  list(SORT have_resources)
  message("Resources: ${have_resources}")
  if(NOT have_resources STREQUAL want_resources)
    problem("Resources has ${have_resources}, not ${want_resources}")
  endif()
  foreach(f ${want_resources})
    if(EXISTS "${contents}/Resources/${f}" AND EXISTS "${source_${f}}")
      file(SHA256 "${contents}/Resources/${f}" a)
      file(SHA256 "${source_${f}}" b)
      if(NOT a STREQUAL b)
        problem("Resources/${f} is not ${source_${f}}")
      endif()
    endif()
  endforeach()

  # nothing else in the bundle: the executable, the plist, Resources and the signature
  file(GLOB_RECURSE all RELATIVE "${bundle}" "${bundle}/*")
  list(SORT all)
  set(want_all "Contents/Info.plist" "Contents/MacOS/nulldc3" "Contents/_CodeSignature/CodeResources")
  foreach(f ${want_resources})
    list(APPEND want_all "Contents/Resources/${f}")
  endforeach()
  list(SORT want_all)
  if(NOT all STREQUAL want_all)
    problem("the bundle has ${all}, not ${want_all}")
  endif()

elseif(CASE STREQUAL "icns")
  # (1) Apple's reader takes the generated .icns: iconutil -c iconset, every image there at its size
  set(icns "${TREE}/nulldc3/apple/nulldc3.icns")
  execute_process(COMMAND iconutil -c iconset -o "${WORK}/nulldc3.iconset" "${icns}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  message("iconutil -c iconset nulldc3.icns: exit ${rc} ${out}${err}")
  if(NOT rc EQUAL 0)
    problem("iconutil -c iconset refuses nulldc3.icns: ${out}${err}")
  endif()
  set(images "icon_16x16.png=16" "icon_16x16@2x.png=32" "icon_32x32.png=32" "icon_32x32@2x.png=64"
             "icon_128x128.png=128" "icon_128x128@2x.png=256" "icon_256x256.png=256" "icon_256x256@2x.png=512"
             "icon_512x512.png=512" "icon_512x512@2x.png=1024")
  file(GLOB got RELATIVE "${WORK}/nulldc3.iconset" "${WORK}/nulldc3.iconset/*")
  list(SORT got)
  message("the iconset of nulldc3.icns: ${got}")
  set(want_names "")
  foreach(entry ${images})
    string(REGEX MATCH "^([^=]+)=(.*)$" _ "${entry}")
    set(f "${CMAKE_MATCH_1}")
    set(size "${CMAKE_MATCH_2}")
    list(APPEND want_names "${f}")
    if(NOT EXISTS "${WORK}/nulldc3.iconset/${f}")
      problem("the iconset of nulldc3.icns has no ${f}")
      continue()
    endif()
    # the PNG's IHDR: width and height, big-endian, at offset 16
    file(READ "${WORK}/nulldc3.iconset/${f}" ihdr OFFSET 16 LIMIT 8 HEX)
    string(SUBSTRING "${ihdr}" 0 8 w)
    string(SUBSTRING "${ihdr}" 8 8 h)
    math(EXPR w "0x${w}")
    math(EXPR h "0x${h}")
    if(NOT w EQUAL size OR NOT h EQUAL size)
      problem("${f} is ${w}x${h}, not ${size}x${size}")
    endif()
  endforeach()
  list(SORT want_names)
  if(NOT got STREQUAL want_names)
    problem("the iconset of nulldc3.icns has ${got}, not ${want_names}")
  endif()

  # (2) which elements Apple's writer makes of the same PNGs: iconutil -c icns of an iconset of nulldc3/icon/png
  set(png "${TREE}/nulldc3/icon/png")
  file(MAKE_DIRECTORY "${WORK}/scratch.iconset")
  foreach(entry ${images})
    string(REGEX MATCH "^([^=]+)=(.*)$" _ "${entry}")
    file(COPY_FILE "${png}/nulldc-${CMAKE_MATCH_2}.png" "${WORK}/scratch.iconset/${CMAKE_MATCH_1}")
  endforeach()
  execute_process(COMMAND iconutil -c icns -o "${WORK}/scratch.icns" "${WORK}/scratch.iconset"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    problem("iconutil -c icns of the PNGs: exit ${rc} ${out}${err}")
  else()
    file(READ "${WORK}/scratch.icns" bytes HEX)
    string(LENGTH "${bytes}" n)
    set(at 16)	# behind 'icns' and its length
    set(types "")
    while(at LESS n)
      string(SUBSTRING "${bytes}" ${at} 8 type_hex)
      math(EXPR len_at "${at} + 8")
      string(SUBSTRING "${bytes}" ${len_at} 8 len_hex)
      math(EXPR len "0x${len_hex}")
      set(type "")
      foreach(k 0 2 4 6)
        string(SUBSTRING "${type_hex}" ${k} 2 c)
        math(EXPR c "0x${c}")
        string(ASCII ${c} ch)
        string(APPEND type "${ch}")
      endforeach()
      list(APPEND types "${type}")
      if(len LESS 8)
        problem("scratch.icns: an element of length ${len}")
        break()
      endif()
      math(EXPR at "${at} + ${len} * 2")
    endwhile()
    message("the elements iconutil writes for the PNGs: ${types}")
  endif()

else()
  message(FATAL_ERROR "macos.cmake: no case ${CASE}")
endif()

if(problems)
  string(REPLACE ";" "\n  " problems "${problems}")
  message(FATAL_ERROR "${CASE}:\n  ${problems}\n(the runs: ${WORK}/*.log)")
endif()
message("${CASE}: passed")
