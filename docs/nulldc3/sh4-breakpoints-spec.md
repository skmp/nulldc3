# SH4 code breakpoints and single step in the active backend — mechanism

Specification for TRACK1-DESIGN.md v2 (replaces the "interpreter loop while breakpoints are armed" text of 2.3 / 2.4).
It implements decision D1 ("what you debug is what runs"). Scope: SH4 code breakpoints, resume, single step, step over,
step out; x64 JIT and the interpreter in full, the other JIT backends as requirements plus the fallback.

Code references are `devcast/libswirl/...` at `34a62ea8` (branch `skmp/nulldc3`), written relative to `libswirl/`.
A prototype of everything in section 3 except 3.j exists (Appendix A); its measurements are in Appendix B; where this text
departs from D1 or from a review proposal is listed in Appendix C.

---

## 1. Requirements

| # | Requirement |
|---|---|
| R1 | A breakpoint is executed by the backend that runs the guest. No engine switch when a breakpoint is armed (x64 JIT, interpreter). |
| R2 | Guest memory is never modified to implement a breakpoint (no opcode patching). |
| R3 | With no breakpoint armed the JIT fast path has no added instruction; the only cost is one load and branch per *decoded* instruction. |
| R4 | A compiled check does not change block boundaries, block costs, or the points at which the slice counter is tested. A run whose checks all answer "continue" is identical, in guest state and scheduler time, to a run without breakpoints. |
| R5 | A stop reports PC = the instruction in front of which it stopped, with the complete SH4 context (integer, FP, SR, T, FPSCR, PR, MAC, banks). Nothing of that instruction has executed. |
| R6 | A stop does not advance the scheduler. The slice counter at the stop holds the block cost of the instructions that ran (3.a "refund"). |
| R7 | Stop followed by resume is identical to no stop: same instruction stream, same total cycles, same block boundary after it, same slice-end points. This holds in every timing mode. |
| R8 | Resume executes the instruction at PC once; the next arrival stops again. |
| R9 | A breakpoint on a delay slot stops once, in front of its branch, with PC = the branch, whether or not the branch is taken. |
| R10 | Addresses are compared as `addr & 0x1FFFFFFF` (non-MMU builds). |
| R11 | Mute / solo, enable state of a category, hit counts and conditions are evaluated in the helper and never need a recompile. Add / remove / enable / disable of a breakpoint recompiles only the blocks of that address's page. |
| R12 | A single step executes one instruction (a delayed branch with its slot) through the same decoder, code generator and cost function as a run, and does the end-of-slice work a run would do. |
| R13 | Leaving and re-entering a backend loop (stop, pause, apply a change) keeps the slice counter. |
| R14 | Odd PC, unreadable PC, undefined opcode and an illegal slot instruction make a step fail with an error; they do not hang or kill the process. |

---

## 2. Model

*Block*: what `dec_DecodeBlock` produces (`hw/sh4/dyna/decoder.cpp:1035-1223`): straight-line instructions from `addr`,
ended by a branch (with its slot), by a fallback that writes PC / FPSCR (`:1098-1106`), or by the size limits (`:1053-1057`:
500 SHIL ops or `guest_cycles >= max_cycles`, never between a branch and its slot). Its cost `guest_cycles` is a
compile-time constant, subtracted from the slice counter in the block prologue (`jit/backend/x64/rec_x64.cpp:203-210`).

*Slice counter*: cycles left until the next `UpdateSystem()`. x64: the global `cycle_counter` (`rec_x64.cpp:41-43`), tested
by the dispatcher after each block (`:163-165`); interpreter: `l` (`hw/sh4/interpr/sh4_interpreter.cpp:74`).
"Time" below is `sh4_sched_now64() + SH4_TIMESLICE - counter`.

*Check*: code in front of one guest instruction that calls the debugger helper and can stop there.

*Stop*: the helper answered non-zero; the backend loop is left with PC at the instruction.

*Temporary block*: a block compiled for one execution and never entered in the block cache (3.g). Two uses: a single
step, and the *remainder* of a block that a stop interrupted (3.f).

*Armed address*: a masked address with an enabled breakpoint (permanent or temporary). Mute / solo are not part of it.

---

## 3. Mechanism

### 3.a Decoder test and the emitted check

**Where.** In `dec_DecodeBlock`, right after the opcode is read (`decoder.cpp:1079`) and before anything is charged or
emitted for it, for instructions that are not a delay slot (`state.cpu.is_delayslot == false`):

```cpp
u32 op = ReadMem16(state.cpu.rpc);
if (!state.cpu.is_delayslot && dbg_sh4_WantsCheck(state.cpu.rpc, (OpDesc[op]->type & Delayslot) != 0)) {
    Emit(shop_dbg_check, shil_param(), mk_imm(state.cpu.rpc), mk_imm(blk->guest_cycles));   // rs2: see "refund"
    state.dbg_checks++;
}
```

`dbg_sh4_WantsCheck(pc, has_slot)` is inline: `armed_count == 0 → false` (one load and branch, R3), else
`IsArmed(pc) || (has_slot && IsArmed(pc + 2))`. `IsArmed` is a per-page bitmap test in front of the exact lookup.

**Inline op, block not ended.** The block is *not* ended at a breakpoint: ending it would change the number of blocks and,
in fast mode, the cycles charged (one cycle per block, `decoder.cpp:1208-1211`). The check is a new SHIL op
`shop_dbg_check`, added at the end of `shil_canonical.h` (after `frswap`, `:989-1012`) as `shil_recimp()` (native
implementation required). `shop_ifb` is not reused: its flush is partial for most opcode classes
(`hw/sh4/dyna/regalloc.h:466-498`), it has no conditional block exit, and the optimiser must know that the op reads `sr.T`.

**Invariants the decoder keeps (R4).**
- Size limits ignore check ops: the soft limit test (`:1053-1054`) and the hard `verify` (`:1143`) use
  `oplist.size() - state.dbg_checks`. Otherwise a check could move a block end.
- `guest_cycles`, `guest_opcodes`, `has_readm/has_writem/has_fpu`, `BlockType`, the idle-loop classification and the hash
  (`:1158-1221`) do not see the op.
- `srt_waw` (`hw/sh4/dyna/shil.cpp:832-865`) treats `shop_dbg_check` like `shop_ifb` at `:843-846`: a reader of `sr.T`.
  Without it a T write in front of the check is deleted when a later instruction overwrites T, and the stop shows a stale T.
- The DIV32 idiom matchers (`decoder.cpp:644-696`) decline when an address in `[pc + 2, pc + 130)` is armed, and in
  temporary blocks. (They are off by default: `settings.dynarec.safemode`.)
