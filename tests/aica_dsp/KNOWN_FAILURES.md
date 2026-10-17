# AICA DSP core tests: checks below their total

`baseline.txt` has two kinds of check that pass fewer cases than they have:

* the report of `dsp_basic` against the console's, 306 of 309 lines, in all six of its rows (the model, the core
  alone and the emulator's sound chip, against the console runs `hw9` and `hw`). The model, the core alone and the
  emulator's sound chip write the same 309 lines (`core-as-model:lines`, `emu-as-core:lines`);
* the report of `dsp_mem3` on the emulator's sound chip (`aica.dsp.case.dsp_mem3.emu`), 7 of 12 lines against the
  console and against the core alone: the five lines whose IWT stores what the SH4 read last, which the emulator
  does not put into the DSP's read latch.

Nothing else is below its total, and nothing is left out of a count.

## aica.dsp.case.dsp_basic and aica.dsp.case.dsp_basic.emu: `console-hw9:*:lines`, `console-hw:*:lines`

Three lines of each console run are not what the model and the core write. None of them is a rule of the DSP that
is in doubt; all three are places where the console's report depends on when the SH4 did something.

**A slot of TEMP that was not rewritten yet, one line per console run.** The case reads four of the 128 TEMP
slots 4 ms after loading a program and expects them equal, because a TWT step rewrites every slot within 128
samples. Once per run the console read a slot that still held the value of the program before:

    hw9, line 279   console: D frc src -98f6 shift 1: T=338400/3ffc00/3ffc00/338400
                    here:    D frc src -98f6 shift 1: T=3ffc00
    hw, line 115    console: A ffff -4096 1 : T=000200/000100/000200/000200 E=0002 N=0002 F=6200
                    here:    A ffff -4096 1 : T=000200 E=0002 N=0002 F=6200

The line differs between the two console runs themselves (each has the other's line as the model has it). caique's
`dsp_temp` dumped all 128 slots to look for it and found the ring rewritten as the model has it; its notes put the
stale slot down to the load of the program (model/NOTES.md, the DSP section, claim D1).

**Two lines that show into which MIXS bank two CPU writes went.** The case writes the upper 16 bits of MIXS5,
then the low 4, reads both back, and then runs a program on MIXS5 for 4 ms and reads four TEMP slots:

    line 306   console: F MIXS5 right after write 1234/5, after 2ms 0000/0
               here:    F MIXS5 right after write 0000/5, after 2ms 0000/5
    line 307   console (hw9): F ira 25 * -4096 shift3: T=000000/edcbb0/000000/edcbb0
               console (hw):  F ira 25 * -4096 shift3: T=edcbb0/000000/edcbb0/000000
               here:          F ira 25 * -4096 shift3: T=ffffb0/edcbb0/ffffb0/edcbb0

The CPU writes MIXS in the bank the DSP reads, and the banks change over every sample. On the console both writes
landed in one sample, so one bank holds 0x12345 and the other 0, and the program's output alternates between
-0x12345 << 4 (0xEDCBB0) and 0. On the harness's time base a sample boundary falls between the two writes of the
first pair: one bank gets 0x12340, the other 0x00005, and the read back shows the second. The case's next pair of
writes lands in one bank, which then holds 0x12345, and the output alternates between -0x12345 << 4 and
-0x00005 << 4 (0xFFFFB0). Both are the two bank rule; which it is depends on where the boundary is. The two
console runs differ from each other in line 307 for the same reason: the alternation starts on the other sample.

The model has the same three lines as the core alone, so these rows say nothing about the core that they do not
say about the model: they are counted, and pinned at 306, so that a change of either shows. The emulator's sound
chip has them too (`emu-as-core:lines` is 309 of 309): its CPU writes of MIXS land in the bank the DSP reads, on
the same time base.

## aica.dsp.case.dsp_mem3.emu: `emu-as-core:lines`, `console-hw9:emu:lines`, `console-hw:emu:lines`

The emulator calls no `AicaDsp::BusRead`: a read of the wave memory by the SH4 or the ARM7 goes through the
emulator's memory map and leaves nothing in the DSP's read latch (the cut of the M3 round, M3-D10: a hook there
would cost every read; the `cycle-similar` AICA of track 5 wires it). The core alone has the read in the latch
(its machine calls `BusRead` with the upper half of every 32-bit read, between two samples), and so has the console.

Lines 3 to 7 of the report put an MRD and an MWT in one step. That step reads nothing (aica_dsp_core.h, "An MRD
in a step that also has MWT reads nothing"), so the IWT behind it stores the word the latch already holds: on the
console the upper half of the SH4's last read of the wave memory, the word in front of the case's MADRS (the
case reads the words back between its runs), here the word of the DSP's own last read:

    line 3    console: MRD+MWT@5                madrs 0320: read 032300  words 031e..0323: ...
              here:    MRD+MWT@5                madrs 0320: read 032000  words 031e..0323: ...
    lines 4 to 7: the same, at madrs 0325, 0330, 0340 (float write) and 0350 (the even step 4)

The rest of each line, the words of the wave memory around the write, is the console's: the write of the step is
right. A scratch build of the test whose machine calls `BusRead` with the upper half of every wave memory read of
the case writes all 12 lines as the console does; the tree does not keep that build, it would test a wiring the
emulator does not have.
