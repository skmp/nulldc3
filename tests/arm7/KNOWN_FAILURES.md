# ARM7: known failures

Why checks of `baseline.txt` are below their number of cases, and what else is known to be wrong in the ARM7
that these tests do not show. "Console" is what a Dreamcast answered (the captures in
`wren7-rtl/model/tests/hw/<suite>/hw`); the D numbers are only labels.

Paths are relative to `libswirl/`; `arm-new.h` is `gpl/vba-arm/arm-new.h`.

## What the baseline shows

| | check | cases | pass | fail |
|---|---|---|---|---|
| D5 | `arm7.directed` `fiq` | 10 | 9 | 1 |
| D7 | `arm7.wren7.bringup` `reset-state` | 4 | 2 | 2 |

Everything else passes: `arm7.vendor` 45/45, the other 131 cases of `arm7.directed`, the r1 and the CPSR of the
45 vendor jobs, the 4000 qemu_diff cases, the 6144 words of the multiplier probe, and how all 55 jobs end.

### D5: ARMRST keeps the service of a request

`SoundCPU_impl::SetResetState` (`hw/arm7/SoundCPU.cpp`) resets the ARM when ARMRST is released and leaves the
interrupt controller in front of nFIQ alone. A request whose handler has read L and has not written M yet stays
in service (`e68k_reg_M`), and the program that is started gets no FIQ until it writes bit 0 of M. One that
writes M in its handler only gets none at all.

That is the case of `fiq` that fails, "ARMRST between the L read and the M write", and it is what a disc of a
KallistiOS program does when it is booted through the BIOS: the SH4 holds ARMRST while the sound program of the
BIOS is between its L read and its M write, the ARM crt0 of KallistiOS writes M in its FIQ handler only, and its
sound driver never gets an interrupt. The disc has no sound. With the sound program of the BIOS running (no disc,
the first 14 emulated seconds) a request is in service with nFIQ released at 405 of the 19292 times the SH4 has
run and the ARM gets its turn (2.1 %; counted at the entry of `SoundCPU_impl::Update`: `enabled`, `e68k_reg_M`
set, `e68k_out` clear).

No console capture has an ARMRST with a request in service: every FIQ kernel of wren7-rtl reads L and writes M in
its prologue, before it turns FIQs on. A job finds SCIEB and L as the job before left them (`fiqdiag` `d1.bin` to
`d5.bin`, the second region), so ARMRST does not reset the registers of the controller; nothing shows the service.
The case says what the reset has to do because software depends on it: a program that ARMRST starts has to get
its interrupts whatever the program in front of it was in the middle of. A console that kept the service would
lose its sound behind about 2 of 100 resets, which is not observed. The emulator gave the program its interrupts
as long as it had one state for nFIQ and the service.

The other case with an ARMRST, "ARMRST in front of the L read", passes: nFIQ stays pulled over ARMRST, the next
program is interrupted as soon as it turns FIQs on and finds the level of that request in L. That is not measured
either; it is what the emulator did before, and what the captures say of everything but a read of L.

### D7: where a reset finds the ARM

On the console a reset (ARMRST) changes four things: PC = 0, the mode and I = F = 1 in CPSR (SVC, 0xD3),
R14_svc = where the ARM was, SPSR_svc = the CPSR of that moment. Every other register keeps its value, and
`SoundCPU_impl::ResetException` (`hw/arm7/SoundCPU.cpp`) does the same.

`reset-state` is the third run in a row of the countdown kernel: r0 as the stub of the run before left it
(0x008028B8), r14, SPSR and CPSR (both 0x600000D3). r0, SPSR and CPSR are the console's in all four jobs. r14 is
not: the reset comes while the ARM sits in the done stub's `b spin` at 0x001FE0CC, at some point of the three
bus cycles of that branch, and the console has two values for it. Of the 116 result blocks under
`wren7-rtl/model/tests/hw` whose r14 is from such a reset, 89 have 0x001FE0CC and 27 have 0x001FE0D0. The
interpreter executes whole instructions and has one value, the address of the instruction the ARM was at:
0x001FE0CC. `hello_1` and `hello_1000` pass, `hello_100000` and `hello_1000000` have 0x001FE0D0 on the console.

