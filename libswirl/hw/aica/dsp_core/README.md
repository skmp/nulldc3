# AICA DSP core

One implementation of the AICA's DSP, for everything that runs it:

* the emulator's sound chip, at every timing tier,
* the debugger's DSP single step,
* the DSP playground of the web tools, through the WebAssembly build in `wasm/`.

`aica_dsp_core.h` and `aica_dsp_core.cpp` are all of it. They need `<cstdint>` and nothing else, compute with
integers only and keep everything in one `AicaDsp`. The header has what each call does.

What the core computes is what the DSP of the caique AICA model computes (`caique-rtl`
`model/sample-model/aica_model.cpp`), and that model is what a Dreamcast answered: caique measured the rules below
on a console, `model/NOTES.md` there has the measurements and `model/tests/<case>/hw*` the captures.
`tests/aica_dsp` compares the core with the model behind every step and with the console's files, see its README.

## The rules

"Console" names the caique case whose captures show the rule (`model/tests/<case>/`, runs `hw`, `hw9`, `hw10`,
`hw11`). "Model" is a rule the caique model has that no capture shows by itself.

### Datapath

| Rule | Measured |
|---|---|
| A sample is 128 steps; step s executes MPRO[s] and takes COEF[s] | console: dsp_basic |
| `ACC = floor(X * Y / 4096) + B`, X 24 bit, Y 13 bit signed, ACC 26 bit, wrapping | console: dsp_basic, dsp_mem (M7d) |
| The shifter works on the ACC of the step before: SHIFT 0 saturates it to 24 bit, 1 saturates twice its value, 2 cuts twice its value to 24 bit, 3 cuts it | console: dsp_basic |
| B is 0 (ZERO), ACC (BSEL) or TEMP; NEGB takes it away. B = ACC is the ACC of the step before | console: dsp_basic (B2) |
| ACC of step 127 is what step 0 of the next sample shifts; FRC_REG, Y_REG and ADRS_REG stay across samples | console: dsp_mem (M3) |
| INPUTS: MEMS as stored, MIXS << 4, EXTS << 8, 0 for IRA above 0x31 | console: dsp_basic (F); EXTS << 8 and the 0: model |
| Y: FRC_REG (13 bit signed), COEF >> 3 (13 bit signed), Y_REG[23:11] (signed), Y_REG[15:4] (unsigned) | console: dsp_basic (D) |
| FRCL: SHIFTED[23:11], or SHIFTED[11:0] with SHIFT 3. YRL: INPUTS | console: dsp_basic (D) |
| ADRL: INPUTS[23:16], or SHIFTED[23:12] with SHIFT 3. ADRS_REG is 12 bit and the step after ADRL has the new value | console: dsp_mem (M3), dsp_mem2 |
| A step reads before it writes: INPUTS sees MEMS before IWT, Y sees Y_REG before YRL | console: dsp_mem (M7b, M7c) |
| TWT writes SHIFTED to TEMP[TWA + MDEC_CT], TRA reads TEMP[TRA + MDEC_CT]: TEMP is a ring of 128 that turns once a sample | console: dsp_basic, dsp_temp |
| EWT writes SHIFTED[23:8] to EFREG; EFREG is replaced, not added to, and keeps its value | console: dsp_basic (B), dsp_mem (M7a) |

### Wave RAM

