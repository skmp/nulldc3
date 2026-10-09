# debugger.contract: the debugger's server against the contract of the devtools UI

The server of the emulator (`libswirl/debugger`: the transport, the core, the emulator side) speaks protocol v3
with the devtools UI. The UI repository has the contract runner that holds a server to that protocol
(`npm run contract`, `scripts/contract/`); the tests of this directory start the emulator and run it, and test
what belongs to the server and is no scenario of the runner.

    python3 tests/debugger/contract/contract.py --emulator <devcast> --work /tmp/w --guest <2ndmix.elf> \
            --mode m1 --devtools <dreamcast-devtools checkout> --keep

Every emulator is started as `tests/system` and the selftests start one: headless (`config:Headless=yes`), no
audio device, an environment of its own (fresh `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME` under the work
directory, no display), the real-time clock pinned, a core size limit of 1 byte and a watchdog; a process is
ended by its pid. The tests run on the emulator that the system tests build (`<build dir>/system/emulator`, or
`-DNULLDC_EMULATOR=`), and are skipped where those are. No embedded UI bundle is needed.

All of them boot `2ndmix.elf` of `NULLDC_ELF_DIR` (the KallistiOS example: it runs under reios, starts the ARM7,
has a `.symtab`, entry `0x8C010000`). The tests that run the contract runner need the dreamcast-devtools checkout
with its `node_modules` (`-DNULLDC_DEVTOOLS_DIR=`) and `npm`; without them they are tests whose data is missing.

| test | what has to hold | needs |
|---|---|---|
| `debugger.contract.m1` `.m1.interp` `.m1.fallback` | The gate of milestone M1. The emulator is started as the design says (5.4): `Debugger:Enabled=yes , Debugger:Port=0 , Debugger:Required=yes , Debugger:StartPaused=yes`, with the default tick period; the port is read from the line `Debugger: http://127.0.0.1:<port>/` on its stdout; then `npm run contract -- --url ws://127.0.0.1:<port>/ws --profile emulator --elf <2ndmix.elf> --milestone M1` runs in the devtools checkout. The runner has to end with exit code 0 and `contract: OK`, with every selected scenario passed (S00 to S15, S19, S20: none failed, none partial, none not applicable) and no invariant violated; the emulator has to be alive when the runner has ended, and ends within 20 s of SIGTERM (emulated time stands still at a stop, so no exit key would end it). On the JIT, on the interpreter (`config:Dynarec.Enabled=no`), and on the JIT with `Debugger:ForceFallback=yes`, where breakpoints and steps come from the interpreter fallback and the ticks carry its notice. The report of the runner is the output of the test | the devtools checkout, npm |
| `debugger.contract.grace` | The grace period after the last client left. A client resumes the target from its start park and lets it boot, adds an enabled code breakpoint (on the entry of the program, which is behind it) and a disabled event breakpoint, two watches and a muted category, pauses the target and disconnects. A client that connects 3 s later finds the stop it left (same `stopId`) and leaves again. 11 s after that a third client finds the target running, with `stopId` unchanged, the two breakpoints, the two watches and the categories as they were, and one `dc.debugger.note` entry in the event log: the one the core appended with no client connected, when it cleared the active mask and released the stop. The client is `wsclient.py` of this directory, which needs nothing but the Python library | nothing more |
| `debugger.contract.config` | The configuration errors of the keys that need the server: three headless runs, none with an exit key, each of which has to end by itself within 60 s: `Debugger:Required=yes` without `Debugger:Enabled`, `Debugger:StartPaused=yes` without it, and `Debugger:Required=yes , Debugger:Enabled=no`. Each ends with exit code 2 and a line on stderr that names the key and says that `Debugger:Enabled=yes` is missing; none prints a `Debugger: http://` line or a `nulldc-exit` line | nothing more |
| `debugger.contract.hash.off` `.interp` | 2ndmix for 600 vblanks (`config:Exit.AfterVblanks=600 , config:Exit.StateHash=yes`) with no `Debugger:` key at all, and with `Debugger:Enabled=no`: the two exit lines have the same state hash, part for part (`reason`, `vblanks`, `hash`, `sh4`, `sched`, `ram`, `vram`, `aram`, `arm7`). And the default of an automated run, which a hash cannot show (a server without a client leaves the hash alone): the run without a `Debugger:` key prints no `Debugger: http://` line, and a third run, with `Debugger:Enabled=yes , Debugger:Port=0`, prints it on stdout | nothing more |
| `debugger.contract.hash.noclient` `.interp` | The same two hashes for `Debugger:Enabled=no` and for `Debugger:Enabled=yes , Debugger:Port=0` with no client: a server that nobody connects to leaves the run alone | nothing more |
| `debugger.contract.hash.idlebp` `.interp` | The same two hashes for `Debugger:Enabled=no` and for a run that the contract runner is connected to in its mode `--idle-breakpoint stars_one_frame`: the run starts at its `StartPaused` stop, the runner mutes the category `sh4`, adds a breakpoint on that symbol, resumes and stays connected until the emulator has run its 600 vblanks and ended. The runner has to end with exit code 0 (the muted breakpoint never stopped the target, no invariant was violated) | the devtools checkout, npm |