- `AnalyseBlock`'s "last op sets flags" shortcut (`shil.cpp:935-951`, arm32 `rec_arm.cpp:346-366`) simply does not apply
  when the check is the last op (a check in front of `bt`/`bf`, which emit no op): `enjcond` adds the `jcond`. Semantics
  are unchanged.

**What is flushed.** `shop_dbg_check` is a flush op for the register allocator: `IsFlushOp` (`regalloc.h:374-380`) returns
true for it and `DoAlloc` (`:449-544`) sets `all` and `fp`, i.e. every span is closed in front of it: `r0-r15`, banked
`r0-r7`, `gbr/ssr/spc/sgr/dbr/vbr`, `mach/macl`, `pr`, `fpul`, `sr.T`, `sr.status`, `old_sr`, `fpscr`, `old_fpscr`, `jdyn`
and all 32 FP registers. Spans write back at the end of their last access (`OpEnd`, `:1106-1128`), so the context is
complete when the helper is called; a flush also clears `spans[]`, so no later write can `Kill()` (`:88-91`, `:612`,
`:621`) a write that precedes the check. After the op nothing is cached: every register is reloaded from the context on
its next use, so register writes made by the debugger during a stop are picked up. **PC is not flushed**: it is passed
to the helper and written by the helper on a stop.

**Refund.** The decoder samples its running cost in front of the instruction (`blk->guest_cycles` before the charge at
`:1086-1090`) into `rs2`. After the block cost is final (after `:1221`, and after a cost override, 3.f) it rewrites
`rs2` of every check into the refund:

```
P        = cost accumulated by the per-instruction charges (value of guest_cycles at :1143)
p_i      = the same accumulator in front of instruction i
C        = final guest_cycles
charged_i = P ? floor(C * p_i / P) : 0          // cost of the instructions in front of i
refund_i  = C - charged_i                        // cost of the instructions from i on
```

| timing | example | charged in front of i | refund |
|---|---|---|---|
| fast, ordinary block (`C = 1`) | any i | 0 | 1 |
| fast, idle-class block (`C = 90`, 3 opcodes) | i = 1 | 30 | 60 |
| idleskip off (`C = floor(1.5 P)`) | P = 8, p = 3 | 4 | 8 |
| per-instruction cost models (`C = P`) | | `p_i` | `C - p_i` |

In fast mode a block is charged as a unit, so "the cost up to instruction i" has no natural value; the rule above (the
block cost spread in proportion to the per-instruction charges) is the definition. It only decides what time a stop
*shows*; R7 holds for any `charged_i` in `[0, C]` because the remainder block is charged exactly the refund (3.f).
A cost model that is not a sum (pairing, run-time carries — PLAN T3.2) keeps the rule: the decoder samples whatever its
running cost is in front of the instruction. Requirement on such models: terms charged at run time at block entry or
exit must not be charged twice by stop + remainder (entry terms belong to the interrupted block, exit terms to the
remainder).

**x64 emission** (in the op switch of `BlockCompiler::compile`, `rec_x64.cpp:224-766`), about 60 bytes per site:

```
mov  arg0, pc_i
mov  arg1, refund_i
mov  arg2, block->addr + block->sh4_code_size      ; end of the block
call dbg_sh4_Check                                  ; GenCall (:1251-1272)
test eax, eax
jz   continue
add  dword [cycle_counter], refund_i                ; absolute form under FEAT_NO_RWX_PAGES, as :203-207
add  rsp, STACK_ALIGN + 8                           ; 3.b
jmp  mainloop_exit
continue:
```

**Helper.** `u32 dbg_sh4_Check(u32 pc, u32 refund, u32 block_end)`:
1. resume exemption (3.f): if armed, disarm it; if it is for this masked pc, return 0;
2. find the breakpoint: the one at `pc`, else the one at `pc + 2` (slot rule); none (cannot happen after 3.e) → return 0;
3. evaluate: temporary-breakpoint stack condition (3.h), category mute / solo, later condition and hit count → return 0
   to continue;
4. stop: `Sh4cntx.pc = pc`; record `{pc, block_end, refund}` as the owed remainder; record the stop reason and
   breakpoint id for the park loop; return 1.

The helper reads guest state freely (the context is complete) and writes none when it returns 0.

### 3.b Leaving the dispatcher after a stop

The x64 main loop tests `CpuRunning` only after `UpdateSystem_INTC` (`rec_x64.cpp:148-174`); a stop must not go through
that. A block is entered by `call rax` (`:162`) with the main loop's frame below it (8 pushes + `STACK_ALIGN`, `:136-144`)
and its own frame is `STACK_ALIGN` (`:216`); callee-saved registers are saved by the main loop, not per block
(`x64_regalloc.h:36-42`). So the stop path, emitted in the block, is:

```
add rsp, STACK_ALIGN + 8        ; drop the block frame and the return address into the dispatcher
jmp mainloop_exit               ; the end_loop label (:176): add rsp, STACK_ALIGN; pop ...; ret
```

`mainloop_exit` is the address of `end_loop`, recorded when `build_mainloop` runs. The dispatcher fast path
(`:158-165`) is unchanged. Time does not advance: no `UpdateSystem`, no counter refill.

**The slice counter survives.** `mov [cycle_counter], SH4_TIMESLICE` at main-loop entry (`:146`) is removed; the counter is
initialised statically to `SH4_TIMESLICE`, reset by `SuperH4_impl::Reset`, and (PLAN T3.0) moves into `Sh4Context`.
The interpreter's `l` (`sh4_interpreter.cpp:74`, reset at `:79`) becomes state with the same lifetime. This also makes an
ordinary pause / resume at a slice end exact (E-06).

**Who is told.** The helper records the stop; `recSH4::Loop()` returns to `SuperH4_impl::Run()`, which returns to
`dc_run`, which parks (review E-01 / E-04 define that protocol: stop reason `DBG_PAUSE`, `CpuRunning` cleared by the
helper on the emulator thread).

### 3.c Delay slots

- The decoder never emits a check while `state.cpu.is_delayslot` (`decoder.cpp:1047-1049`).
- In front of an instruction whose table row has the `Delayslot` type bit (`hw/sh4/sh4_interpreter.h:20`; the ten rows
  `braf bsrf rte rts jmp jsr bf.s bt.s bra bsr`, `hw/sh4/sh4_opcode_list.cpp:135-136,155-156,317-318,334,336,338-339`) a
  check is emitted if the branch address **or** `pc + 2` is armed. A stop reports PC = the branch and the id of the armed
  breakpoint (the helper looks up `pc`, then `pc + 2`).
- Branch and slot are always in one block (`decoder.cpp:1054`, `dec_End(..., delay = true)`), and a compiled `bt.s`/`bf.s`
  block runs its slot whether or not the branch is taken (`:185-202`), so the breakpoint fires exactly once per
  execution of the pair, taken or not.