| Rule | Measured |
|---|---|
| Address in words: MADRS[MASA] + ADRS_REG (ADREB, sign extended from 12 bit) + 1 (NXADR) | console: dsp_mem (M2, M3), dsp_mem2 |
| TABLE 0: + MDEC_CT, wrapped in the ring of 8K << RBL words. TABLE 1: wrapped at 64K words | console: dsp_mem (M1, M2) |
| The ring starts at byte RBP * 2048 | console: dsp_mem (M1c) |
| MDEC_CT counts down once a sample, 16 bit, wrapping | console: dsp_mem (M1b), mdec_hold |
| MWT writes at its own step, odd or even: SHIFTED[23:8] with NOFL, else SHIFTED in the float format | console: dsp_mem (M4), dsp_wslot |
| The float format: sign, 4 bit exponent, 11 bit mantissa. Packing masks the mantissa to 11 bit at exponent 12; unpacking an exponent above 11 puts the sign where the bit that is not stored would be | console: dsp_pack (all 2^24 values), dsp_unpack (all 2^16 words) |
| Every read of the wave RAM leaves its 16 bit word in one latch, whoever reads: the DSP, a channel, the ARM7 (the low half of what it reads), the SH4 (the low half in its first slot, the upper half in its second) | console: dsp_mem3, dsp_coll, arm_latch, sh4_latch |
| The DSP copies the latch at the end of every odd step; IWT stores that copy from the start of the next odd step on | console: sh4_latch, arm_latch, dsp_wslot |
| So an odd MRD is stored by an IWT 2 or 3 steps on, an even MRD 3 or 4 steps on, unless another read comes in the odd step behind the even one | console: dsp_basic (C), dsp_mem (M5), dsp_wslot, sh4_latch |
| IWT stores the word << 8 when the step two before the IWT had NOFL, else unpacked from the float format | console: dsp_basic (C), dsp_mem2 (E) |
| An MRD in a step that also has MWT reads nothing: IWT gets what the latch held | console: dsp_mem3 |
| A read behind the copy is too late: an odd MRD keeps its word for an IWT 3 steps on whatever is read in between | console: sh4_latch (B) |
| A write of the DSP in the step behind an even MRD leaves the read's word alone | console: arm_latch (B, C) |

### Channels and MIXS

| Rule | Measured |
|---|---|
| Channel K reads the wave RAM in the slot of DSP step 2K - 14 | console: dsp_coll; wren7-rtl tests/hw/sgc |
| A DSP MWT in a slot a channel fetches in is dropped, a DSP MRD there returns the channel's word: the word that holds its sample CA + 1 | console: dsp_coll (18 runs) |
| Channels 0 to 6 (steps 114 to 126) collide with their fetch of the next sweep | console: dsp_coll (p16_k3) |
| Every channel sends to the bus of its ISEL in every sample; a bus no channel sends to keeps its value | console: mixs_write, eg_lock |
| The sum on a bus is 20 bit and wraps | console: sgc_mix |
| Two banks: the DSP reads the bank the sweep before filled | console: eg_lock, dsp_basic (F), dsp_temp (T2) |
| The CPU reads and writes MIXS in the bank the DSP reads; a value it writes to a bus without a sender shows in every other sample | console: mixs_rd, dsp_basic (F) |

### Registers as the CPU has them

| Rule | Measured |
|---|---|
| COEF keeps bits 15:3; MADRS and MPRO 16 bit; EFREG 16 bit | console: probe |
| TEMP: 24 bit, both halves read and written | console: probe |
| MEMS: bits 23:8 read and written, bits 7:0 read only | console: probe, dsp_basic (C2) |
| MIXS: 20 bit, both parts read and written | console: probe, mixs_rd |
| 0x2804 (RBP, RBL) reads 0; EXTS reads 0 and is not written, with no disc playing | console: probe |
| Bit 15 of 0x2804: while it is set MDEC_CT counts down once a step, its two bytes apart, in place of once a sample. A whole sample of it is MDEC_CT XOR 0x8080, which is what the core does at the end of the sample | console: mdec_hold, k_jump. Per sample only: see below |

### Not measured

* What MDEC_CT is after power on. The core starts at 1, as the model does; a program cannot read it.
* EXTS with a disc playing: what the CPU reads at 0x45C0 then, and the << 8.
* Offsets 0x3300-0x33FF and 0x3C00-0x3FFF. The core has no registers there.
* A wave RAM of 8 MB. The core takes any power of two, RBP is 12 bit and reaches 8 MB.
* A bus read in a slot the DSP uses. The bus does not grant one; in the core the DSP's read comes last.

## What is not modelled

* **Time inside a step.** A step is one call. The hardware reads its operands, multiplies and writes in four
  clocks, and a CPU access to TEMP or EFREG waits for the port; the core has no clocks.
* **The bus.** Which step a read of the ARM7 or the SH4 gets, and that it gets none where the DSP or a channel has
  the slot, is the caller's: the core takes `BusRead` in front of the step the caller says. `StepUsesRam` has the
  DSP's part of that decision.
