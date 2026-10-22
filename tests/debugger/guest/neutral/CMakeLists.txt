## The debugger at work on the ARM7, the DSP and the SGC of the sounding guest: the run is the run nobody touched ##
#
#	Twenty tests, debugger.sound.neutral.<idlebp|hitresume|steps|hoststop>.<jit|interp> on the ARM7,
#	debugger.sound.neutral.dsp.<loop|hitresume|steps|hoststop>.<jit|interp> on the DSP and
#	debugger.sound.neutral.sgc.<running|paused>.<jit|interp> on the SGC capture, made by nulldc_sound_neutral of
#	../dsptest/CMakeLists.txt: the mode neutral of ../dsptest/soundlines.py on a selftest session whose one test
#	never returns (libswirl/debugger/dbg_selftest_arm7.cpp, arm7n.*; libswirl/debugger/dbg_selftest_dsp.cpp,
#	dspn.*; libswirl/debugger/dbg_selftest_sgc.cpp, sgcn.*). Each makes four runs of dsptest.elf and holds two things
#	against the run without the session: the sound line at 2000 ticks, whole, and the state hash at 90 vblanks,
#	part for part. The line has no field for wave memory, main memory or the scheduler; the hash has. The line's
#	dsp field is the state of the DSP core, folded at every tick.
#
#	Both hold whole because the SH4 of the guest does not sleep, and because every test takes its stops early in
#	the run. A stop that is latched at a slice end and resumed leaves no trace outside a SLEEP wait. A run that
#	ends in the very tick in which a stop is latched - a check of the ARM7 that suspends the burst, an event
#	breakpoint - is another matter: the burst is whole in front of the stop callback that prints the exit line,
#	but the interrupt acceptance of the SH4 that the latch held back is made behind that callback (dc_run of
#	libswirl/libswirl.cpp), so with an interrupt pending there the `sh4` part of the line, and its `hash`, are
#	not those of the plain run. The tests have done their work within some hundreds of sound bursts (idlebp
#	takes its one stop some 900 bursts in); the line is taken at 2000 and the hash some 2070 bursts in. A test
#	that is added here keeps its stops as far from the end of the run.
#
#	On an arm64 host the .jit tests that nulldc_a64_known_limits() of ../../selftest/CMakeLists.txt lists are known
#	limits of the arm64 recompiler (KNOWN LIMIT (A64), NOT RUN), counted as skipped; their .interp twins run.
#	debugger.a64.knownlimits, at the end of this file, checks that registration on every host.
#
#	A test that armed nothing would pass both, so each says what it did in lines that its target requires
#	(EXPECT), and fails a check of its own when it did less:
#
#	    idlebp      an active breakpoint of the ARM7 on an address the guest never executes: the debug loop ran
#	                every ARM phase of the run, at least 32 checks for every burst the scheduler started
#	    hitresume   200 stops at breakpoints of the ARM7, each resumed: on the loop and on the FIQ handler, at
#	                least 50 each, in at least 100 different bursts
#	    steps       single steps of the ARM7, in runs that are followed by a resume, until each of four kinds
#	                was taken often enough: from a stop with no burst suspended, across a time step of the AICA,
#	                across the end of a burst (20 each), and of the msr that ends the F window of the guest's
#	                loop, ending on the FIQ vector inside one ARM phase (5)
#	    hoststop    sixty times the host stops the machine while the thread is parked on a burst that a
#	                breakpoint of the ARM7 suspended, on thirteen addresses in turn, and starts it again: the
#	                thread that ends runs the rest of the burst in front of its stop callback (dc_run), from
#	                inside a sample. The sessions of debugger.selftest.arm7* hold that path to its counts; what
#	                the rest of the burst executed is held here. At least five of the stops are taken where
#	                the rest of the sample has a FIQ to enter (on the msr that ends the F window of the
#	                guest's loop, or in front of it, with a request of the AICA latched): that loop enters it
#
#	On the DSP, whose 128 steps execute in every sample whatever the ARM7 does (a listed breakpoint of it is
#	reached in every sample, so there is no idle one):
#
#	    dsp.loop       the debug loop of the DSP forced for the whole run with nothing listed (the test hook of
#	                   dbg_dsp_private.h): every DSP phase of the run goes through it, a check in front of every
#	                   step, by the DSP counts of the machine (SoundBurstDspCounts)
#	    dsp.hitresume  at least 200 stops at breakpoints of the DSP, each resumed, in at least 100 bursts, at at
#	                   least 8 different steps: each breakpoint is hit in the first sample of its burst and, through
#	                   the exemption, one sample later
#	    dsp.steps      single steps of the DSP until each of five kinds was taken at least 20 times: from a stop
#	                   with no burst suspended, inside a DSP phase, of step 127 across the end of a sample, of step
#	                   127 across the end of a burst, and from a stop of the ARM7
#	    dsp.hoststop   sixty times the host stops the machine while the thread is parked on a burst suspended in
#	                   front of a DSP step (at least 20) or at the end of a sample (at least 5), and starts it
#	                   again: the thread that ends runs the rest of the burst in front of its stop callback (dc_run)
#
#	The SGC capture only reads the guest, while the machine runs and from a stop; its stop at the end of a sample
#	is a stop like every other:
#
#	    sgc.running    captures of 1 to 4096 frames while the machine runs, one behind the other, each armed in a
#	                   table call (a service excursion) and complete with its frames, none stopping the run
#	    sgc.paused     captures armed at stops, each resumed, until each kind was taken ten times: behind a burst,
#	                   in front of an ARM7 instruction, in front of a DSP step, and at the end of a sample (the
#	                   stop of the capture before); each stops behind its last frame with its frames
#
#	README.md of ../dsptest has the guest; tests/debugger/selftest/README.md has the tests of the session.
#