- A block that *starts* at the slot address (it was reached as a branch target) decodes it as an ordinary instruction;
  its check reports PC = that address.
- The page discard of 3.e covers the pair even across a page boundary: the block contains both addresses, so it is in the
  list of the slot's page.

### 3.d Address identity

`dbg_sh4_Mask(addr) = addr & 0x1FFFFFFF` in non-MMU builds. It is applied in: the armed set (stored masked), the
decoder query, the helper lookup, the resume exemption, temporary breakpoints. The page discard uses the block
manager's own index, `(addr & RAM_MASK) / REI_PAGE_SIZE` (`hw/sh4/dyna/blockmanager.cpp:132-133`), which is the same for
every mirror. ROM at `0x00000000+X` and RAM at `0x0C000000+X` stay different addresses.

The PC a stop reports is the address the running block was compiled for. Blocks are looked up by
`fpcb[(addr >> 1) & FPCB_MASK]` (`blockmanager.cpp:46`), so a block compiled as `0x8C…` also runs for `0xAC…`; the
reported PC can then be another mirror of the instruction than the guest jumped to. Clients compare masked; the tick
reports the PC unmasked.

MMU builds: the JIT does not run with `WITH_MMU` today (devcast-build.md 7); for interpreter-type backends P1/P2 addresses
are compared masked and all others exactly.

### 3.e Changing the armed set

```cpp
void dbg_sh4_SetArmed(u32 addr, bool on)      // add / remove / enable / disable; also temporary breakpoints
{
    update the masked set and armed_count
    if (the active backend is a dynarec)
        bm_DiscardAddrPage(addr);              // new: copy page_blocks[(addr & RAM_MASK) / REI_PAGE_SIZE], bm_DiscardBlock each
}
```

- **Completeness.** `bm_AddBlock` enters a block in the list of every page between its first and last byte
  (`blockmanager.cpp:132-143`), so the list of the address's page holds every block that contains it, including blocks
  that start on the previous page and blocks that contain it as a delay slot. The existing `bm_DiscardAddress`
  (`:176-185`) is not used (undeclared, erases while iterating).
- **Thread and point.** On the emulator thread with no guest code on the host stack: in the park loop, or in `dc_run`
  after `Run()` returned for an "apply" request. While running, the network thread's closure sets the request and clears
  `CpuRunning` from the slice hook; the loop ends at that slice end, `dc_run` applies the change and re-enters `Run()`
  without parking (E-03 rule: closures that touch the block manager do not run in the slice hook). Leaving and entering
  the loop is exact (R13). **No block is executing** when the set changes. (The slice hook can run inside a block — the
  `SLEEP` fallback loops on `UpdateSystem_INTC`, `hw/sh4/interpr/sh4_opcodes.cpp:1043-1061` — which is why the hook only
  requests.) `bm_DiscardBlock` itself would be safe there: it moves the block to `del_blocks` and never frees or
  overwrites host code (`:147-174`), exactly as `bm_LockedWrite` does from the fault handler in the middle of a block
  (`libswirl.cpp:1161-1199`).
- **SMC protection.** The discard does not touch `page_has_data` or the page protection. Recompiled blocks get the same
  `DoCheck` result as before (`hw/sh4/dyna/driver.cpp:99-163`, `:266`). A guest write to the page still faults into
  `bm_LockedWrite` (`blockmanager.cpp:342-373`); the blocks compiled afterwards carry the check again because the decoder
  asks at every decode. `FullCheck` compares guest bytes (`rec_x64.cpp:1183-1224`) and is unaffected (R2).
- **ROM and handler-backed code.** The decoder reads opcodes through `ReadMem16`; `bm_AddBlock` files every block under
  `addr & RAM_MASK` whatever the area, so the same index finds it. Aliasing between areas only causes extra discards.
- **Cache reset.** `bm_Reset` (`:201-234`) needs nothing: the armed set lives in the debugger and is consulted at decode.

### 3.f Resume

**Exemption (helper-side).** On every resume and every step the debugger computes
`exempt = WantsCheck(pc, opcode at pc has a slot)` (opcode read through `dbg_mem`) and stores the masked pc. The first
check call clears the flag and, if its pc matches, answers "continue". The flag is only armed when the instruction at PC
has a check, so the first block that runs starts with that check and consumes it; it cannot go stale. It is also cleared
at the next stop.

**Remainder (exact time, R7).** If the stop came from a check in block B at instruction i and the PC is unchanged,
`recSH4::Loop()` first runs the *remainder* as a temporary block (3.g), decoded from `pc_i` with

```
end_pc = B.addr + B.sh4_code_size      // the decoder ends the block in front of this address (BET_StaticJump)
cycles = refund_i                       // replaces the computed cost, after the clamps at :1219-1221
```

then enters the main loop. The remainder block starts with the check for `pc_i` (consumed by the exemption) and contains
the checks of any other armed instruction of B. Result: the instructions `i..end` run once, the total charged for B is
`charged_i + refund_i = C`, the next block starts where B's successor starts, and the slice-end test happens after the
remainder as it would after B. A stop inside the remainder works the same way (its refunds are computed from the
overridden cost).

Why not "a cached block that starts at `pc_i`" (the form D1 sketches): such a block has its own cost (in fast mode 1, or an
idle-class multiple) and, when B ended at a size limit, a different end, so stop + resume would not be neutral. The
remainder cannot be a cached block either: a block registered at `pc_i` must be the one any jump to `pc_i` gets.

The owed remainder is dropped when: the PC at resume differs from `pc_i` (the user set PC), a step ran, the machine
was reset or a state was loaded. Resume then dispatches normally: the first block is the cached (or newly compiled)
block at PC, its first check consumed by the exemption. In that case the stop is not neutral: `charged_i` stays
charged and the following block costs what it costs (fast mode: at most one block cost of skew).

The remainder is decoded from current memory with the current FPSCR and armed set, so breakpoints, registers and
memory changed during the stop take effect.

### 3.g Single step

`SuperH4Backend::Step()` (new virtual; `hw/sh4/sh4_if.h:412-418`). JIT: `rdv_RunTempBlock({max_opcodes = 1})`:

1. Apply the boot soft-reset rule (`driver.cpp:235-241`) and the free-space rule (`:244-248`): `ResetCache()` first.
2. `rbi->Setup(pc, fpscr, limits)`: the decoder ends the block after one instruction; a delayed branch keeps its slot
   (the limit is tested at `decoder.cpp:1053-1057`, never inside a slot). No cost override: the cost is what the cost
   function gives a block of that one instruction (fast mode: 1 cycle, or the idle / syscall-area multiples).