* **The test mode of MDEC_CT inside a sample.** On the hardware the count runs once a step while bit 15 of 0x2804
  is set, so two memory steps of one sample have different MDEC_CT. The core has the whole sample's effect at
  the end of the sample, as the caique sample model. The envelope generator's counter does not follow MDEC_CT in
  this mode: a user that keeps that counter takes the difference from MDEC_CT before and after a sample.
* **The sound generator.** The sends and the fetches come in through calls. That a channel fetches, and with
  which word, is the channel's business (PCM every sample, ADPCM when it enters a word, noise never).
* **The output.** EFREG is where the core ends: EFSDL, EFPAN, the mixer and the DAC are the sound chip's.
* **A switch of the CPU's MIXS bank inside a sample.** The console shows the low 4 bits change over a read
  earlier than the upper 16; the core changes both at step 0.

## The interface

`aica_dsp_core.h`, in short:

| Call | What it is on the hardware |
|---|---|
| `Init(ram, size)`, `Reset()` | power on |
| `RegRead(off)`, `RegWrite(off, v)`, `RegOwned(off)` | the CPU at 0x2804 and 0x3000-0x45C7 |
| `StepOne(in, view)` | one step; step 0 changes the MIXS banks over, step 127 counts MDEC_CT |
| `RunSample(in)` | the steps up to the end of the sample |
| `SweepBegin()`, `MixsSend(bus, v)`, `MixsSendLate(bus, v)` | the sound generator's sweep and its sends |
| `ChannelFetch(word)` | a channel's read in the slot of the next step |
| `BusRead(word)` | a read of the ARM7 or the SH4 in the slot of the next step |
| `EXTS[]` | the CD-DA input |
| `StepUsesRam(s)`, `ChannelStep(K)` | which slots the DSP and the channels have |
| `AicaDspDecode`, `AicaDspEncode`, `AicaDspPack`, `AicaDspUnpack` | the fields of a step, the float format |
| `StateSize()`, `SaveState(blob)`, `LoadState(blob, size)` | a versioned little endian blob, without the wave RAM |
| `AicaDspStepView` | what a step computed, for a debugger |

The registers and the datapath are fields of `AicaDsp`. A debugger reads and writes them there. The header says
which bits each field has: the core computes with those, whatever a field is made to hold.

`StepOne` and `RunSample` are two pieces of code for the same steps: `StepOne` executes a step as the header
describes it, `RunSample` executes the program from a plan that it makes of MPRO once (and again when MPRO is
another program) and skips the empty rows a program ends with. `tests/aica_dsp` holds them against each other
from every step.

### The three users

**The emulator's sound chip.** Per sample, at the tier without time inside the sample:

    AicaDspSampleInputs in;                  // the sweep: per bus the sum of the sends and that it has one,
    ...                                      // per channel that it fetches and the word its fetch leaves
    dsp.EXTS[0] = cdda_left;  dsp.EXTS[1] = cdda_right;
    dsp.RunSample(&in);                      // EFREG has the 16 outputs

with `RegRead` / `RegWrite` for the CPU's accesses to the DSP's registers and `BusRead(value >> 16)` for an SH4
read of the wave RAM between two samples. This is the caique sample model's order: the sweep, then the DSP on the
bank of the sweep before. The fetches of channels 0 to 6 are those of the next sweep (their slots are steps 114
to 126), which the model computes ahead, from this sample's registers.

A chip at this tier that has its sends one by one and not as sums calls `SweepBegin()`, then `MixsSend` for each
send, then `RunSample()`. The DSP reads those sends in the sample after, as it reads the sums of the inputs and
as the console does. It is the `SweepBegin()` that makes the sample of delay: a send is in the bank that the DSP
starts to read at the first step 0 behind the next `SweepBegin`, and step 0 does a `SweepBegin` itself when none
was called since the step 0 before. Sends in front of a `RunSample()` without a `SweepBegin()` are therefore read
by that `RunSample()`, a sample earlier than on the console.

At a tier with steps or clocks the chip calls `StepOne()` without inputs at each step, and in front of it
whatever falls into that step: `SweepBegin()` 16 steps before the DSP's step 0, `MixsSend` when a channel's send
lands (`MixsSendLate` for the last 8 channels, whose sends land behind the next `SweepBegin`), `ChannelFetch` in
a channel's slot when it fetches, `BusRead` in the slot the bus gave a CPU read. The core has been compared with
the sample model only: these calls at other places than the sample model's are tested as far as that model's DSP
can be made to follow them (bus reads in front of any step, fetches in any slot), not against caique's clocked
model.