nulldc_sound_neutral(idlebp    SELFTEST arm7n.idlebp
                     EXPECT "the debug loop ran every sample")

nulldc_sound_neutral(hitresume SELFTEST arm7n.hitresume
                     EXPECT "stops at breakpoints of the ARM7, each resumed")

nulldc_sound_neutral(steps     SELFTEST arm7n.steps
                     EXPECT "steps from a stop with no burst suspended"
                            "steps across a time step of the AICA"
                            "steps across the end of a burst"
                            "steps of the msr that ends the F window which ended on the FIQ vector inside one ARM phase")

nulldc_sound_neutral(hoststop  SELFTEST arm7n.hoststop
                     EXPECT "the host stopped the machine 60 times while the thread was parked on a burst"
                            "each of the 60 stop callbacks found the burst whole"
                            "of the stops of the host were taken where the rest of the sample had a FIQ to enter")

nulldc_sound_neutral(dsp.loop      SELFTEST dspn.loop
                     EXPECT "every DSP phase of the run went through the debug loop")

nulldc_sound_neutral(dsp.hitresume SELFTEST dspn.hitresume
                     EXPECT "stops at breakpoints of the DSP, each resumed")

nulldc_sound_neutral(dsp.steps     SELFTEST dspn.steps
                     EXPECT "steps from a stop with no burst suspended"
                            "steps inside a DSP phase"
                            "steps of step 127 across the end of a sample"
                            "steps of step 127 across the end of a burst"
                            "steps from a stop of the ARM7")

nulldc_sound_neutral(dsp.hoststop  SELFTEST dspn.hoststop
                     EXPECT "the host stopped the machine 60 times while the thread was parked on a suspended burst"
                            "each of the 60 stop callbacks found the burst whole")

nulldc_sound_neutral(sgc.running   SELFTEST sgcn.running
                     EXPECT "captures while running, of 1 to 4096 frames, each complete with its frames, and none stopped the run")

nulldc_sound_neutral(sgc.paused    SELFTEST sgcn.paused
                     EXPECT "captures armed at stops, each resumed"
                            "each stopped behind its last frame with its frames and its time steps")


## debugger.a64.knownlimits: the registration of the arm64 recompiler's known limits ##
#
#	Here, behind the last of the targets nulldc_a64_known_limits() lists (../../selftest/CMakeLists.txt), the
#	configure checks what nulldc_debugger_selftest and nulldc_sound_neutral registered, and the test says what it
#	found: "PASS: ..." or "FAIL: ..." (a failure). It decides by itself whether this is an arm64 host, from
#	CMAKE_SYSTEM_PROCESSOR (aarch64 or arm64), and holds for every entry of the list:
#	  - it is <target>=stale or <target>=slice, and the target is one of the recompiler with a twin on the
#	    interpreter: debugger.selftest.<name> (no .interp, no .fallback) and .<name>.interp,
#	    debugger.sound.neutral.<name>.jit and .<name>.interp;
#	  - the twin was registered to run (on every host, an arm64 host too), or both were skipped (no emulator, no data);
#	  - with its twin registered to run, the target is a known limit on an arm64 host and registered to run on any
#	    other;
#	and that nothing else was registered as a known limit. So a list or a registration that also took an interpreter
#	target, or that took effect on a host that is not arm64, is a failure here.