3. `rdv_ngen->Compile(rbi, NoCheck, ...)`: the code was decoded this instant, an SMC entry check is pointless (and its
   failure path, `rdv_BlockCheckFail`, looks the block up in the cache).
4. `bm_SetTempBlock(rbi)`; `rdv_ngen->RunBlock(code)`; `bm_SetTempBlock(0)`.
5. Give the code space back: if `LastAddr` is still where the compile left it, set it back to its value before the
   compile (nothing else compiles while a block runs; a cache clear from inside the block moves `LastAddr` and wins).
   Delete `rbi`.

**Not registered** in `fpcb` / `blkmap` / `all_blocks` / page lists: a cached block at that PC may exist (`bm_AddBlock`
verifies the slot is empty, `blockmanager.cpp:129`) and must stay; a one-instruction block must never be found by the
dispatcher (it would change block boundaries and costs of the run); its life is one execution. The block manager knows
it only through one pointer (`bm_SetTempBlock`) consulted first by `bm_GetBlock(void* code)` (`:74-92`), so that the
memory-access rewrite on a fault (`rec_x64.cpp:1386-1426`) finds its `memory_accesses`.

**`RunBlock`** (new `NGenBackend` entry, generated next to the main loop): the main loop's frame, then

```
call  arg0                       ; the block
mov   ecx, [cycle_counter] ; test ecx, ecx ; jg done
add   ecx, SH4_TIMESLICE ; mov [cycle_counter], ecx
call  UpdateSystem_INTC          ; exactly slice_loop's slow path (:163-173)
done: jmp mainloop_exit
```

Same frame as the main loop, so a stop inside a temporary block leaves through the same `mainloop_exit`.

**Semantics to document.**
- Time: a step is charged like a block. In fast mode stepping n instructions of a block costs n cycles where a run costs
  the block cost; in per-instruction cost modes stepping and running charge the same.
- Slice end: when the step uses up the slice, `UpdateSystem` runs (scheduler events fire) and `UpdateINTC` accepts a
  pending interrupt; the step then ends at `VBR + 0x600`.
- `RTE`: its block end calls `UpdateINTC`; the step can end in the next handler.
- `SLEEP` (`decoder.cpp:274-280`, handler `sh4_opcodes.cpp:1043-1061`): one step waits until an interrupt is accepted
  (ends at the handler) or the handler's bound of 1002 slices = 448 896 cycles passed (ends at `SLEEP` again).
- reios trap (`sh4_opcode_list.cpp:133`): one step executes the whole HLE call.
- A step drops an owed remainder.
- The park loop sets `CpuRunning` for the duration of `Step()`, so `UpdateSystem`'s return value and the `SLEEP` handler
  behave as in a run; a host stop request during the step clears it and is honoured when the step returns.

**Preconditions (R14)**, checked by the debugger before calling `Step()`, opcodes read through `dbg_mem`:
PC even; opcode readable; `OpDesc[op]->oph != iNotImplemented` (it `die()`s in NO_MMU builds,
`sh4_opcodes.cpp:73-79,2175-2185`); for a `Delayslot` opcode the slot opcode readable, defined and without `WritesPC`
(the decoder `verify`s that, `decoder.cpp:1092`). Failure → RPC error, nothing executed.

### 3.h Step over and step out

**Temporary breakpoints** are entries of the armed set with a stack condition, added and removed with
`dbg_sh4_SetArmed` (page discard), never listed, never muted, removed at every stop whatever caused it.

**Stack test**, shared: `OnLevel(sp, sp0) = sp >= sp0 && sp - sp0 < 0x100000` — "at the level of the request or above
it, on the same stack". Below `sp0` is a deeper activation; 1 MiB above is another stack (guest thread).

**Step over.** At `BSR / BSRF / JSR`: temporary breakpoint at `PC + 4`, `sp0 = R15`, resume; the helper stops only if
`OnLevel(R15, sp0)`. At `TRAPA`: the same at `PC + 2`. Any other instruction: a step. (`PR = PC + 4`:
`sh4_opcodes.cpp:890-901,997-1006,1029-1040`.)

**Step out.** "Stop after the first RTS/RTE with `R15 >= sp0`" (review E-08) is wrong twice: a callee that uses no stack
returns with `R15 == sp0`, and the RTE of an interrupt taken in the function's body lands on `R15 == sp0` as well. The
rule is:

State: `sp0 = R15` at the request, `level = 0`, `calls = 0`. The first matching row applies.

| event (reported after the instruction and its slot ran) | action |
|---|---|
| interrupt / exception accepted (`Do_Interrupt`, `Do_Exception`, `hw/sh4/sh4_interrupts.cpp:153-190`) | `level++` |
| `RTE`, `level > 0` | `level--` (a handler entered after the request, skipped whole) |
| any event, `level > 0` | ignore |
| call (`BSR/BSRF/JSR`), `R15 == sp0` | `calls++` |
| `RTS` or `RTE`, `!OnLevel(R15, sp0)` | ignore |
| `RTS`, `R15 == sp0`, `calls > 0` | `calls--` (return of something called at the request level) |
| `RTE`, `R15 == sp0`, `calls > 0` | ignore |
| `RTS` or `RTE` otherwise (`R15 > sp0`, or `R15 == sp0` with no call outstanding) | **stop**, reason `step` |

**Instrumentation.** While a step-out is pending (`dbg_sh4_stepout != 0`) the decoder marks blocks by their end type:
`BET_StaticCall`, `BET_DynamicCall` → call, `BET_DynamicRet` → RTS, `BET_DynamicIntr` → RTE (`decoder.h:26-33`; a field
`RuntimeBlockInfo::dbg_flow`). The backend epilogue of a marked block, after it has written `next_pc`
(`rec_x64.cpp:770-822`), calls `dbg_sh4_CheckFlow(kind)` and on non-zero leaves through `mainloop_exit`. For an RTE block
the report comes **before** `UpdateINTC` (`:825`) and `UpdateINTC` still runs before leaving: if the RTE lets an interrupt
in, the stop is at the handler entry. No SHIL op and no register flush are needed: at the epilogue every span is
written back. Block boundaries and costs are unchanged.

**Which blocks are discarded.** Not pages: at the request, every cached block whose end type is one of the four above
(`bm_DiscardFlowBlocks(false)`, a scan of `all_blocks`); when the step-out ends or is cancelled by any stop, every block
with `dbg_flow` set (`bm_DiscardFlowBlocks(true)`). Both on the emulator thread while parked. Alternatives rejected:
`ResetCache()` (also clears `page_has_data`, changing the SMC handling of later blocks, `blockmanager.cpp:218-228`); a
permanent flag test in every call / return epilogue (run-time cost with no debugger); single-stepping until the return
(slow, and in fast mode it changes the time charged); a temporary breakpoint at `PR` (wrong in any function that has made
a call, E-08).