**The debugger.** `StepOne(in, &view)` with the inputs of the running sample steps the emulator's DSP by one
step; `view` and the fields of `AicaDsp` are what it shows. `step` is the step that is executed next. A write
into the fields needs no call behind it. `SaveState` / `LoadState` are for the save states of the emulator.

**The playground.** `wasm/`: `build.sh <out dir>` makes `aica_dsp_core.wasm` and copies the loader
`aica_dsp_core.mjs` next to it. The module is the core behind the C interface of `wasm/aica_dsp_capi.h`, one
`.wasm` without generated JavaScript; it imports `env.emscripten_notify_memory_growth`, which may do nothing. A
playground has no sound generator and no bus: it sets the inputs' MIXS sums and calls `runSample()` or `step()`.

## The playground: from the Rust DSP to this core

What `src/lib/aicaDsp.ts` of the web tools offers, and what serves it in `aica_dsp_core.mjs`:

| aicaDsp.ts | Loader |
|---|---|
| `initialize()` | `module = await loadAicaDsp()`, `dsp = module.create()` |
| `readReg(addr)` | `dsp.readReg(addr)` as the CPU reads; `dsp.peekReg(addr)` to show a register |
| `writeReg(addr, data)` | `dsp.writeReg(addr, data)`; `dsp.pokeReg(addr, data)` for the low byte of MEMS and for EXTS |
| `step(stepNum)` | `dsp.step()`: the core has the step number. `dsp.set(REG.STEP, n)` first for another step |
| `step128Start()`, `step128End()` | nothing: step 0 and step 127 do what belongs to them |
| `doDspStep()` | `dsp.step()` |
| `runToNextSample()` | `dsp.runSample()`, or `dsp.run(128 - dsp.currentStep, stops)` with breakpoints |
| `getCurrentStep()` | `dsp.currentStep` |
| `getSampleCounter()` | `dsp.sampleCounter` |
| `resetCounters()` | `dsp.set(REG.STEP, 0)`, `dsp.set(REG.SAMPLES, 0)`; `dsp.reset()` for all of the state |
| `getDspRegisters()` | `dsp.view` after `step()` (`VIEW.*`) and `dsp.get(REG.*)` |
| writing the 16 inputs to MIXS before a sample | `dsp.inputsMixs[bus] = sample << 4`, `dsp.setInputs({ sent: 0xFFFF })` once |
| reading the 16 outputs from EFREG | `dsp.efreg[i]`, signed 16 bit |
| the register dump of the panel | `peekReg` over the DSP's offsets; the module has no other registers |

What the user of the playground sees change with the swap:

