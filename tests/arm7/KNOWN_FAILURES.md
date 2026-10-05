# ARM7: known failures

Why checks of `baseline.txt` are below their number of cases, and what else is known to be wrong in the ARM7
that these tests do not show. "Console" is what a Dreamcast answered (the captures in
`wren7-rtl/model/tests/hw/<suite>/hw`); the D numbers are only labels.

Paths are relative to `libswirl/`; `arm-new.h` is `gpl/vba-arm/arm-new.h`.

## What the baseline shows

| | check | cases | pass | fail |
|---|---|---|---|---|
| D7 | `arm7.wren7.vendor` `cpsr` | 45 | 4 | 41 |
| D7 | `arm7.wren7.bringup` `reset-state` | 4 | 0 | 4 |

Everything else passes: `arm7.vendor` 45/45, `arm7.directed` 63/63, the r1 of the 45 vendor jobs, the 4000
qemu_diff cases, the 6144 words of the multiplier probe, and how all 55 jobs end.

### D7: the reset state

On the console a reset (ARMRST) changes four things: PC = 0, the mode and I = F = 1 in CPSR (SVC, 0xD3),
R14_svc = the address the ARM was at, SPSR_svc = the CPSR of that moment. Every other register keeps its
value. `SoundCPU_impl::Reset` (`hw/arm7/SoundCPU.cpp`) zeroes all registers, loads VisualBoyAdvance's GBA
stack pointers and leaves IRQs enabled: CPSR = 0x53.

* `cpsr`: the CPSR at the end of 41 vendor jobs differs from the console's in bit 7 (I) and in nothing else.
  The 4 that pass (`LDM_5` to `LDM_8`) write the CPSR themselves.
* `reset-state`: the third run in a row of the countdown kernel. Console: r0 = 0x008028B8 (left by the stub of
  the run before), r14 = 0x001FE0CC (the stub's spin loop, where the reset came), SPSR = 0x600000D3, CPSR =
  0x600000D3. Here: 0, 0, 0 and 0x60000053.
* Only FIQ is ever raised (`ARM7Backend::UpdateInterrupts`), so I = 0 after reset has no consequence by
  itself; a guest that reads CPSR, or relies on a register over a reset, sees the difference.

## Known, and not shown by these tests

Known from the code and from what wren7-rtl measured or models. The 55 jobs do not get there: none returns
from an exception or gets an interrupt, and all 4000 random instructions pass.

| | what | console / data sheet | here |
|---|---|---|---|
| D1 | return address of SWI and of the undefined instruction trap | R14 = address of the instruction + 4 (console: wren7's `swi` and `und` timing kernels take the time of a trap that returns to the next instruction) | + 8: `CPUSoftwareInterrupt` and `CPUUndefinedException` in `hw/arm7/arm7.cpp` add 4 to a PC that is already past the instruction, so `movs pc, lr` skips one instruction. `CPUFiq` is right |
| D5 | interrupt controller | a request with level 0 (SCILV0-2) does not reach FIQ; reading L (0x2D00) releases nFIQ; writing M (0x2D04) bit 0 lets the next request in (console) | any enabled pending bit raises FIQ (`update_arm_interrupts` in `hw/aica/aica_mmio.cpp`); reading L does nothing, only the M write releases (`hw/arm7/SoundCPU.cpp`) |
| D6 | SCILV0-2 | read back as 0 (console) | the stored value |
| D8 | `STR` of R15 | the instruction's address + 12 (data sheet; console for STM) | + 8 in every word STR handler of `arm-new.h`; STM stores + 12 |
| D9 | `STR Rd, [Rn, #x]!` with Rd == Rn | stores the old base (data sheet) | stores the new one: the pre-indexed handlers write the base back before the store (`arm-new.h`, `CASE_16(0x520)` and the like) |
| D10 | TSTP, TEQP, CMPP, CMNP (Rd = R15 with S) | SPSR to CPSR, the PC is not written (data sheet) | take the Rd = R15 path and reload the PC, which skips an instruction |
| D11 | undefined instruction | a trap like any other | a trap (with D1) that also prints `arm7: CPUUndefinedException(). SOMETHING WENT WRONG` every time |
| D12 | recompiler: LDM / STM of one register with P = 0 and W = 0 | the base stays | rewritten as a post-indexed LDR / STR, which writes the base back (`hw/arm7/arm7_jit_virt.cpp`). The recompiler is not built on x86-64 and no test runs it |

D8 to D10 rest on the data sheet and on wren7's model, there is no console capture for them.