**Limits (documented in the UI help).** A function that does not return by `RTS` (tail call by `JMP`, context-switch
primitive resumed by `RTE`) is left at the next enclosing return. A request made while extra words are pushed below the
function's steady stack pointer can stop at the return of a later callee (still inside the function). Another guest
thread whose stack lies within 1 MiB above `sp0` can satisfy the test.

### 3.i Interpreter-type backends

`SH4IInterpreter::Loop()` runs a separate debug loop while `armed_count != 0` or a step-out is pending; the normal loop
(`sh4_interpreter.cpp:81-114`) is untouched. Same slice structure and counter as the normal loop, one iteration:

```
addr = next_pc; op = IReadMem16(addr)
if (addr == slot_pc)            slot_pc = none                       // see below
else if (WantsCheck(addr, has_slot(op)) && dbg_sh4_Check(addr, 0, 0)) return   // stop: nothing ran, nothing charged
next_pc = addr + 2; ExecuteOpcode(op)
if ((OpDesc[op]->type & Delayslot) && next_pc == addr + 2) slot_pc = addr + 2
l -= cost
if (step-out pending) { report call / RTS; if (stop pending) return }
```

- A stop returns from the loop with `next_pc = addr` and `l` unchanged; resume re-enters with the exemption armed.
  No remainder exists (`block_end = 0`); the loop is exact by construction.