## Known, and not shown by these tests

Known from the code and from what wren7-rtl measured or models. The 55 jobs do not get there, and all 4000
random instructions pass. The last column says why a row is not fixed: "pending console confirmation" rests on
the data sheet and on wren7's model only, and is not changed until a console capture has it.

| | what | console / data sheet | here | |
|---|---|---|---|---|
| D5 | interrupt controller: more than one request pending | not measured | the lowest pending bit gives the level, and nothing interrupts when that level is 0, as wren7's model has it (`update_arm_interrupts` in `hw/aica/aica_mmio.cpp`, `update_e68k` in `hw/arm7/SoundCPU.cpp`) | not measured |
| D5 | what puts the 0 into L behind a job with level 0 | not measured: between the 7 that `d4.bin` finds and the 0 that `d5.bin` finds, the job reads L, writes SCILV0-2, SCIRE, M and SCIEB and has requests of level 0 pending (`fiqdiag`) | the request: with nothing in service a request of level 0 leaves its level in L, and does nothing else (`update_e68k`) | not measured |
| D7 | registers and interrupt controller when the machine is switched on | not known: a console job starts with what the programs before it left | cleared, with VisualBoyAdvance's GBA stack pointers (R13 = 0x03007F00, R13_irq = 0x03007FA0, R13_svc = 0x03007FE0) and CPSR = 0x53 (`SoundCPU_impl::Reset` in `hw/arm7/SoundCPU.cpp`); the first reset puts that CPSR into SPSR_svc. The controller holds nothing and L is 0 | not measured |
| D8 | `STR` of R15 | the instruction's address + 12 (data sheet; console for STM) | + 8 in every word STR handler of `arm-new.h`; STM stores + 12 | pending console confirmation |
| D9 | `STR Rd, [Rn, #x]!` with Rd == Rn | stores the old base (data sheet) | stores the new one: the pre-indexed handlers write the base back before the store (`arm-new.h`, `CASE_16(0x520)` and the like) | pending console confirmation |
| D10 | TSTP, TEQP, CMPP, CMNP (Rd = R15 with S) | SPSR to CPSR, the PC is not written (data sheet) | take the Rd = R15 path and reload the PC, which skips an instruction | pending console confirmation |
| D11 | undefined instruction | a trap like any other | a trap that also prints `arm7: CPUUndefinedException(). SOMETHING WENT WRONG` every time. In the recompiler the call of the interpreter for an opcode it does not know passes the address of the next instruction and ends the block only when the decoder has the opcode as one that sets the PC (`_set_pc` in `DecodeOpcode`, `hw/arm7/arm7_jit_virt.cpp`: SWI is, an undefined opcode is not), so R14_und is stale and the block runs on behind the trap | the message is no matter of the console; the recompiler is not built on x86-64 and no test runs it |
| D12 | recompiler: LDM / STM of one register with P = 0 and W = 0 | the base stays | rewritten as a post-indexed LDR / STR, which writes the base back (`hw/arm7/arm7_jit_virt.cpp`) | the recompiler is not built on x86-64 and no test runs it |
| D14 | CPSR in the handler of a SWI or of the undefined instruction trap that is taken in a privileged mode | N, Z, C, V and F as they were, I set (data sheet 3.4) | N, Z, C, V and F of the SPSR of the mode the trap leaves: `ARM7Backend::CPUSwitchMode` (`hw/arm7/arm7.cpp`) loads the CPSR from the SPSR on the way into the mode. The SPSR of the trap and the return are right, and so is the entry of a FIQ. A SWI in SVC mode with CPSR = 0xA00000D3 and SPSR = 0x00000053 has 0x000000D3 in its handler | pending console confirmation |

What the recompilers (ARM hosts, 32 bit x86) do not have of their own goes through the code these tests run:
MUL and MLA, SWI and everything else they hand to `ARM7Backend::singleOp`, the reset and the interrupt
controller. Data processing with its shifter carry is their own code.