nulldc_a64_known_limits(nulldc_a64_list)
get_property(nulldc_a64_registered GLOBAL PROPERTY NULLDC_A64_REGISTERED)
set(nulldc_a64_arm64_host OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set(nulldc_a64_arm64_host ON)
endif()

# what each target was registered as
set(nulldc_a64_problems "")
set(nulldc_a64_names "")
foreach(entry ${nulldc_a64_list})
  if(NOT entry MATCHES "^([a-z0-9.]+)=(stale|slice)$")
    list(APPEND nulldc_a64_problems "the entry \"${entry}\" is not <target>=stale|slice")
    continue()
  endif()
  set(name "${CMAKE_MATCH_1}")
  list(APPEND nulldc_a64_names "${name}")
  set(twin "")
  if(name MATCHES "^debugger\\.selftest\\.[a-z0-9.]+$" AND NOT name MATCHES "\\.(interp|fallback)$")
    set(twin "${name}.interp")
  elseif(name MATCHES "^debugger\\.sound\\.neutral\\.[a-z0-9.]+\\.jit$")
    string(REGEX REPLACE "\\.jit$" ".interp" twin "${name}")
  else()
    list(APPEND nulldc_a64_problems "${name} is not a target of the recompiler with a twin on the interpreter")
    continue()
  endif()
  set(kind "")
  set(twin_kind "")
  foreach(r ${nulldc_a64_registered})
    if(r MATCHES "^(.+)=(run|limit|skip)$")
      if(CMAKE_MATCH_1 STREQUAL name)
        set(kind "${CMAKE_MATCH_2}")
      elseif(CMAKE_MATCH_1 STREQUAL twin)
        set(twin_kind "${CMAKE_MATCH_2}")
      endif()
    endif()
  endforeach()
  if(NOT kind)
    list(APPEND nulldc_a64_problems "${name} was not registered")
  elseif(twin_kind STREQUAL "skip")
    if(NOT kind STREQUAL "skip")
      list(APPEND nulldc_a64_problems "${name} was registered as ${kind}, and its twin ${twin} was skipped")
    endif()
  elseif(NOT twin_kind STREQUAL "run")
    list(APPEND nulldc_a64_problems "the twin ${twin} of ${name} was registered as \"${twin_kind}\", not to run")
  elseif(nulldc_a64_arm64_host AND NOT kind STREQUAL "limit")
    list(APPEND nulldc_a64_problems "${name} was registered as ${kind} on an arm64 host, not as a known limit")
  elseif(NOT nulldc_a64_arm64_host AND NOT kind STREQUAL "run")
    list(APPEND nulldc_a64_problems "${name} was registered as ${kind} on a host that is not arm64 (${CMAKE_SYSTEM_PROCESSOR}), not to run")
  endif()
endforeach()

# nothing else is a known limit
set(nulldc_a64_limits 0)
foreach(r ${nulldc_a64_registered})
  if(r MATCHES "^(.+)=limit$")
    math(EXPR nulldc_a64_limits "${nulldc_a64_limits} + 1")
    if(NOT CMAKE_MATCH_1 IN_LIST nulldc_a64_names)
      list(APPEND nulldc_a64_problems "${CMAKE_MATCH_1} was registered as a known limit and is not in the list")
    endif()
  endif()
endforeach()

list(LENGTH nulldc_a64_names nulldc_a64_count)
if(nulldc_a64_problems)
  string(REPLACE ";" "; " nulldc_a64_problems "${nulldc_a64_problems}")
  add_test(NAME debugger.a64.knownlimits COMMAND "${CMAKE_COMMAND}" -E echo "FAIL: ${nulldc_a64_problems}")
elseif(nulldc_a64_arm64_host)
  add_test(NAME debugger.a64.knownlimits COMMAND "${CMAKE_COMMAND}" -E echo
           "PASS: ${nulldc_a64_count} targets in the list, ${nulldc_a64_limits} registered as known limits on this arm64 host (${CMAKE_SYSTEM_PROCESSOR}), their twins on the interpreter to run")
else()
  add_test(NAME debugger.a64.knownlimits COMMAND "${CMAKE_COMMAND}" -E echo
           "PASS: ${nulldc_a64_count} targets in the list, ${nulldc_a64_limits} known limits on this host (${CMAKE_SYSTEM_PROCESSOR}), which is not arm64: each target and its twin registered to run")
endif()
set_tests_properties(debugger.a64.knownlimits PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL: " PASS_REGULAR_EXPRESSION "PASS: ")