- **Untaken `BT/S`, `BF/S`**: the interpreter runs the slot as a separate instruction
  (`sh4_opcodes.cpp:948-957,972-981`). `slot_pc` suppresses the check in front of it once, so the breakpoint fires once,
  in front of the branch, as in the JIT (R9). (Review E-09's "only if the branch will be taken" is replaced.)
- `Step()`: one iteration plus the end-of-slice work; if the instruction was an untaken delayed branch, the slot too, so
  a step is the same unit in both backends.
- Step-out: calls and `RTS` are reported by the loop after `ExecuteOpcode`; `RTE` reports from its handler between
  `next_pc = newpc` and the interrupt check (`sh4_opcodes.cpp:905-921`), the same order as the JIT epilogue.
- The cycle-accurate backend (PLAN track 4) follows this section.

### 3.j Other JIT backends, and the fallback

A backend states `ngen_features::DebugChecks` (new flag, `decoder.h:47-51`, filled in `GetFeatures`). It may set it when
it implements all of:

| item | x64 (prototype) | arm64 | arm32 | x86 | cpp (cached SHIL) |
|---|---|---|---|---|---|
| `shop_dbg_check` | 3.a | same shape; flush comes from the shared `regalloc.h` | same | own register allocator: flush all | an `opcodeExec` that calls the helper |
| leave without time advance | `jmp mainloop_exit` | store `next_pc`, branch to `.end_mainloop` (`rec_arm64.cpp:173-184`, needs a global label) | branch to `cleanup` (`ngen_arm.S`) | jump to the loop epilogue (`rec_lin86_asm.S`) | `longjmp` to a `setjmp` at `rec_mainloop` entry (`rec_cpp.cpp:823-841`); the op chain has no early exit (`:762-776`) |
| refund | `add [cycle_counter]` | `add w27` in the stop path | `add r9` | `add [cycle_counter]` | `cycle_counter +=` |
| counter survives | static init | `w27` loaded from / stored to the context at loop entry / exit (`:133`) | `r9` likewise (`ngen_arm.S:150`) | drop `mov cycle_counter, 448` (`rec_lin86_asm.S:106`) | drop `cycle_counter = 0` (`:827`), refill after the inner loop |
| `RunBlock` | generated thunk | block must end by returning to the thunk, not by linking (`RelinkBlock`, `:868-952`; `rdv_LinkBlock` would chain it) | same | same | call `execute()` once |
| step-out epilogue report | 3.h | in `RelinkBlock` for call / ret / intr ends, before the link or lookup | same | same | in the block-end op |
| `Sh4cntx.pc` at a stop | helper writes it | same (the PC lives in `w29` between blocks, E-21) | same (`r4`) | same | same |

arm32 / arm64 / x86 test the counter at block *entry* and restart the block after an interrupt
(`rdv_DoInterrupts`, `driver.cpp:328-352`); a check stop is independent of that path.

**Fallback (only for a JIT backend with `DebugChecks == false`).** Selected by exactly that flag, read once after
`setBackend`: SH4 breakpoints and steps then run in the interpreter debug loop of 3.i
(`SuperH4_impl::Run()` chooses it while the armed set is non-empty, a step-out is pending or a step is requested; arming /
disarming while running uses the "apply" path of 3.e to switch loops; the JIT's block cache is kept). Time: the fallback
charges **by the decoder's blocks**: at each block start it decodes the block at PC (`RuntimeBlockInfo::Setup`, no
compile), charges its `guest_cycles`, and interprets instructions until the block's end (control transfer, or
`next_pc == addr + sh4_code_size`) before it tests the counter. That is the active timing mode's charge for every mode
(the decoder alone defines block boundaries and costs). What it cannot match is arithmetic (the interpreter's FPU paths
differ from the JIT's, devcast-fpu.md). The UI is told through `debugger.describe`: `capabilities` contains
`"sh4-debug-native"` when `DebugChecks` is set, otherwise `"sh4-debug-fallback"`, and every paused or running tick while
the fallback loop is active carries `executionState.note = "SH4 runs on the interpreter while breakpoints are armed
(JIT backend <name> has no native breakpoints): FPU results can differ from a run without breakpoints"`. If a fallback
is shipped without block charging, the note must also say that timing differs.

---

## 4. Interface between backends and debugger

Declared in `debugger/debugger.h` (the one header emulator code includes); everything is emulator-thread only.

```cpp
#define DBG_SH4_ADDR_MASK 0x1FFFFFFF
extern u32 dbg_sh4_armed;                                   // number of armed addresses
bool dbg_sh4_IsArmed(u32 pc);
static inline bool dbg_sh4_WantsCheck(u32 pc, bool has_slot);   // 3.a; has_slot: the opcode has the Delayslot type bit
bool dbg_sh4_AnyArmed(u32 pc, u32 bytes);                   // idioms that fold instructions
extern "C" u32 DYNACALL dbg_sh4_Check(u32 pc, u32 refund, u32 block_end);   // 3.a helper
extern u32 dbg_sh4_stepout;                                 // decoder: instrument call / return blocks
extern "C" u32 DYNACALL dbg_sh4_CheckFlow(u32 flow);        // 3.h; 1 call, 2 RTS, 3 RTE
void dbg_sh4_OnException();                                 // from Do_Interrupt / Do_Exception
bool dbg_sh4_StopPending();                                 // did the loop return because of a check
bool dbg_sh4_TakeRemainder(u32 pc, u32* end_pc, u32* cycles);   // 3.f; hands it out once
```

Backend side:

```cpp
struct SuperH4Backend { ... virtual bool Step(); virtual int SliceCyclesLeft(); };   // sh4_if.h
struct DecodeLimits { u32 max_opcodes; u32 end_pc; s32 cycles; };                    // decoder.h
void dec_DecodeBlock(RuntimeBlockInfo*, u32 max_cycles, const DecodeLimits* = 0);
void RuntimeBlockInfo::Setup(u32 pc, fpscr_t, const DecodeLimits* = 0);
void rdv_RunTempBlock(const DecodeLimits*);                                          // driver.cpp
void bm_DiscardAddrPage(u32 addr); void bm_DiscardFlowBlocks(bool instrumented_only);
void bm_SetTempBlock(RuntimeBlockInfo*);                                             // blockmanager.h
struct NGenBackend { ... RunBlockFnPtr_t RunBlock; };  struct ngen_features { ... bool DebugChecks; };
```

`SliceCyclesLeft()` gives `dc.timing` the cycles inside the running slice.

---

## 5. Files and ownership

One package ("E4b sh4-native-debug"), after E0; it must own or co-own:

| File | Change |
|---|---|
| `hw/sh4/dyna/decoder.cpp`, `decoder.h` | query + `shop_dbg_check` emission, size limits without check ops, DIV32 rule, `DecodeLimits`, refund rewrite, `dbg_flow`, `ngen_features::DebugChecks` |
| `hw/sh4/dyna/shil_canonical.h`, `shil.cpp` | the op; `srt_waw` T-reader rule |
| `hw/sh4/dyna/regalloc.h` | flush rule (shared by x64, arm32, arm64) |
| `hw/sh4/dyna/blockmanager.h/.cpp` | `bm_DiscardAddrPage`, `bm_DiscardFlowBlocks`, temporary-block pointer, `dbg_flow` (co-owned with E3, which adds its write helper there, E-14) |
| `hw/sh4/dyna/driver.cpp`, `ngen.h` | `rdv_RunTempBlock`, `recSH4::Loop` remainder, `recSH4::Step`, `RunBlock`; banner prints move out of `Loop()` |
| `jit/backend/x64/rec_x64.cpp` | check emission, `mainloop_exit`, `RunBlock` thunk, epilogue report, counter not reset at entry |
| `hw/sh4/sh4_if.h`, `SuperH4_impl.h/.cpp` | `Step()` plumbing, counter reset in `Reset` (with E4) |
| `hw/sh4/interpr/sh4_interpreter.cpp`, `sh4_opcodes.cpp` (RTE) | debug loop, step, persistent counter, RTE report (replaces E-05's three functions) |
| `hw/sh4/sh4_interrupts.cpp` | `dbg_sh4_OnException` next to the `dc.sh4.interrupt` / `.exception` events |
| `debugger/debugger.h` (through the integrator), `debugger/dbg_emu.cpp` | section 4; armed set, exemption, remainder, step-over / step-out state |
| `tests/debugger/sh4bp/` | section 6 |

Order against the timing tracks: this package lands before PLAN T3.0 / T3.2 edit the decoder's cost code and the x64
prologue; T3 keeps: the running cost is sampled in front of each instruction; a `DecodeLimits::cycles` override is
honoured after all cost post-processing; the counter survives loop exit.

---

## 6. Tests the implementation must pass

Headless, 2ndmix.elf under reios, fixed RTC, `none` renderer. "State" = scheduler time, slice counter, PC, a hash of the
SH4 context and a hash of main RAM at a fixed vblank. Every state comparison uses a baseline taken in the same session
and at least two baseline runs must agree (B.6).

| # | Test | Pass |
|---|---|---|
| T1 | breakpoint at a block start, in the middle of a block, on the first instruction after boot | PC = address; integer registers = interpreter's at the first arrival |
| T2 | breakpoint in a loop, resume each time: at block start, mid-block, on the slot of the loop's `bf.s` | hit count = interpreter's; slot case reports PC = branch; time at the loop exit = time without the loop breakpoint |
| T3 | state at vblank N with: checks that all answer continue; stop + resume at every arrival (ordinary block, idle-class block, `SLEEP` block, slot); two breakpoints in one block; `idleskip` off; blocks ended by the size limit; pause / resume at slice ends | state = baseline in every case |
| T4 | 20000 single steps from the entry point | PC and integer registers after every step = interpreter's |
| T5 | step over the reios trap; step on `SLEEP`; step with odd PC, undefined opcode, branch in a slot | lands at `0x8C010000`; ends at the handler or at `SLEEP`; RPC error, process alive |
| T6 | arm an address in an already compiled, hot block; disarm after k hits | it hits; after disarming the check counter stops and state at vblank N = baseline |
| T7 | step over a call, a recursive call, `TRAPA`; step out of a leaf, of a non-leaf after it called a stack-less leaf, out of a blocking call across interrupts; step-out pending for the whole run | stops at `PC+4` / `PC+2` at the request level; stops at the caller with `R15 >= sp0`; state at vblank N with a never-finishing step-out = baseline; interpreter and JIT stop at the same PC |
| T8 | breakpoint given as `0x0C…`, `0x8C…`, `0xAC…` | all hit |
| T9 | guest write into a page with an armed address (SMC path), then re-execution | hits again |
| T10 | set PC / reset / load state while stopped in a block | resume does not run a stale remainder |
| T11 | host instructions of the emulator thread to vblank N: no hook, hook with nothing armed, one armed address never reached | first two within run-to-run spread; third within 0.01 % |
| T12 | a backend without `DebugChecks` | `describe` reports the fallback; breakpoints work; state at vblank N with continue-only checks = baseline (block charging) |

---

## 7. Open risks

1. **Guest threads and stack-level conditions** (3.h). Step-out and step-over identify "the same activation" by R15.
   Kernels that switch stacks defeat that in the listed ways. A kernel-aware thread id is not available.
2. **Blocks keyed by PC only.** A cached block compiled under another FPSCR.PR/SZ runs with stale decode assumptions
   (`blockmanager.h:39`, no entry check); temporary blocks decode with the current FPSCR. Step and run can differ exactly
   where the run is already wrong. PLAN T3.4 (blocks keyed on PR/SZ) removes it.
3. **Stop + resume costs a compile** (about 33 k host instructions on x64, B.5). Anything evaluated per arrival (hit
   counts, conditions, log points) must be answered in the helper, where an arrival costs about 35 instructions.
4. **Pause latency inside `SLEEP`** is up to 1002 slices unless the handler also leaves on `CpuRunning == 0` (E-07); the
   "apply" path of 3.e inherits it.
5. **Linking backends** need a non-linking block end for temporary blocks; until then they use the fallback, whose FPU
   arithmetic is the interpreter's.
6. **Cost models with run-time terms** (PLAN T3.2 carries, taken-branch costs) must respect 3.a's requirement or R7
   breaks silently; T3 gates need test T3 of this document.
7. **`fpcb` aliasing** between the boot ROM and RAM (`driver.cpp:235-241` clears the cache at three boot addresses) is
   untouched. `rdv_RunTempBlock` applies the same rule, so stepping through those addresses clears the cache as the
   dispatcher would; the mirror a stop reports (3.d) follows from the same lookup.
8. **Host determinism.** One run in about 150 took a different guest path with identical inputs (B.6). Gates that
   compare state hashes need a retry policy.
9. Not prototyped: MMU builds, arm / x86 / cpp backends, the fallback, the step preconditions, the park protocol
   (E-01, E-04) around the stop. The emitted x64 code was only run with the System V ABI; the Windows paths
   (`STACK_ALIGN` 40, argument registers) follow the existing `WIN32_ONLY` conventions of `rec_x64.cpp` but are untested.

---

## Appendix A — prototype

Branch `skmp/nulldc3-proto-bp` (commits `947f0eca`, `12e46c3e`, `540d4767`, `8e693134`, `d49b8f58` on `34a62ea8`; not to
be merged), worktree
`agents/nulldc3/wt/proto-bp`; builds, logs and the run script under `agents/nulldc3/proto-bp/`
(`build/hle` = TA_HLE build with the `none` renderer; `build/hle-nohook` = same source with `-DSH4DBG_NO_DECODER_HOOK`;
`run/run.sh <tag> <seconds> <binary> <ENV=..>... -- <args>`; logs in `logs/<tag>.log`).

The debugger is replaced by `hw/sh4/sh4_debug.{h,cpp}` (names `sh4dbg_*` = section 4's `dbg_sh4_*`) driven by
environment variables, and the park loop by `sh4dbg_ProtoPark` called from `SuperH4_impl::Run()`.

| Variable | Effect |
|---|---|
| `NULLDC_DBG_BP=a,b,..` | armed addresses (hex) |
| `NULLDC_DBG_ANSWER=continue` | every check answers continue |
| `NULLDC_DBG_MAXHITS=n` | stop at the first n arrivals, then answer continue |
| `NULLDC_DBG_EXIT_AFTER=n`, `NULLDC_DBG_EXIT_AT=a` | exit at the n-th stop / when address a stops |
| `NULLDC_DBG_STEP=n` | at the first stop single step n times (dump after each), exit |
| `NULLDC_DBG_STEPOVER=1`, `NULLDC_DBG_STEPOUT=n` | at the first stop: step over / step out n times, exit |
| `NULLDC_DBG_AT_HIT=k:+a,k:-a` | at the k-th stop arm / disarm address a |
| `NULLDC_DBG_PAUSE_EVERY=n` | leave and re-enter the backend loop at every n-th slice end |
| `NULLDC_DBG_MAX_CYCLES=n` | block size limit in cycles (makes blocks end at the limit) |
| `NULLDC_DBG_ANCHOR=n` | at vblank n print time, counter, PC, context hash, RAM hash; exit |
| `NULLDC_DBG_FIXED_RTC=t` | host time for the RTC (the only host input that changed RAM between runs) |
| `NULLDC_DBG_BLOCKLOG`, `_PCSAMPLE`, `_QUIET`, `_RAMDUMP` | diagnostics |

Prototype code by section: 3.a `decoder.cpp` (query, limits, refund rewrite), `shil_canonical.h`, `shil.cpp`,
`regalloc.h`, `rec_x64.cpp` (`case shop_dbg_check`); 3.b `rec_x64.cpp` (`mainloop_exit`, `GenLeaveMainloop`, counter
init); 3.e `blockmanager.cpp` (`bm_DiscardAddrPage`), `sh4_debug.cpp` (`SetArmed`); 3.f `driver.cpp` (`recSH4::Loop`),
`sh4_debug.cpp` (`ArmExemption`, `sh4dbg_TakeRemainder`); 3.g `driver.cpp` (`rdv_RunTempBlock`, `recSH4::Step`),
`rec_x64.cpp` (`build_runblock`), `blockmanager.cpp` (temporary pointer); 3.h `sh4_debug.cpp` (`sh4dbg_CheckFlow`),
`decoder.cpp` (`dec_BlockFlow`), `rec_x64.cpp` (epilogue), `blockmanager.cpp` (`bm_DiscardFlowBlocks`); 3.i
`sh4_interpreter.cpp` (`DebugStep`, `LoopDebug`, `Step`), `sh4_opcodes.cpp` (RTE).

Not in the prototype: the real debugger, the park protocol, `CpuRunning` handling at a stop, step preconditions, other
backends, the fallback, MMU builds.

## Appendix B — measurements

Host: x86-64 Linux (WSL2), 24 cores, load average 24-40 from unrelated jobs during all runs. Guest: 2ndmix.elf under
reios, `-config audio:backend=none , config:pvr.backend=none`, `NULLDC_DBG_FIXED_RTC=946684800`. 2ndmix here spends its
time in the KOS idle thread (`SLEEP`), a busy-wait in `play_s3m`, the PVR submission path and interrupt handlers.

**B.1 Breakpoint at a block start / mid-block vs the interpreter (first arrival).**

| address | position | JIT PC at stop | integer registers vs interpreter |
|---|---|---|---|
| `8C01003E` | first instruction of a 19-opcode block | `8C01003E` | identical |
| `8C01004E` | 9th instruction of the same block | `8C01004E` | identical |
| `8C026ADE` | 16th instruction of a 31-opcode block | `8C026ADE` | identical |

**B.2 Resume and next arrival** (memset's 5-opcode loop block `8C03F95C..8C03F966`, exit at its return `8C01A63C`).

| breakpoint | reported PC | hits JIT | hits interpreter | time at `8C01A63C` (JIT) |
|---|---|---|---|---|
| none | — | — | — | sched 150052, counter 94 |
| `8C03F95C` (block start) | `8C03F95C` | 5209 | 5209 | sched 150052, counter 94 |
| `8C03F960` (mid-block) | `8C03F960` | 5209 | 5209 | sched 150052, counter 94 |
| `8C03F964` (slot of `bf.s`) | `8C03F962` | 5209 | 5209 | sched 150052, counter 94 |

Registers at the first three stops identical to the interpreter's in all three cases.

**B.3 Timing neutrality, JIT** — state at vblank 1500. Baseline: sched 5007487716, counter 198, PC `8C024CDE`,
ctx `BDF0839F2B140A61`, ram `EB5B555450433806` (3 baseline runs identical).

| run | check calls | stops | state |
|---|---|---|---|
| 4 addresses in hot blocks, helper answers continue | 15 732 865 | 0 | = baseline |
| `8C0226BA` (mid-block, PVR path), stop + resume every arrival | 10 343 096 | 5 171 548 | = baseline |
| `8C0226C0` (slot of a `jsr`), every arrival | 10 343 096 | 5 171 548 | = baseline |
| `8C0104EE` (mid-block of an idle-class block, cost 90), first 100 000 arrivals | 5 477 440 | 100 000 | = baseline |
| `8C024CDC` (`SLEEP` block) | 24 658 | 12 329 | = baseline |
| `8C0226B8` + `8C0226BC` (same block), first 200 000 | 10 543 096 | 200 000 | = baseline |
| disarm `8C0226BA` at its 1000th stop | 1 999 | 1 000 | = baseline |
| pause + resume at every 7th slice end (227 351 pauses) | 0 | 0 | = baseline |
| pause every 5th slice end + `8C0226BA`, first 200 000 | 5 371 548 | 200 000 | = baseline |
| step-out pending from vblank ~170 to the end (call / return blocks instrumented) | 5 171 549 | 1 | = baseline |
| `idleskip=no`: own baseline vs 3 addresses, first 300 000 stops | 126 504 498 | 300 000 | = its baseline (ctx `FFAE3992ECEA6C35`) |
| block limit 3 cycles (all blocks end at the limit): own baseline vs 4 addresses, first 300 000 stops | 11 092 878 | 300 000 | = its baseline (ctx `C83360AA9DE10B41`) |

Per-stop skew: 0 cycles in every case. Interpreter (vblank 600): normal loop = debug loop with 22.5 M continue answers =
2 000 000 stops + resumes = 1 440 309 pauses (ctx `F7C4E699FEBF709F`).

**B.4 Single step.** 20000 steps from `8C010000`: PC and integer registers after every step identical between JIT
temporary blocks and the interpreter (44 slice ends crossed in the JIT, 357 in the interpreter). JIT fast mode charges 1
cycle per step, the interpreter 8. Step from `A0000000` executes the reios boot and lands at `8C010000`. Steps on `SLEEP`
at `8C024CDC`: a step that finds no interrupt advances 1002 slices (448 896 cycles) and ends at `SLEEP`; the step in
which an interrupt is accepted ends at the handler `8C02D834` (JIT: the 4th step, interpreter: the 5th). Step over `jsr` at `8C01A614` stops at `8C01A618`. Step out: leaf `8C01AF14` → `8C02964C`;
non-leaf `8C029634` → `8C01A61E`; from the memset loop → `8C01A63C` at sched 150052 counter 94 (the time of B.2); from
`pvr_prim` (`8C0226BA`, which then calls a stack-less leaf) → `8C010762`, then → `8C011104`; from `genwait_wait` entry
(blocks, 3 interrupts and thread switches) → its return address `8C0246E2` at the same R15; interpreter and JIT stop at
the same PC and R15 in the cases run on both.

**B.5 Cost** — user-mode instructions of the emulator thread from the first `Run()` to vblank 4000 (hardware counter),
5 runs each (4 for the last row), same source:

| build / setup | instructions (min … max) | vs no hook |
|---|---|---|
| decoder query compiled out | 46 685 360 852 … 46 685 476 691 | — |
| query present, nothing armed | 46 685 346 255 … 46 685 505 428 | +0.5 ppm (means); inside the 2.5 ppm run-to-run spread |
| one armed address never reached (`8C024CE2`) | 46 686 029 497 … 46 686 164 622 | +14 ppm |
| 4 armed addresses, 34 858 102 check calls answering continue | 47 906 275 486 … 47 906 450 767 | +2.6 %; 35 instructions per call |

Thread CPU time to vblank 4000 (min of 5): 7.95 s, 8.00 s, 8.31 s — 500 vblanks/s, 8.3x real time, indistinguishable under
the machine's load. Stop + resume: about 32 700 host instructions (14.5 µs) each (400 000 stops); pause + resume at a
slice end: about 800 instructions. Interpreter debug loop with 4 armed addresses: 2.1x the instructions of the normal loop
(the prototype's query is a binary search; a per-page bitmap in front of it is the obvious fix).

**B.6 Host determinism.** With a fixed RTC, runs with identical inputs gave identical state except one (a no-hook
build without breakpoints, vblank 4000: 3128 blocks compiled instead of 2687, three times the instructions, different
context and RAM hashes). Cause not investigated; the window was on a shared desktop, an input event is the likely source.

## Appendix C — deviations from D1 and from review proposals

| # | D1 / proposal | This specification | Reason |
|---|---|---|---|
| C1 | D1: after a stop, resume runs "a block compiled starting at that pc", first instruction exempt | The rest of the interrupted block runs as a temporary block with the interrupted block's end and the refunded cost; the helper-side exemption is kept and consumed by it | A cached block at that PC has its own cost (fast mode: 1 or an idle multiple) and, after a size-limit end, its own boundary; stop + resume would not be neutral. Measured exact with the temporary block (B.2, B.3) |
| C2 | D1: "cycles of the unexecuted remainder are refunded, so scheduler time is exactly what the active timing mode charges up to that instruction" | Refund = block cost minus its proportional share for the instructions in front; exact in per-instruction models, a definition in fast mode (an ordinary block shows 0 charged until it completes) | Fast mode charges blocks, not instructions |
| C3 | D1: single step = one-instruction block "through the same cost function" | As stated; consequence spelled out: in fast mode n steps cost n cycles where the run costs one block | Inherent in the decision |
| C4 | Task (h) / E-08: step out = first RTS/RTE executed with `R15 >= sp0` | Stack-level rule with calls at the request level counted and handlers skipped; call blocks are instrumented too; block discard by end type, not by page | The simple rule stopped at the return of a stack-less callee in the prototype (step-out of `pvr_prim` stopped inside it) and would stop at any interrupt return into the function body |
| C5 | E-09: delay-slot breakpoint hits only "if the branch will be taken" | Always in front of the branch; the interpreter suppresses the second check on an untaken slot | Task (c); makes JIT and interpreter agree |
| C6 | E-05 / E-06: interpreter debug loop for every backend, UI warns that timing differs | Only as the fallback of 3.j, charging by decoder blocks | D1 |
| C7 | Dispatcher today resets the slice counter at entry | Counter survives; also changes (corrects) an ordinary GUI pause / resume | R13, E-06 |
| C8 | Decoder | Size limits ignore check ops; DIV32 idiom not folded over an armed address or in temporary blocks | R4; otherwise a check could move a block end or be skipped |
