## The debugger at work on the ARM7 and the DSP of the sounding guest: the run is the run nobody touched ##
#
#	Sixteen tests, debugger.sound.neutral.<idlebp|hitresume|steps|hoststop>.<jit|interp> on the ARM7 and
#	debugger.sound.neutral.dsp.<loop|hitresume|steps|hoststop>.<jit|interp> on the DSP, made by
#	nulldc_sound_neutral of ../dsptest/CMakeLists.txt: the mode neutral of ../dsptest/soundlines.py on a
#	selftest session whose one test never returns (libswirl/debugger/dbg_selftest_arm7.cpp, arm7n.*;
#	libswirl/debugger/dbg_selftest_dsp.cpp, dspn.*). Each makes four runs of dsptest.elf and holds two things
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
