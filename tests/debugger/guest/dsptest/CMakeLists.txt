## The sounding guest, and the sound lines ##
#
#	dsptest.elf is the guest of this directory (README.md): a bare-metal program for the SH4 and the ARM7 that
#	plays two channels, whose ARM7 state differs behind every sample and whose DSP runs a program that is heard.
#	soundlines.py runs the emulator on it,
#	or on another program, with the sound regression harness (--config config:Debug.AicaHash=<N>), headless, on
#	the emulator the system tests build (tests/debugger/guest/CMakeLists.txt), and says what the lines have to
#	agree on. No line is stored in the tree: a test makes its runs itself.
#
#	    debugger.guest.dsptest.files                        the committed files are those of the manifest, and
#	                                                        what gen.sh makes of the sources (where the assemblers
#	                                                        are; on Windows gen.sh, a bash script, is not run, and
#	                                                        the test has to say so)
#	    debugger.sound.lines.dsptest.<jit|interp>           two runs of 2000 ticks print one line, with ticks=2000,
#	                                                        and it is not a line of silence
#	    debugger.sound.engines.dsptest                      the recompiler and the interpreter agree in samples,
#	                                                        regs, arm7, ticks and cycles, and in the line's dsp
#	                                                        field, the whole state of the DSP at every tick
#	    debugger.sound.release.dsptest.<jit|interp>         a release of the ARM7 that finds a request of the AICA
#	                                                        latched starts the program of the run without one
#	    debugger.sound.held.dsptest.<jit|interp>            a held ARM7 is an idle one for the rest of the AICA: a
#	                                                        copy of the guest whose ARM7 stays held and one whose
#	                                                        ARM7 idles on a branch to itself print one line in
#	                                                        every field but arm7
#	    debugger.sound.heard.dsptest.<jit|interp>           the guest's DSP is heard: a copy of the guest whose
#	                                                        outputs of the DSP have EFSDL 0 prints its line in
#	                                                        arm7, sh4, dsp, ticks and cycles, and other samples
#	    debugger.sound.lines.2ndmix.<jit|interp>            two runs of 40000 ticks of 2ndmix.elf print one line,
#	                                                        and it is the silence of 40000 ticks, 32 pairs of
#	                                                        zeroes for each (needs NULLDC_ELF_DIR; that program is
#	                                                        silent here)
#	    debugger.sound.muted.dsptest.<jit|interp>           "Disable Sound" (config:aica.NoSound=yes) mutes: the
#	                                                        line of the run without the key in every field but
#	                                                        samples, which is the hash of 2000 * 32 pairs of
#	                                                        zeroes (the harness's fold; the audio stream itself is
#	                                                        held by aica.dsp.emu.state)
#	    debugger.sound.ignored.dsptest                      config:aica.NoBatch, which an older build read and
#	                                                        wrote into every configuration file, changes nothing:
#	                                                        a run with it, no or yes, prints the line without it
#	    debugger.sound.names                                no test of the sound or of the session arm7 of the
#	                                                        selftests is named after a render of the sound
#	                                                        (.batch, .persample): there is one way to make the
#	                                                        sound
#	    debugger.sound.runner                               soundlines.py fails a run that was not made on the SH4
#	                                                        engine it was asked for, its mode names fails those
#	                                                        names and no other test's, its modes held, heard and
#	                                                        oracle check their copies and their fields on copies of
#	                                                        dsptest.elf, its mode silent the k of the silence, and
#	                                                        its mode release the empty end of the DSP part of the
#	                                                        table (soundlines_test.py, on stand-ins for the
#	                                                        emulator, whose line depends on the guest, for the
#	                                                        oracle and for a build: no emulator is run)
#
#	debugger.sound.oracle.dsptest.<jit|interp>, the mode oracle (the DSP of a copy of the guest whose sends are 0
#	against the caique model), is registered in tests/aica_dsp, where the tool it needs is built.
#
#	Every run of these tests has to say that it was made on the engine it was asked for. An emulator without a
#	recompiler (-DNULLDC_EMULATOR= with a TARGET_NO_REC build, or a generic host) runs config:Dynarec.Enabled=yes
#	on its interpreter: its .jit. and engines tests fail, as system.guest_exit.jit and system.boot.*.jit do.
#
#	For the directories behind this one:
#
#	    nulldc_sound_neutral(<name> SELFTEST <filter> [EXPECT <text>...] [CONFIG <section:key=value>...]
#	                         [TIMEOUT <seconds>])
#
#	makes debugger.sound.neutral.<name>.<jit|interp>: the mode neutral of soundlines.py on the
#	guest, with N = 2000 and V = 90. The debugger at work (the session <filter> of the selftests, whose test
#	never returns true) leaves the sound line and the state hash at 90 vblanks as they are without it, and says
#	every EXPECT text in a line of its own.
#

set(nulldc_sound_ticks 2000)
set(nulldc_sound_ticks_2ndmix 40000)