| | Rust DSP | This core, the console |
|---|---|---|
| Scale of the signal | MIXS holds the 16 bit sample, the datapath works 16 times below full scale and SHIFT 0 / 1 saturate at 20 bit | MIXS holds the sample * 16 (20 bit), INPUTS is that << 4, SHIFT 0 / 1 saturate at 24 bit. An input written as before comes out 16 times quieter: send `sample << 4` |
| ACC and B | ACC is kept 4 times its value with 2 bits of fraction, `X * Y >> 10`; B from TEMP is `TEMP << 2`; the value wraps at 24 bit | ACC is the value, each product floored to an integer (`>> 12`), 26 bit: 2 bits of headroom above TEMP. A chain of products can be lower by 1 per product, and a sum that passes 24 bit and comes back is right |
| EFREG | a 32 bit sum: cleared at the start of every sample, EWT adds `SHIFTED >> 4`, and `readReg` returns all 32 bits, `FFFFxxxx` for a negative output | 16 bit: keeps its value, EWT replaces it with `SHIFTED[23:8]`: two EWT to one register leave the second, a register no step writes holds its last value. `readReg` and `peekReg` return the 16 bits, `0000xxxx` for a negative output; `dsp.efreg[i]` has it signed |
| MIXS | one register per bus, read by the DSP in the sample it is written | two banks: a value is read one sample after it was sent or written, and a CPU write without a sender shows in every other sample |
| Wave RAM steps | MRD and MWT in odd steps only, ignored in even ones | MWT in any step; MRD in any step, an even one is stored a step later |
| MRD to IWT | four slots by step mod 4: a read in step s is stored by IWT in s + 2, s + 6, ...; IWT in an even step stores 0 | one latch: stored by IWT in s + 2 and s + 3 (s odd), then whatever was read last |
| MRD with MWT in one step | reads the old word, then writes | writes, reads nothing: IWT gets the last word read by anyone |
| NOFL | the conversion of a read is chosen by NOFL of the MRD's step | by NOFL of the step two before the IWT, which is the MRD's step only for an IWT 2 steps behind an odd MRD |
| Float format | packing does not mask the mantissa at exponent 12, unpacking leaves bit 22 clear above exponent 11: -2048 to -1 come back 2048 lower through delay memory | both as the console: every value and every word is the console's |
| ADRS_REG | added unsigned | added as a signed 12 bit number: a negative modulation goes down |
| MDEC_CT | counts from the ring length down to 1 and reloads | 16 bit, free running; the same addresses in the ring and in TEMP, another number in the register view |
| Bit 15 of 0x2804 | stored | a test mode: MDEC_CT is XORed with 0x8080 every sample while it is set |
| Register slots | every slot is 32 bit of storage, and an offset is a byte's: 0x12345678 written to COEF, MADRS, MPRO or the upper part of TEMP reads back whole; the low parts keep the bits above their own (TEMP low 0x1FF reads 1FF and is 0x1FF to the DSP, MIXS low 0x1F reads 1F); a write to 0x3001 changes the second byte of COEF0 | a slot is one 16 bit register, the low parts of TEMP / MEMS / MIXS 8 / 8 / 4 bit: the upper half of a slot reads 0, a bit a register does not have is not stored, and bits 1:0 of an offset are ignored, so a write to 0x3001 writes COEF0. This does not show for what the panel and the assembler write; it does for a register file that is loaded and for a value typed in above 16 bit |
| COEF | 32 bit stored | 16 bit, and bits 2:0 read 0 |
| MEMS low byte, EXTS, 0x2804 | read and written by `writeReg` / `readReg` | not the CPU's to write (MEMS low byte, EXTS) or to read (0x2804, EXTS): `pokeReg` / `peekReg` |
| Other register offsets | 0x8000 bytes of storage | none: they read 0 |
| Internal registers | MEMVAL[0..3] | the latch, its copy and what IWT stores (`REG.LATCH`, `REG.LATCH_COPY`, `REG.MEMVAL`) |
| Steps | any step number in any order, the sample's start and end as calls | in order, start and end by themselves |

## Speed

`RunSample` with the sweep's 16 sends, `tests/aica_dsp/aica_dsp_bench`: one thread of a Ryzen 9 3900X, g++ 13 -O2.
The machine was busy on all of its 24 threads while this was measured (load average 26 to 55), so each row has
the slowest and the fastest of eight runs of a million samples, and next to them what does not depend on the
load: the instructions a sample takes (callgrind: what a run of 12,000 samples takes more than one of 2,000,
for each of the 10,000).

| Program | Samples a second | Times 44100 Hz | Instructions a sample |
|---|---|---|---|
| a reverb's shape, 96 steps: multiplies, float reads from the ring, IWT, some writes | 0.88 to 1.19 million | 20 to 27 | 6350 |
| the same over all 128 steps | 0.28 to 0.95 million | 6 to 22 | 7850 |
| no program | 3.9 to 4.6 million | 88 to 104 | 1750 |

clang 18 -O2 ran the first row at 1.14 to 1.29 million. A sample of single steps, 128 `StepOne`, costs about
six times a `RunSample`: 0.13 to 0.21 million samples a second on a 96 step program, 3 to 5 times 44100 Hz.

## Licence

The core is derived from the DSP of the caique AICA model, which is under the MIT licence:

    Copyright 2026 Stefanos Kornilios Mitsis Poiitidis

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without
    limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
    Software, and to permit persons to whom the Software is furnished to do so, subject to the following
    conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions
    of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
    TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
