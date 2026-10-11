## The debugger at work on the ARM7 of the sounding guest: the run is the run nobody touched ##
#
#	Twelve tests, debugger.sound.neutral.<idlebp|hitresume|steps>.<jit|interp>.<batch|persample>, made by
#	nulldc_sound_neutral of ../dsptest/CMakeLists.txt: the mode neutral of ../dsptest/soundlines.py on a selftest
#	session whose one test never returns (libswirl/debugger/dbg_selftest_arm7.cpp, arm7n.*). Each makes four
#	runs of dsptest.elf and holds two things against the run without the session: the sound line at 2000
#	ticks, whole, and the state hash at 90 vblanks, part for part. The line has no field for wave memory, main
#	memory or the scheduler; the hash has. The SH4 of the guest does not sleep, so both hold whole.
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