# The function runs in the scope of its caller, a sibling of this directory: it uses what
# tests/debugger/guest/CMakeLists.txt sets (Python3_EXECUTABLE, nulldc_guest_emulator, nulldc_guest_skip,
# nulldc_guest_env), nulldc_tests_path and nothing this file sets
function(nulldc_sound_neutral name)
  cmake_parse_arguments(ARG "" "SELFTEST;TIMEOUT" "EXPECT;CONFIG" ${ARGN})

  if(NOT ARG_SELFTEST)
    message(FATAL_ERROR "nulldc_sound_neutral(${name}): no SELFTEST")
  endif()
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 900)
  endif()

  foreach(engine jit interp)
    set(test debugger.sound.neutral.${name}.${engine})

    if(nulldc_guest_skip)
      nulldc_add_skipped_test(${test} "${nulldc_guest_skip}")
      continue()
    endif()

    set(command "${Python3_EXECUTABLE}" "${nulldc_tests_path}/debugger/guest/dsptest/soundlines.py"
                --emulator "${nulldc_guest_emulator}" --work "${CMAKE_BINARY_DIR}/debugger/guest/work/${test}"
                --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf"
                --mode neutral --ticks 2000 --vblanks 90 --engine ${engine}
                --selftest ${ARG_SELFTEST})
    foreach(text ${ARG_EXPECT})
      list(APPEND command --expect "${text}")
    endforeach()
    foreach(item ${ARG_CONFIG})
      list(APPEND command --config ${item})
    endforeach()

    add_test(NAME ${test} COMMAND ${command})
    # soundlines.py has a watchdog per run of the emulator, this is the one behind it
    set_tests_properties(${test} PROPERTIES TIMEOUT ${ARG_TIMEOUT} ENVIRONMENT "${nulldc_guest_env}")
  endforeach()
endfunction()


set(nulldc_sound_runner "${CMAKE_CURRENT_SOURCE_DIR}/soundlines.py")

# The manifest needs python and nothing else; gen.sh --check runs where bash and the assemblers are. On Windows
# bash is WSL's launcher, which would run gen.sh inside a Linux of the computer: there the mode looks for no shell,
# says so, and fails when it looks for one
if(Python3_Interpreter_FOUND)
  set(nulldc_files_host "")
  if(${HOST_OS} EQUAL ${OS_WINDOWS})
    set(nulldc_files_host --no-posix-shell)
  endif()
  add_test(NAME debugger.guest.dsptest.files
           COMMAND "${Python3_EXECUTABLE}" "${nulldc_sound_runner}" --mode files --dir "${CMAKE_CURRENT_SOURCE_DIR}" ${nulldc_files_host})
  set_tests_properties(debugger.guest.dsptest.files PROPERTIES TIMEOUT 120 ENVIRONMENT "${nulldc_guest_env}")
else()
  nulldc_add_skipped_test(debugger.guest.dsptest.files "no python3 to run tests/debugger/guest/dsptest/soundlines.py with")
endif()