No hash is stored in the tree: a test makes its runs itself and compares them with each other.

## The address of the idle breakpoint

`hash.idlebp` is about a breakpoint that is listed, armed and never stops (R5 and R13 of the design on a real
program): the blocks of its page are compiled with the check, the check answers "continue" at every arrival.
That says something only for an address the program arrives at; a muted breakpoint reports nothing, so an address
that is never executed would pass the test as well (the entry `0x8C010000` runs once, at the boot, and is such
an address).

The symbol the CMake line names is `stars_one_frame` (`_stars_one_frame`, `0x8C010748` in the 2ndmix.elf the
test was written with): the function of 2ndmix that moves and draws its star field, called from the main loop
once in every frame. That it is reached was shown with the server itself: the emulator at its `StartPaused`
stop, an enabled breakpoint on the symbol with nothing muted, and `control.runUntil` twelve times in a row. Every
resume was followed by the stop of that breakpoint (`reason: "breakpoint"`, PC `0x8C010748`): the first 262 ms
after the resume (the boot and the set-up of the program, 166 frames of emulated time), the second within the
same frame (the program calls the function twice before its first frame is over), and every one after that
1.6 to 3.5 ms after its resume and 3,335,817 to 3,337,097 SH4 cycles after the stop before it, which is one
frame of 59.94 Hz (3,336,670 cycles). The selftest `core.guest` stops on the same symbol.

## Where the mock of the UI repository and this server differ

Both implement the protocol, and the runner passes on both. Where they differ the design decides; these are the
differences that a client could see (design section in brackets):

* `breakpoints.setCategoryStates`: the mock validates the value of every key of `categories`, also of keys that
  are no category, and answers `-32602` for a malformed one; this server does not look at keys that are no
  category at all (3.3: unknown keys ignored).
* A register that the debugger never writes (`dc.aica.arm7` `ENABLED`): the mock answers `state.setRegister`
  on it with `-32004` and every other read-only register with `-32013`; this server answers `-32004` for the
  whole method until milestone M2.
* `symbol` of a callstack frame: `name` at the start of a symbol and `name+0x<offset>` inside it here (2.10);
  the mock writes `name+0x0` at the start. The hex digits of `symbol` and `location`: lower case here (2.10),
  upper case in the mock; a client accepts either.
* `sp` of a scanned callstack frame: the stack pointer of the processor here, the same in every frame (3.2); the
  mock gives the address the frame was found at.
* Memory: the mock's ARM7 has the RTC at `0x00810000`; here the ARM7 sees wave memory and the AICA register
  file only (2.7).
* The targets: the mock steps the ARM7 and the DSP and has register writes and the SGC capture ("M1 plus");
  this server is milestone M1.
* The tick that follows a request that changed the tables is sent from inside the call, on the emulator thread,
  and can reach the client in front of the response of that request; the mock sends the response first (2.9
  allows both).
* The grace period: when it expires the mock releases the stop without an entry in the event log; this server
  appends `dc.debugger.note` (2.6, 2.9).
* A describe of a client that names another protocol number is logged as `dc.debugger.note` here (2.6); the
  mock logs nothing.