# The runner against stand-ins for the emulator: python, and a host that runs a script by its first line
if(Python3_Interpreter_FOUND AND ${HOST_OS} EQUAL ${OS_LINUX})
  add_test(NAME debugger.sound.runner
           COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/soundlines_test.py"
                   --work "${CMAKE_BINARY_DIR}/debugger/guest/work/debugger.sound.runner")
  set_tests_properties(debugger.sound.runner PROPERTIES TIMEOUT 300 ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
elseif(NOT Python3_Interpreter_FOUND)
  nulldc_add_skipped_test(debugger.sound.runner "no python3 to run tests/debugger/guest/dsptest/soundlines_test.py with")
else()
  nulldc_add_skipped_test(debugger.sound.runner
    "the stand-ins of tests/debugger/guest/dsptest/soundlines_test.py for the emulator are scripts that the host runs by their first line (#!): a Linux host")
endif()

# nulldc_sound_test(<test> <program> <ticks> <arguments of soundlines.py>...)
function(nulldc_sound_test test program ticks)
  if(nulldc_guest_skip)
    nulldc_add_skipped_test(${test} "${nulldc_guest_skip}")
    return()
  endif()

  add_test(NAME ${test}
           COMMAND "${Python3_EXECUTABLE}" "${nulldc_sound_runner}" --emulator "${nulldc_guest_emulator}"
                   --work "${CMAKE_BINARY_DIR}/debugger/guest/work/${test}" --guest "${program}" --ticks ${ticks} ${ARGN})
  set_tests_properties(${test} PROPERTIES TIMEOUT 900 ENVIRONMENT "${nulldc_guest_env}")
endfunction()

set(nulldc_sound_no_2ndmix "")
if(NOT NULLDC_ELF_DIR OR NOT EXISTS "${NULLDC_ELF_DIR}/2ndmix.elf")
  set(nulldc_sound_no_2ndmix
      "no 2ndmix.elf in NULLDC_ELF_DIR (\"${NULLDC_ELF_DIR}\"), configure with -DNULLDC_ELF_DIR=<directory with the KallistiOS examples>")
endif()

foreach(engine jit interp)
  nulldc_sound_test(debugger.sound.lines.dsptest.${engine} "${CMAKE_CURRENT_SOURCE_DIR}/dsptest.elf" ${nulldc_sound_ticks}
                    --mode same --mode sounds --engine ${engine})

  # g_hold_arm7 and g_release_arm7 are there for tests, which hold the ARM7 at a tick of their choice: a_reset
  # starts the same program whatever request the AICA has latched for the ARM7 by then
  nulldc_sound_test(debugger.sound.release.dsptest.${engine} "${CMAKE_CURRENT_SOURCE_DIR}/dsptest.elf" ${nulldc_sound_ticks}
                    --mode release --fields samples,regs,arm7,ticks,cycles --engine ${engine})

  # The AICA is clocked whatever ARMRST says, and only the ARM7 is held by it: a copy of the guest whose last release
  # of the ARM7 is a hold and a copy whose ARM7 stays on a branch to itself, both built in the work directory, agree
  # in samples, regs, sh4, dsp, ticks and cycles; the held one sounds, and the idle one is not the guest
  nulldc_sound_test(debugger.sound.held.dsptest.${engine} "${CMAKE_CURRENT_SOURCE_DIR}/dsptest.elf" ${nulldc_sound_ticks}
                    --mode held --engine ${engine})

  # The guest's DSP is heard: a copy of the guest whose outputs of the DSP have EFSDL 0 prints the guest's line but
  # for samples (and regs, the register file has the levels): the mix takes EFREG from the DSP
  nulldc_sound_test(debugger.sound.heard.dsptest.${engine} "${CMAKE_CURRENT_SOURCE_DIR}/dsptest.elf" ${nulldc_sound_ticks}
                    --mode heard --engine ${engine})

  # 2ndmix stays in the matrix: a program of another kind (KallistiOS, a player that waits for a timer). Its
  # ARM7 gets no interrupt under reios, so it plays nothing and has no "sounds"; its samples are the silence of
  # N ticks, every tick 32 pairs of zeroes, also while it holds its ARM7
  if(nulldc_sound_no_2ndmix AND NOT nulldc_guest_skip)
    nulldc_add_missing_data_test(debugger.sound.lines.2ndmix.${engine} "${nulldc_sound_no_2ndmix}")
  else()
    nulldc_sound_test(debugger.sound.lines.2ndmix.${engine} "${NULLDC_ELF_DIR}/2ndmix.elf" ${nulldc_sound_ticks_2ndmix}
                      --mode same --mode silent --engine ${engine})
  endif()

  # "Disable Sound" mutes: the samples are made, and the harness folds a pair of zeroes for each, the pair the
  # audio stream is handed (every burst makes its 32 samples, whether it finds the ARM7 clocked or held, so a tick
  # is 32 such pairs; the stream itself, which keeps the speed limit going, is held by aica.dsp.emu.state). The run
  # without the key has to sound, or its silence would say nothing
  nulldc_sound_test(debugger.sound.muted.dsptest.${engine} "${CMAKE_CURRENT_SOURCE_DIR}/dsptest.elf" ${nulldc_sound_ticks}
                    --mode sounds --mode muted --engine ${engine})
endforeach()

# The guest is built so that the two engines agree in every field but sh4
nulldc_sound_test(debugger.sound.engines.dsptest "${CMAKE_CURRENT_SOURCE_DIR}/dsptest.elf" ${nulldc_sound_ticks}
                  --mode engines --fields samples,regs,arm7,ticks,cycles)

# An older build read config:aica.NoBatch (the sound in batches of 32 samples, without the DSP, or sample by sample)
# and wrote it into every configuration file it saved: this one reads it nowhere. One engine: the key is no
# business of the SH4
nulldc_sound_test(debugger.sound.ignored.dsptest "${CMAKE_CURRENT_SOURCE_DIR}/dsptest.elf" ${nulldc_sound_ticks}
                  --mode ignored --ignored config:aica.NoBatch=no --ignored config:aica.NoBatch=yes --engine jit)

# The names of the tests of this build, from its CTestTestfile.cmake files: python and nothing else
if(Python3_Interpreter_FOUND)
  add_test(NAME debugger.sound.names
           COMMAND "${Python3_EXECUTABLE}" "${nulldc_sound_runner}" --mode names --build "${CMAKE_BINARY_DIR}")
  set_tests_properties(debugger.sound.names PROPERTIES TIMEOUT 120 ENVIRONMENT "${nulldc_guest_env}")
else()
  nulldc_add_skipped_test(debugger.sound.names "no python3 to run tests/debugger/guest/dsptest/soundlines.py with")
endif()
