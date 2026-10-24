# The platform round — the cut

> **This copy.** `docs/nulldc3/platform-round.md` is the repository's copy of the working document
> `PLATFORM-ROUND.md` as it stands at the end of the platform round (package PLG, 2026-10-24): the cut with its three
> revisions and its addendum A64, the decisions PL-D1 to PL-D86, the questions PL-Q1 to PL-Q3 (open at the round's end;
> work proceeded on their recommendations), the changes the round makes to the design text (Appendix D, applied by
> PLG), the integrator's notes on the packages (Appendix I, I.1 to I.13) and the reviews of the cut (Appendix R). It
> was the contract where it and the design or the plan differed until the end of the round, when they were brought to
> it (package PLG); the design, the plan and the frozen headers cite it for header revision 8 (its 2.3), the version
> string (its 5.1) and the platform decisions. The text below names its companion documents by their working names;
> in `docs/nulldc3/` they are:
>
> | Named below | In `docs/nulldc3/` |
> | --- | --- |
> | `PLATFORM-ROUND.md`, "this file", "the cut" | `platform-round.md` (this file) |
> | `TRACK1-DESIGN.md`, "the design" | `devtools-integration-design.md` |
> | `PLAN.md` | `PLAN.md` |
> | `DESIGN-V3-OPEN.md` | `devtools-integration-open.md` |
> | `M2-ROUND.md` | `devtools-integration-m2-round.md` |
> | `M3-ROUND.md` | `devtools-integration-m3-round.md` |
>
> The paths of the form `agents/nulldc3/...` — the reader reports of `understand-pl/` and `understand-a64/`, the briefs
> of `pl-briefs/`, the packages' directories, the tools, the UI pin `wt/ui-pin` — point into the working area outside
> this repository (`docs/nulldc3/README.md`). The remote systems, "the Windows host", "the Mac" and "the arm64 host",
> are named only so: their access details were never written down (4.3).

Status: cut, 2026-10-19, from the four reader reports of `agents/nulldc3/understand-pl/` and the project owner's
decisions of the same day. Documents only: nothing was built or run for it; the facts it adds to the reports were read
in the two trees, and four were found by read-only listings on this machine (named where used: a Python 3.11 on the
Windows host, `qemu-aarch64-static`, `ld.lld` and the docker client on this machine). Packages start from here. The
design text is brought to the round by Appendix D, at the end of the round (package PLG). **Until then, where this file
and `TRACK1-DESIGN.md` or `PLAN.md` differ, this file is the contract** (PL-D1).

**Revision 1** (2026-10-19, the same day): the cut and its briefs after the adversarial review of round 1 (readers
"platforms", "release" and "integration"; 71 findings). Every finding was checked against the two trees and the owner's
decisions; Appendix R lists each one and what became of it. The revision adds PL-D68 to PL-D84 and PL-Q3; revises PL-D6,
PL-D13, PL-D14, PL-D17, PL-D24, PL-D25, PL-D29, PL-D34, PL-D35, PL-D42, PL-D43, PL-D45, PL-D59 to PL-D61, PL-D64, PL-D66
and PL-D67; rewrites 2.2, the package sections 2.4 to 2.14 (2.6 and 2.12 in part), sections 4 to 7 and 9; and adds rows
to Appendix D. The briefs under `pl-briefs/` are revised to match (each says "revision 1").

**Revision 2** (2026-10-19, the same day): the cut and its briefs after the adversarial review of round 2 (the same
three readers; 12 findings, R72 to R83 of Appendix R). Each was checked against the two trees and the owner's decisions;
all twelve hold and are taken (R82 settles with R76). The revision adds no decision and no question; it revises PL-D60,
PL-D61, PL-D68 to PL-D70, PL-D73 to PL-D75, PL-D77, PL-D78 and PL-D80, and with them 2.2, 2.4, 2.7, 2.8, 2.10 to 2.14,
4.1 to 4.3, 5.2, 6.1 to 6.6, section 7 (items 2, 8, 9), section 9, the risks of 8.4 and Appendix D row 3. Nothing of it
reaches the owner's decisions of 1.3. The briefs say "revision 2"; VCu, VTe, W64a, W64b, MACa, MACb, CIe and PLG changed
in substance, RB, IC and LA64 in their rules and remote lines.

**Revision 3** (2026-10-19, the same day): the cut and its briefs after the adversarial review of round 3 (readers
"platforms" and "release"; 2 findings, R84 and R85 of Appendix R). Both were checked against the two trees, this
machine's files and the owner's decisions; both hold and are taken, each extended where the check found more (R84: the
banner's second form, the host name in capitals and inside longer words, the scrubber's missing host-name variables;
R85: the remote systems pass their UI clone, not the submodule). The revision adds PL-D85; revises PL-D61, PL-D73 and
PL-D80, and with them 2.13, 2.14, section 3, 4.1 to 4.3, 6.3, 6.4, 6.6, section 7 (item 3) and section 9, and adds a
note to PL-Q1. Nothing of it reaches the owner's decisions of 1.3. The briefs W64a, W64b, MACa, MACb, LA64, CIe, PLG and
RB say "revision 3" (their scan lines and remote lines; CIe and PLG also the UI checkout); VCu, IC and VTe are unchanged
at revision 2.

**Addendum A64** (2026-10-23, after CIe's merge): the owner asked for the arm64 limits to be fixed before PLG ("before
PLG, can you fix the arm64 limitations?"). PLG's first run was stopped and archived (`agents/nulldc3/plg-run1/`, local
branch `skmp/nulldc3-plg-run1`); the package A64 (2.15, `pl-briefs/A64-BRIEF.md`, written from the reader reports and
reviews of `agents/nulldc3/understand-a64/`) runs between CIe and PLG, with the decision PL-D86. The owner's answers of
the same day: new arm64 state-hash references accepted; both of A64's steps (the 13 known limits, then native SH4
checks on arm64) before PLG; the Mac kept on AC power while A64 runs. PL-D78 is superseded by PL-D86 when A64 merges.

**State it starts from.** Emulator: repository `devcast` (GitHub `skmp/nulldc3`, public), integration branch
`skmp/nulldc3` = `34233717` = `main` `48bb09d9` plus the commit that pins the UI; the executable is `nulldc3`, portable
by default (the executable's directory), `--portable <dir>`, `--shared`, switches with two dashes; 307 ctest targets in
the tests project, green on Linux. The platform shell is the directory `reicast/` (`reicast/linux`, `reicast/windows`,
`reicast/apple`, `reicast/cmake`, `reicast/imgs`, `reicast/emscripten`). Only Linux x86_64 has been built and tested:
there has never been a real MSVC build, the macOS build has not been made since the debugger came in (December 2024 was
the last), and no arm64 build of any kind has been made in this project. Headless (`--config config:Headless=yes`)
exists on Linux only. UI: repository `dreamcast-devtools` (GitHub `skmp/dreamcast-devtools`, public), branch
`skmp/nulldc3` = `main` = `3cf8b15`; it is the emulator's submodule `devtools/`, pinned at `3cf8b15`. Neither repository
has a tag or a release. The UI has a CI on pull requests only; the emulator has none (its `appveyor.yml`,
`azure-pipelines.yml`, `wercker.yml` are reicast's and stale). M1, M2 and M3 are complete and merged on GitHub.

**Inputs.** The reader reports `understand-pl/rebrand.md`, `version-ci.md`, `windows.md`, `macos.md` (cited by name and
section; their file and line references are not repeated here); `M3-ROUND.md` (the model of this file: its section 9,
its Appendix I); `PLAN.md` (ground rules; targets; the backlog; N1); the design sections the reports name (D7, D10,
2.3.5, 2.3.7, 3.1, 5.4 to 5.8, 6.3 rows W64 and A64, Appendix B); the approved icon `agents/nulldc3/icon/nulldc-icon/`.

**The method** is that of M1 to M3 and is not restated per package: one package at a time; an emulator package works in
its own worktree (`agents/nulldc3/wt/<key lower case>`, branch `skmp/nulldc3-<key lower case>`), the UI package on the
UI's branch `skmp/nulldc3` itself; numbered deliverables; files it owns, disjoint in its turn; a gate; an independent
verifier who reruns the gate and attacks the package with mutations, beside two read-only reviewers; the integrator
merges and runs the full ctest. One heavy job at a time across this machine and the Windows host (the same computer):
`ninja -j8`, `ctest -j4`, at most four emulator processes; on the Mac `ninja -j6`. **Time neutrality holds through the
round: the state hashes (design 5.7) and the sound lines (design 5.6) on Linux do not move**; one baseline, P0, taken by
the first emulator package, holds every package (PL-D3). **Linux behaviour does not change except where 1.4 says so.**

**Names.** Decisions of this cut **PL-D1 ...** (8.1), questions for the owner **PL-Q1 to PL-Q3** (8.2), review findings
**R1 ... R85** (Appendix R; round 1 R1 to R71, round 2 R72 to R83, round 3 R84 and R85), items of the comment-only
header revision **H8-1 ... H8-4** (2.3; H8-5 by A64), packages **VCu, RB, IC, VTe, W64a, W64b, MACa, MACb, LA64, CIe, A64,
PLG** (section 2), the round's baseline **P0** (PL-D3). "The Windows host" is the Windows 11 computer this Linux VM runs on; "the Mac"
is the owner's Apple M2. Their access details are never written down (section 4).

Contents: 1 scope · 2 packages · 3 order and bases · 4 the remote systems · 5 versions · 6 CI · 7 the tags · 8
decisions, questions, risks · 9 what every brief carries · 10 the briefs · Appendix D the design text and the plan at
PLG · Appendix I the integrator's notes · Appendix R the reviews of rounds 1 to 3.

---

## 1. Scope

### 1.1 In

| What | Package | As cut |
| --- | --- | --- |
| The UI's version from the tag `v3.0.0-pre`; byte-exact checkouts (`.gitattributes`); the UI's CI on pull requests and tags, with a GitHub Release of its files on a tag | VCu | one UI package, first (PL-D2); the format of section 5; the release files `dist-native` and `dist-wasm` zipped (PL-D62) |
| `reicast/` becomes `nulldc3/`; every live name that says reicast gets a nulldc3 name; dead reicast files go; "reicast" stays only as history | RB | a pure-move commit first (PL-D7); the names of PL-D9; the deletions of PL-D14; a test that holds it (`rebrand.names`, PL-D17); H8-1 to H8-3 |
| The icons: the approved art in the tree, a generator of `.ico`, `.icns` and the X11 pixel data with a check target; the X11 window's icon and class | IC | adoption where each system is built: Windows by W64a, macOS by MACa, the AppImage by CIe (PL-D22) |
| The emulator's version from the tag, made at build time; every reader of it; `--version` | VTe | section 5; H8-4 |
| Windows: the MSVC build (static CRT, the GCC-only codecs, the `.rc` with icon and version), Windows headless with a no-dialog policy, the Windows ABI fixes, the portable C++ suites on the Windows host | W64a | the first emulator run on the Windows host is a proof that no window can appear (4.3) |
| Windows: the Python harnesses, the selftests, the sound tests and the contract gates M1 to M3 on the Windows host; the x64 JIT's native debugger checks on Windows | W64b | design 6.3's W64, its window items to the owner's by-hand list (PL-D40) |
| macOS arm64: the bundle made by CMake (template, icon, ad-hoc signature), the Xcode project gone, macOS headless, the `printf` macro gone, the portable C++ suites on the Mac | MACa | the first emulator run on the Mac is a proof that no window can appear (4.3) |
| macOS arm64: the harnesses, the selftests, the sound tests and the contract gates M1 to M3 on the Mac | MACb | the first real arm64 emulator meets the debugger's arm64 rules (`registersStale`, the fallback engine) |
| Linux arm64: the arm64 code builds and runs on Linux (the arm64 AppImage's binary) | LA64 | cross-built and run under `qemu-aarch64-static` here; natively first in CI (PL-D52) |
| The emulator's CI on pull requests and tags (Linux x86_64 and arm64, Windows x86_64, macOS arm64), the release files (two AppImages, a Windows zip, a macOS zip), the GitHub Release; the AppImage's portable directory | CIe | thin workflows over committed scripts that the package runs on the three systems first (PL-D54) |
| The round's gate on all three systems, the documents, the two PR texts; the PRs and the tags | PLG (the PRs and the tags by the integrator outside the package workflow, PL-D84) | UI PR first; the emulator PR after the owner merged it; tags after both merges, with the owner's go (section 7) |

### 1.2 Not in this round

| What | Goes to | Why |
| --- | --- | --- |
| the arm64 SH4 FPU fast-tier fixes **and the arm64 ARM7 recompiler** (arm64 native debugger checks, design 2.3.5 in `jit/backend/arm64`, joined the round as package A64 on 2026-10-23: 2.15) | design 6.3 | the ARM7 recompiler is unsafe on Apple silicon as written (`macos.md` 5) and is not built in this round (PL-D46) |
| A `.deb`, a per-user default directory for the AppImage, any Linux package but the AppImage | — | the owner: AppImage only, portable |
| Windows on arm64, macOS x86_64 releases | — | the owner's release targets; macOS x86_64 stays source-consistent and unbuilt (PL-D47) |
| Apple Developer ID, notarization, the hardened runtime, Windows code signing | — | the owner: unsigned (ad-hoc) |
| The window items of W64 (a window paused by the debugger and then closed, the window sessions of design 6.1.2, a GL renderer smoke) | the owner, by hand, from PLG's list | no agent may open a window on the owner's desktops (PL-D40) |
| The Linux-only system-test harness (`tests/system`: `LD_PRELOAD` shims, namespaces) on Windows and macOS | — | the owner's decision; its targets are skipped there with that reason |
| 2ndmix, hello and the RTL checkouts (caique, wren7, shrike4) in CI | PL-Q1 | not in a public repository, or no licence file (PLAN "Needs the user"); CI runs what public data allows (PL-D58) |
| A macOS-specific icon with Apple's margin | PL-Q2 | new art the owner has not approved |
| Rewriting history to drop reicast's old AWS key id from `appveyor.yml` | — | public history; the file goes from the tip (R12 of `rebrand.md`; 8.4) |
| The sent-signal rule on Darwin, `si_code` (`macos.md` 6.2) | backlog | a `kill -SEGV` of a harness is not a path any test takes |
| Speed | — | measured nowhere in this round; nothing is changed for it |

### 1.3 The owner's decisions (2026-10-19; the round's fixed points)

1. `reicast/` becomes `nulldc3/`; files and identifiers named after reicast get nulldc3 names; "reicast" stays only as
   history (licences, credits, documents that record history, git history).
2. Branding: company and copyright holder **"nullDC Team"**; macOS bundle identifier **`org.emudev.nulldc3`**.
3. Releases: Linux = **AppImage only** (x86_64 and arm64), portable: the emulator treats the AppImage's own directory as
   its portable directory; no `.deb`, no per-user default. Windows = a `.zip` with `nulldc3.exe` (x86_64). macOS = an
   unsigned (ad-hoc signed) `nulldc3.app` in a zip (arm64 only).
4. macOS is built with CMake only: the Xcode project and workspace go (git history keeps them); `cmake -G Xcode` is how
   someone gets an IDE.
5. The tag is **`v3.0.0-pre`**, on both repositories; both derive their version from it (`git describe`); the tags are
   pushed by the integrator after the round's PRs are merged and only with the owner's go.
6. CI: GitHub Actions on pull requests and on tags, both repositories; PR builds build and test, tag builds also make
   the release files and attach them to a GitHub Release. Nothing in CI files or CI logs may name a host, a user, a home
   path or a LAN address (the owner's words; the runner's own paths, user and machine name, which every GitHub-hosted
   log carries, are PL-Q3, worked on as PL-D61 recommends until the owner answers).
7. Windows and macOS get headless modes: no window, no Dock icon, no dialog of any kind.
8. Test depth on Windows and macOS: the portable part of the test suite, the debugger self-tests and the contract gates
   M1 to M3 against the emulator on that system; what is skipped is listed with its reason; the Linux-only harness is
   not ported.
9. Icons: the approved emulator icon (`agents/nulldc3/icon/nulldc-icon/`) in the Windows `.ico` (executable and window
   class), the Linux `.desktop` and PNGs and the X11 window's own icon, the macOS `.icns`; `.ico`, `.icns` and the X11
   data derived from the committed PNGs by a generator a ctest target runs in check mode; old reicast images go;
   `WorkDir/` stays as it is.
10. PRs: none for intermediate work; at the end the devtools PR first, the nulldc3 PR only after the owner merged it
    (the emulator then pins the devtools `main` commit).

### 1.4 What may change on Linux, and what may not

Allowed, and only these: **names** (the Lua module `nulldc3`, the HTTP User-Agent `nulldc3`, the man page and tool
names, the imgui button "Report a game bug" gone, PL-D12); **version strings** (window titles, `debugger.describe`'s
`emulator.version`, `dc.BUILD`, the paused title, the page served without a bundle, the imgui About and welcome screen,
`--version`, its line in `--help`); **the icon** (the X11 window's `WM_CLASS` and `_NET_WM_ICON`); **the AppImage
portable directory** (`$APPIMAGE`, PL-D51). Anything else that moves on Linux — a state hash, a sound line, a line of
output a test compares, a compile command of the Linux build outside the files a package edits — is a defect of the
package until the integrator decides otherwise.

---

## 2. Packages

### 2.0 The packages

| # | Key | Repository | Content in one line | Brief |
| --- | --- | --- | --- | --- |
| 1 | **VCu** | `dreamcast-devtools` | the UI's version from the tag, `.gitattributes`, the UI's CI on PRs and tags with its Release | `pl-briefs/VCu-BRIEF.md` |
| 2 | **RB** | `devcast` | `reicast/` → `nulldc3/`, the names, the dead files, P0, H8-1 to H8-3, `rebrand.names` | `pl-briefs/RB-BRIEF.md` |
| 3 | **IC** | `devcast` | the icon sources, the generator and its check, the X11 icon and class | `pl-briefs/IC-BRIEF.md` |
| 4 | **VTe** | `devcast` | the emulator's version from the tag at build time, its readers, `--version`, H8-4 | `pl-briefs/VTe-BRIEF.md` |
| 5 | **W64a** | `devcast` | the MSVC build, the `.rc`, Windows headless and no dialogs, the ABI fixes, the C++ suites on the Windows host | `pl-briefs/W64a-BRIEF.md` |
| 6 | **W64b** | `devcast` | the harnesses, selftests, sound tests and contract gates on the Windows host; native debugger checks on Windows | `pl-briefs/W64b-BRIEF.md` |
| 7 | **MACa** | `devcast` | the CMake bundle, Xcode gone, macOS headless, `printf` macro gone, the C++ suites on the Mac | `pl-briefs/MACa-BRIEF.md` |
| 8 | **MACb** | `devcast` | the harnesses, selftests, sound tests and contract gates on the Mac | `pl-briefs/MACb-BRIEF.md` |
| 9 | **LA64** | `devcast` | the arm64 build on Linux, cross-built and run under qemu here | `pl-briefs/LA64-BRIEF.md` |
| 10 | **CIe** | `devcast` | the emulator's CI and release files, the AppImage and its portable directory | `pl-briefs/CIe-BRIEF.md` |
| 11 | **A64** | `devcast` | the arm64 recompiler's native debugger contract: the 13 known limits (A64) and PL-D78's rule gone, native SH4 checks on arm64, the sh4bp suite on arm64 (added 2026-10-23) | `pl-briefs/A64-BRIEF.md` |
| 12 | **PLG** | both (integrator) | the round's gate on three systems, the documents, the PRs, the tags | `pl-briefs/PLG-BRIEF.md` |

The keys are unique against every earlier round (no directory `agents/nulldc3/<key lower case>` exists; W64 is the
design's name of the planned Windows package, split here in two).

### 2.1 Why this shape

Against the suggested shape (rebrand, icons, version e/u, Windows, macOS, CI, gate):

- **The UI's share is one package, VCu, and it comes first.** The UI's whole part of the round is a version function,
  one `.gitattributes` and one workflow file, all on Linux, all in one repository the package workflow serves one at a
  time. Splitting the UI's version from the UI's CI would cost a second pin move and verification for a workflow whose
  tag job checks that very version. The emulator then works against a pinned UI that already has its final version rule.
  The release conventions both repositories follow are in this cut (section 6), not in either package.
- **The rebrand is first among the emulator packages, and its move is a commit of its own.** Every later package edits
  paths under `nulldc3/`; the history reader (`rebrand.md` 2.3) shows that only a commit without content changes keeps
  the small files' rename pairing. The path lines of files that do not move (`CMakeLists.txt`, two test CMake files) go
  into the same commit so that every commit builds (PL-D7). The dead files go in RB too: one owner of deletions.
- **Adoption of the icon is split by system** (PL-D22): IC owns the art, the generator and the Linux window; the Windows
  resource is first compiled by W64a and the bundle first made by MACa, so they adopt the files there. A package cannot
  gate what it cannot build.
- **The version is one emulator package and one UI part** (VCu); the readers of the version are all on the emulator
  side, and Windows' `VERSIONINFO` and the plist take their values from VTe's header and file in W64a and MACa.
- **Windows and macOS are two packages each**: first the build and a headless mode proved not to open a window (and the
  C++ suites, which need no harness), then the harness-driven gates. The first half changes the product on that system;
  the second changes tests (and on Windows the JIT's debugger stub), and its gate is long. A single package would make
  one verifier attack both the no-dialog policy and the contract gates in one round.
- **Linux arm64 is a package of its own, LA64, behind macOS.** The arm64 SH4 JIT is shared by macOS and Linux and is
  macOS-only as written (`macos.md` 4, 18); LA64 makes it build on Linux without changing what the Mac runs, which it
  proves on the Mac with MACa's tools. It is the one package whose system is not reachable natively: it cross-builds and
  runs under `qemu-aarch64-static` (installed on this machine), and CI is its first native run (PL-D52).
- **The emulator's CI and the AppImage are one package, CIe, last before the gate.** CI needs every platform's build and
  headless switches; the AppImage's portable directory is emulator code whose only consumer is the AppImage, and its
  test is a system test of the same package. The workflows call committed scripts, so CIe runs every job's script on the
  three systems before any PR exists (PL-D54).
- **PLG** is the integrator's, as M3G was, with the tag plan added; the package workflow runs its gate and its
  documents, and the integrator makes the pushes, PRs and tags outside it, step by step with the owner (PL-D84).

### 2.2 The conflict surface

Files that more than one package edits, and who edits what in which turn. Every package is based on the merge of the one
before it; nobody edits a file in another's turn. A package named in a row owns those lines in its turn, and its brief
repeats them under "Owns".

| File | Turns |
| --- | --- |
| `CMakeLists.txt` (top) | RB (the shell path names, the path lines in the move commit, the entitlements attribute out, the comment of `TNAME`) → IC (`linux-dist/x11_icon.cpp` in the Linux sources) → VTe (the version target, the old module out, `libswirl/version.cpp` and `libswirl/version_title.cpp` in the sources with the generated directory on `version.cpp`'s include path alone) → W64a (the `.rc` in the Windows sources with the generated directory on its include path, MSVC link options, policies, the codec condition) → MACa (`MACOSX_BUNDLE`, languages, plist, `.icns`, the licences, the bundle's stamp target, deployment target, the post-build script out) → LA64 (Linux arm64 conditions, if any) → CIe (`CMAKE_VERBOSE_MAKEFILE` overridable, the prefix-map option) |
| `nulldc3/cmake/config.cmake` | RB (moved; reicast's TODO line) → W64a (the MSVC flags, the runtime library) → MACa (`FEAT_AREC` off on arm64, `-ffp-contract=off` on arm64, the Darwin page and stack rules if CMake carries them) → LA64 (Linux arm64) → CIe (the prefix-map option) |
| `nulldc3/cmake/GetGitRevisionDescription.cmake{,.in}` | RB (moved) → VTe (deleted) |
| `libswirl/version.h.in` | RB (`REICAST_VERSION` → `NULLDC_VERSION`) → VTe (deleted; `nulldc3_version.h` generated) |
| `libswirl/version.cpp`, `libswirl/version_title.cpp`, `libswirl/nulldc_version.h` (new; PL-D75) | VTe |
| `nulldc3/windows/nulldc3.rc`, `resource.h` | RB (renamed; `CompanyName`, `LegalCopyright`, the icon file name, the macro name) → W64a (compiled; icon id; version macros) |
| `nulldc3/windows/nulldc3.ico`, `nulldc3/apple/nulldc3.icns`, `libswirl/linux-dist/x11_icon.h` | IC (generated) → MACa (`.icns` and the generator's `.icns` part, only if `iconutil` refuses it) |
| `nulldc3/icon/**` | IC → W64a (`make_icons.py`'s file input and output: runtime portability fixes only, PL-D68) → MACa (as above; and the same runtime fixes) |
| `nulldc3/apple/Info.plist` → `Info.plist.in` | RB (moved; copyright, identifier text) → MACa (`git mv` to `.in`, template keys) |
| `nulldc3/apple/*.m`, `*.mm`, `*.h` | RB (moved, flattened; names) → MACa (headless, the alert rule, `printf`, translocation, restoration, the About panel) |
| `nulldc3/apple/emulator-osx/emulator-osx/build_app_bundle.sh` → `nulldc3/apple/build_app_bundle.sh` | RB (moved; `com.reicast` out, the old icon line out) → MACa (deleted) |
| `nulldc3/linux/nulldc3.desktop` | RB (renamed; `Icon=nulldc3`) → IC (`StartupWMClass=nulldc3`) → CIe (`Exec=nulldc3 %f`, `[el]`) |
| `nulldc3/linux/man/nulldc3.1` | RB (names) → VTe (`--version`) → CIe (the AppImage's directory) |
| `nulldc3/README.md` | RB (rewritten) → IC (the icon section) → VTe (the generated version files) → MACa (the `apple/` part) |
| `libswirl/linux-dist/main.cpp` | RB (the entry points) → CIe (`find_host_dirs`: the AppImage rule; its comment) |
| `libswirl/linux-dist/x11.cpp` | IC |
| `libswirl/windows/**`, `libswirl/oslib/windows/**`, `libswirl/oslib/audiobackend_directsound.cpp` | RB (the entry points) → W64a |
| `libswirl/jit/backend/x64/rec_x64.cpp` | W64a (`Mainloop` / `RunBlock` save XMM6-15 on Windows) → W64b (the check stub's shadow space, the Windows flush budget, `DebugChecks` on `_WIN32`) |
| `libswirl/jit/backend/arm64/**`, `libswirl/linux/context.cpp` | MACa (only what the Mac's build needs) → MACb (Darwin and arm64 defects its gates show, each named) → LA64 |
| `libswirl/linux/common.cpp`, `posix_vmem.cpp`, `libswirl/stdclass.h`, `libswirl/oslib/posix/threading.cpp` | RB (`posix_vmem.cpp`: the comment that cites reicast's code) → MACa (Darwin: fatal policy, `shm_open`, page rule, stacks) → MACb (Darwin defects its gates show) → LA64 (Linux arm64 page refusal, if needed) |
| `libswirl/types.h` | VTe (includes `nulldc_version.h`; `VER_SHORTNAME` / `VER_FULLNAME` expand to `nulldc_title()`) → MACa (`darw_printf` out) |
| `libswirl/cfg/cl.cpp` | VTe (`--version`, help) → MACa (the translocated case of the "cannot be written" message) → CIe (help: the AppImage's directory) |
| `libswirl/libswirl.cpp`, `libswirl.h` | RB (entry point names) → VTe (only if the version line needs it) |
| `libswirl/gui/gui.cpp`, `gui_welcome.cpp`, `gui_settings_about.cpp` | RB (`ImguiGUI`, the button, the macro name) → VTe (the version readers) |
| `libswirl/debugger/debugger.h` (frozen) | RB (H8-1 to H8-3) → VTe (H8-4) |
| `libswirl/debugger/dbg_core.cpp`, `dbg_regs.cpp`, `dbg_emu.cpp`, `dbg_webui.cpp`, `dbg_selftest_core.cpp`, `dbg_selftest_regs.cpp`, `tests/debugger/net/test_webui.cpp` | VTe |
| `libswirl/debugger/dbg_selftest_hostcycles.cpp` | W64b |
| `libswirl/hw/sh4/sh4_core_regs.cpp`, `libswirl/hw/sh4/dyna/decoder.cpp`, `libswirl/oslib/oslib.h` | W64a |
| the 15 files of the save-state macros | RB (whole-word rename only) |
| any file of `libswirl/**` (deps included) or `tests/**` that a compiler refuses | W64a (MSVC) → MACa (AppleClang on Darwin) → LA64 (the Linux arm64 cross build): compile-only portability fixes, each in its own turn, by PL-D68 |
| `tests/CMakeLists.txt` | RB (`rebrand/`) → IC (`icons/`) → VTe (`version/`, and the version step before the debugger suites) → W64a (`winrun/`, `windows/`) → MACa (`macos/`) → CIe (`ci/`) |
| `tests/common/libswirl.cmake`, `tests/cfg/CMakeLists.txt` | RB (`reicast/cmake`) → VTe (`libswirl/version.cpp`, `libswirl/version_title.cpp` and the generated directory for the library that holds `cfg/cl.cpp`) → W64a (MSVC) → MACa (Darwin) |
| `tests/debugger/net/CMakeLists.txt` | VTe (the standalone project's version step under a target name of its own; `version.cpp` in the `dbg_webui` object library; PL-D75) → W64a (MSVC, if it needs any) |
| `tests/debugger/{regs,regs/aica,symbols}/CMakeLists.txt` | RB (`reicast/cmake`, `DEVCAST_DIR`) → W64a (MSVC) → MACa (Darwin) |
| `tests/debugger/disasm/CMakeLists.txt` | RB (`DEVCAST_DIR`) → W64a (MSVC; the objdump oracles a platform skip on hosts that are not Linux) → W64b (`tsx` on Windows) → MACa (Darwin) |
| `tests/common/*.cpp` (one translation unit) | W64a (the `/EHsc` `#error`) |
| `tests/common/emurun.py` (new) | W64b → MACb (the Darwin branch, through `macrun.py`) |
| `tests/system/CMakeLists.txt`, `tests/debugger/{selftest,contract,guest}/CMakeLists.txt`, `tests/aica_dsp/CMakeLists.txt` (the emulator-driven part) | W64a (the Windows skip reasons: "W64b", for `system` "the Linux-only system-test harness") → W64b (Windows opened, `NULLDC_EMULATOR`, the suffix) → MACa (the Darwin skip reason "MACb") → MACb (Darwin opened) → CIe (`tests/system`: the `dirs.appimage*` cases) |
| `tests/system/system_test.py`, `tests/system/README.md` | RB (`REICAST_CONFIGS` / `REICAST_DATAS` → `LEGACY_CONFIGS` / `LEGACY_DATAS`) → VTe (`system.help`) → CIe (`dirs.appimage*`) |
| `tests/debugger/{selftest,contract,guest}/**` (the harnesses), `tests/debugger/guest/dsptest/soundlines.py` | W64b (the shared run helper, Windows opened, no `bash` on Windows) → MACb (Darwin opened; in `tests/debugger/contract/contract.py`, PL-D78's rule in `run_runner` and its test) |
| `tests/debugger/selftest/window_checklist.sh` | VTe (the expected paused title only) |
| `tests/debugger/sh4bp/**` | W64a (`CMakeLists.txt` only: skipped on Windows with the reason "W64b") → W64b (the port of `selftest.sh`, the Windows gates) |
| `tests/winrun/**`, `tests/windows/**` | W64a |
| `tests/macos/**` | MACa |
| `tests/version/**` | VTe → W64a (the Windows launcher of `version.binary`; runtime portability fixes, PL-D68) → MACa (the Darwin launcher, if one is needed; runtime portability fixes) |
| `tests/rebrand/**` but `allowed.txt`, `tests/icons/**` | RB, IC → W64a → MACa (runtime portability fixes only, PL-D68) |
| `tests/rebrand/allowed.txt` | RB → every later package, PLG included: the rows of the history it adds, moves, rewraps or deletes (PL-D79) |
| `tests/README.md`, the READMEs of `tests/*` | every package, its rows and counts, in its turn |
| `README.md` (emulator) | RB (paths) → VTe (`--version`) → W64a (Windows quick start) → MACa (macOS quick start) → CIe (downloads, the AppImage) → PLG |
| `docs/nulldc3/automated-runs.md` | VTe → W64a (Windows headless) → W64b → MACa (macOS headless) → MACb → CIe (the AppImage's directory) → PLG |
| `reidbg/gtk-gui/MainWindow.cs`, `reidbg/gtk-gui/gui.stetic` | RB (the label "Reicast Host" only) |
| `.gitignore` | RB (dead patterns) → CIe (AppDir and release output) |
| `.gitattributes` (new) | RB |
| `.github/workflows/**`, `nulldc3/ci/**` | CIe |
| `nulldc3/ci/hygiene-allow.txt`, `nulldc3/ci/hygiene-allow-ui.txt` | CIe → PLG (the rows its documents need, PL-D79) |
| `docs/nulldc3/*` (the design and plan copies, the round's record) | PLG |
| the UI repository | VCu (a UI defect found after the UI PR is merged: a second UI PR, section 7) |
| the submodule `devtools/` | the integrator alone, in a commit of its own (section 3, section 7) |

**Portability fixes** (PL-D68). *Compile-only*: W64a (MSVC), MACa (AppleClang on Darwin) and LA64 (the Linux arm64 cross
build) may, in their own turn, change any file of `libswirl/**` (deps included) and `tests/**` that their compiler
refuses. Each fix changes no behaviour, leaves the Linux default build's `ninja -t commands` equal to the base's except
for the files edited, keeps P0 identical, and is listed in the report as file, error and change. A frozen header (design
Appendix B) is excluded: a declaration a compiler refuses there goes to the integrator as header revision 9. *Runtime,
in earlier packages' test scripts*: W64a (on the Windows host) and MACa (on the Mac) may also fix, on their system, the
test scripts of earlier packages that their gate is the first to run there — `tests/rebrand/**`, `tests/icons/**` and
`make_icons.py`'s file input and output, `tests/version/**` — when they fail there for a reason that is the system's (a
`file://` URL with a drive letter, a text-mode write that adds `\r\n`, a Python construct newer than 3.9). Each such fix
leaves what the test checks unchanged and the Linux results equal, and is listed in the report as file, failure and
change. A file that a later package owns in this table keeps its later owner; the fix is the only change made to it in
the earlier turn.

### 2.3 Header revision 8 (comment-only)

`libswirl/debugger/debugger.h` is frozen (design Appendix B). The round makes four of its comments false and changes no
declaration. The texts are fixed here (PL-D4); RB applies H8-1 to H8-3 in the commit that renames the entry points, VTe
applies H8-4 in the commit that makes the paused title carry the version. No other frozen text changes in this round. A
frozen comment carries no character outside ASCII. PLG adds row 8 to Appendix B of the design.

- **H8-1**, the comment of `Init()`: `// (U), once, at the end of reicast_init(); lifetime NoMachine.` becomes `// (U),
  once, at the end of nulldc_init(); lifetime NoMachine.`
- **H8-2**, the comment of `Term()`, first line: `at the start of reicast_term(),` becomes `at the start of
  nulldc_term(),`.
- **H8-3**, the same comment, last line: `the emulator's shutdown makes right behind this call (reicast_term).` becomes
  `the emulator's shutdown makes right behind this call (nulldc_term).`
- **H8-4**, the comment of the park loop, the bullet "Parks while a debugger stop is in force": `the window title
  becomes "nullDC 3.0.0 [paused by debugger]"` becomes `the window title becomes "nullDC <version> [paused by
  debugger]", <version> being the build's version string (NULLDC_VERSION)`; the rest of the sentence as it is, rewrapped
  at the file's width.

### 2.4 VCu — the UI's version, line endings and CI

**Delivers.** (1) `scripts/version.ts`: a pure function from the git facts (a checkout or not, the describe output, the
full hash, dirty) and the base (`package.json` `version`) to the version string of section 5, and a command that prints
it. Every git call is `execFileSync('git', ['-C', root, ...])` with no shell and `GIT_OPTIONAL_LOCKS=0` (5.1); describe
is `describe --tags --long --match v[0-9]*`; vitest cases for every row of 5.1. (2) `vite.config.ts` uses it; `gitRev`
marks `-dirty` by the same rule (tracked changes only). (3) `.gitattributes` `* -text` (PL-D16). (4)
`.github/workflows/ci.yml` (6.1, 6.2): `pull_request`, `push` of tags `v*`, `workflow_dispatch` with the input `tag`;
the jobs `check` and `e2e` as today with `fetch-depth: 0`, `package` on every run (`release-files.ts pack`),
`release-check` on pull requests and dispatches (`if: github.event_name != 'push'`, read-only, `release-files.ts
check`), `release` on tag pushes only (`if: github.event_name == 'push' && github.ref_type == 'tag'`, `needs` exactly
`check`, `e2e`, `package`, the only job with `contents: write`, `release-files.ts publish`); actions pinned by commit
(PL-D60). (5) `scripts/release-files.ts` with unit tests, three explicit invocations (6.1, PL-D77): `pack` (the two
zips, `SHA256SUMS`, the notes; never calls `gh`, whatever the event), `check` (creates nothing) and `publish` (takes the
files from `package`'s artifact; refuses unless `GITHUB_EVENT_NAME` is `push` and `GITHUB_REF_TYPE` is `tag`). (6)
`README.md`: the version rule, CI, releases.
**Owns.** `scripts/version.ts`, `scripts/release-files.ts` and their tests, the release notes template; `vite.config.ts`
(the two functions); `package.json` (a `print-version` script; the version stays `3.0.0-pre`); `.gitattributes`;
`.github/workflows/ci.yml`; `README.md` (those sections). **Must not touch.** `src/**` beyond what the version's type
needs, `server/**`, `scripts/contract/**`, the e2e specs (they must pass unchanged), the emulator repository.
**Depends on.** Nothing of the round (UI `3cf8b15`).
**Gate.** Linux only: `npm run check`; the unit suite three times in a fresh clone; the e2e suite; `actionlint` (a
pinned release binary in `agents/nulldc3/tools/`, its checksum checked) clean on the workflow, and a read of the
workflow against 6.1's rules (each job's invocation, `release`'s `needs` and `if:`, `release-check`'s `if:`);
`release-files.ts` in a scratch clone against a stub `gh`: `publish` with a scratch tag and `GITHUB_EVENT_NAME=push`
(never a tag in the shared repository: a draft with the three files, the asset list checked, then published; a re-run
with the draft present; a published Release refused; a scratch tag `v3.0.1` with base `3.0.0-pre` refused; the same tag
with `GITHUB_EVENT_NAME=workflow_dispatch` refused), `pack` with `GITHUB_EVENT_NAME=push` and `GITHUB_REF_TYPE=tag` (no
`gh` call recorded) and `check` (a pull request: names only; a dispatch: the base against the `tag` input), nothing
created outside `publish`; the version in a fresh clone, a shallow clone made through a `file://` URL, a clone with a
scratch tag, a dirty tree, a copy without git, a copy inside another repository, and as the emulator's submodule (`-c
protocol.file.allow=always`), with `<h>` asserted to be 10 hexadecimal digits and a prefix of `git rev-parse HEAD` in
each. What only GitHub can check: the runner images, the `gh release` calls, the token's permissions — first on the
round's UI PR (`check`, `e2e`, `package`, `release-check`), then on the dispatch check and on the tag (section 7).
**Verifier.** Mutations: `<h>` taken from describe's abbreviation (the length assertion is red in a scratch repository
of a few commits, where git abbreviates to 7), the `v` kept, untracked files counted dirty, `--match` dropped (a scratch
tag `foo` is taken), the fallback without the hash, git run through a shell with the quoted pattern (a unit test of the
argument vector), the tag check removed (the `v3.0.1` case passes), `release` without its `if:` or with the event
condition dropped, `release` needing `release-check`, `package` calling `publish` (the read of the workflow), `pack`
calling `gh` (the stub test), `publish` without the event check (the dispatch case of the stub test), `contents: write`
at the top level, an action by tag instead of commit; each must be caught by a named test or by the read of the workflow
against the rules.

### 2.5 RB — the rebrand

**Delivers.** (1) **P0** on RB's base (PL-D3). (2) **The move commit**: `git mv reicast nulldc3`, the renames of
`rebrand.md` 1.3 (`reicast.rc` → `nulldc3.rc`, `reicast.desktop` → `nulldc3.desktop`, `reicast.1` → `nulldc3.1`,
`reicast-joyconfig.1` → `nulldc3-joyconfig.1`, `reicast-joyconfig.py` → `nulldc3-joyconfig.py`,
`apple/emulator-osx/emulator-osx/*` → `apple/*`), with no content change of a moved file; in the same commit only the
path lines of files that do not move (`CMakeLists.txt`: `reicast_shell_path`'s value, `d_osx`;
`tests/common/libswirl.cmake`; `tests/debugger/regs/aica/CMakeLists.txt`), so that the commit builds. (3) **The deletion
commit** (PL-D14; `reidbg/` stays). (4) **The renames** (PL-D9 to PL-D13, PL-D15), as whole-word replacements, in
commits of their own: the save-state macros, the entry points, the CMake variables, the version macro, the strings, the
shell files, `nulldc3/README.md` rewritten, `REICAST_CONFIGS` / `REICAST_DATAS` → `LEGACY_CONFIGS` / `LEGACY_DATAS` in
`tests/system/system_test.py` (their path strings and the `dirs.*` docstrings stay), reidbg's label "Reicast Host" →
"nullDC host" (`reidbg/gtk-gui/MainWindow.cs`, `gui.stetic`; the CRLF files `reidbg.csproj` and `reidbg.sln` untouched),
the comment of `libswirl/linux/posix_vmem.cpp` that cites reicast's code. (5) H8-1 to H8-3. (6) `.gitattributes` `*
-text`. (7) `tests/rebrand/`: `rebrand.names`, `git grep -I -i -n reicast` against the committed allow list
`tests/rebrand/allowed.txt` (PL-D17, PL-D79): `docs/nulldc3/**` and the licence files allowed as whole paths (no rows,
no "gone" check); every other piece of history a row of path, pattern and reason; red on an occurrence that no entry
allows and on a row whose occurrence is gone. (8) The READMEs and `tests/README.md` paths.
**Owns.** Every path under `reicast/` (as `nulldc3/`), the files of the deletions, the lines that name reicast in
`CMakeLists.txt`, `libswirl/**`, `tests/**` (the 13 uses of `DEVCAST_DIR` among them; the two names of
`tests/system/system_test.py`), `reidbg/gtk-gui/MainWindow.cs` and `gui.stetic` (the label), `.gitignore`,
`.gitattributes`, `README.md`, `tests/README.md`, `libswirl/debugger/debugger.h` (H8-1 to H8-3 only),
`tests/rebrand/**`, `tests/CMakeLists.txt` (one line). **Must not touch.** What history keeps (PL-D13: the licences,
credits, `docs/nulldc3/**`, the macOS file headers, the vixl markers, the legacy directories' path strings and the
`dirs.*` docstrings of `tests/system/system_test.py`), `WorkDir/`, anything a later package changes on purpose
(versions, icons, headless), the UI repository.
**Depends on.** VCu verified and the pin moved (section 3).
**Gate.** Linux: the five builds; ctest all green twice, none skipped, 308 stated (307 + `rebrand.names`); P0 identical;
`git diff -M --name-status` of the move commit shows every move as `R100`; every source file named by the Darwin and
Windows branches of `CMakeLists.txt` exists (a script); `git log --follow nulldc3/windows/nulldc3.rc` reaches
`reicast/windows/reicast.rc`; a configure with `--warn-uninitialized` shows no warning that names `nulldc_root_path` or
`reicast_root_path`; a configure with a scratch bundle at `<worktree>/devtools/dist-native` (written as
`tests/debugger/net/embed_check` writes one) says `Dreamcast DevTools: embedding <worktree>/devtools/dist-native`, and
the scratch bundle is removed after it. The Windows host and the Mac: nothing runs there; a CMake configure on the Mac
(no build, no run) if the owner's tools are there (4.4), else MACa's first configure is the check.
**Verifier.** Reproduce P0 on the base; mutations: one missed path (`d_osx`: the script; the aica test project's module
path: its configure fails; the debugger's `reicast_root_path` in `debugger.cmake`: the `--warn-uninitialized` configure
warns and the scratch-bundle configure says "not embedded"); a `REICAST_S` prefix replace that breaks `REICAST_SA`; a
live `reicast` string added (the test is red); an occurrence of an allow-listed row removed (`rebrand.names` names the
row); the move commit with one content edit (no longer `R100`); `reidbg/` deleted (read against PL-D14).

### 2.6 IC — the icons

**Delivers.** (1) `nulldc3/icon/`: the approved files byte for byte (`nulldc.svg`, `nulldc-small.svg`, `png/` 16 to
1024, `preview.png`, `README.md`, extended by the generator's part). (2) `nulldc3/icon/make_icons.py` (Python 3.9,
standard library only, PL-D74): validates the eleven PNGs (signature, IHDR size = name, 8-bit RGBA, not interlaced),
writes `nulldc3/windows/nulldc3.ico` (PL-D19), `nulldc3/apple/nulldc3.icns` (PL-D20), `libswirl/linux-dist/x11_icon.h`
(PL-D21: `uint32_t` ARGB, sizes 16 to 128, a size table, a `static_assert` of the length, a hash of the payload), with a
fixed text layout, no time stamp, no host path; `--check` rebuilds in memory, compares bytes, re-parses the committed
`.ico` and `.icns` structurally. (3) `tests/icons/`: `icons.generated` (the check; Python required, skipped without, a
failure under `NULLDC_TESTS_REQUIRE_DATA`) and `icons.x11` (a C++ test of `x11_icon_cardinals()`: the widened `unsigned
long` layout, the hash recomputed, the corners' alpha 0 for 24 px and larger and at most 12 for 16 px — the approved
small art's antialiasing —, an opaque centre). (4) The X11 window: `XSetClassHint` (`nulldc3`, `nulldc3`) and
`_NET_WM_ICON` before the window is mapped, from `libswirl/linux-dist/x11_icon.cpp` (no X11 header in it, so the test
needs no display). (5) `StartupWMClass=nulldc3` in the desktop file.
**Owns.** `nulldc3/icon/**`, the three generated files, `libswirl/linux-dist/x11.cpp`, `x11_icon.cpp`, `x11_icon.h`, the
Linux source line in `CMakeLists.txt`, `tests/icons/**`, `tests/CMakeLists.txt` (one line), the desktop file's
`StartupWMClass`, the icon rows of the READMEs. **Must not touch.** The `.rc`, `win_common.cpp`, the plist, the bundle
(W64a, MACa); the AppImage (CIe); the UI repository.
**Depends on.** RB merged.
**Gate.** Linux: the five builds; ctest green twice, none skipped, 310 stated; P0 identical (no window is made
headless); the generator run twice gives identical bytes; `--check` green from a clone made with `core.autocrlf=true`
(the `.gitattributes` holds); the approved files' SHA-256 equal to `agents/nulldc3/icon/nulldc-icon/`'s. The real look
of the window icon is the owner's, by hand (`nulldc3` started windowed on the owner's desktop; PLG's list).
**Verifier.** Each must turn a named target red: one byte of a PNG; an `.ico` entry dropped or reordered; the 256 px
entry's `bWidth` / `bHeight` written as 255, or the 128 px entry's as 0 (256 written into the byte is 0, the right
value, so it is no mutation); the `.icns` truncated or its total length wrong; A and R swapped in the header; a value
dropped; a 32 px PNG under the 24 px name; the widening done with sign extension; the property set after mapping (read
the diff); the header with CRLF in the working tree of an `autocrlf=true` clone (must stay green: `-text`).

### 2.7 VTe — the emulator's version

**Delivers.** (1) `nulldc3/version-base.txt` (`3.0.0-pre`) and `nulldc3/cmake/version.cmake`: section 5's rule in CMake
script mode (git by `execute_process` with an argument list, `-C <source>`, `GIT_OPTIONAL_LOCKS=0`; the checkout test of
5.1); the old module and `libswirl/version.h.in` deleted. (2) A target run at every build that writes, only when their
text changes, `<build>/generated/nulldc3_version.h`, `<build>/nulldc3-version.txt` and `<build>/nulldc3-version.cmake`
(PL-D25); the emulator depends on it; nothing is written into the source tree. (3) **One reader translation unit**
(PL-D75): `libswirl/nulldc_version.h` (new, no `#include`) declares `nulldc_version()`, `nulldc_title()`,
`nulldc_git_hash()` and `nulldc_git_dirty()`; `libswirl/version.cpp` is the only C++ file that includes
`nulldc3_version.h` and includes nothing else but `nulldc_version.h`; it defines `nulldc_version()`, `nulldc_git_hash()`
and `nulldc_git_dirty()`; `libswirl/version_title.cpp` (includes `types.h`, never the generated header) defines
`nulldc_title()` (`VER_EMUNAME`, a space and `nulldc_version()`); `libswirl/types.h` includes `nulldc_version.h` and
`VER_SHORTNAME` / `VER_FULLNAME` expand to `nulldc_title()`; the generated directory is on `version.cpp`'s include path
alone (a source-file property). (4) The readers of 5.3, through those functions: `debugger.describe`'s
`emulator.version`, `dc.BUILD`, the paused title, the page without a bundle, the titles (no `__DATE__` / `__TIME__`),
the imgui About (Version, Git Hash; Build Date goes), the welcome screen (the version whole, no parsing), `--version`
and its line in `--help`, the man page's switch list (PL-D27, PL-D28); `tests/debugger/selftest/window_checklist.sh`'s
expected paused title. (5) The tests projects (PL-D75): the tests project runs `version.cmake` into its own
`<build>/generated` (a step in `tests/CMakeLists.txt` before the debugger suites) and compiles `version.cpp` and
`version_title.cpp` into the library that holds `cfg/cl.cpp` (that project defines the `TARGET_*` macros through
`config.cmake`); the standalone `tests/debugger/net` project, which defines none and includes no `types.h`, runs its own
step under a target name of its own (it is also a subdirectory of the tests project: one name for both would stop the
configure) and compiles `version.cpp` alone into the `dbg_webui` object library, so that `test_webui`, `dbg_net_demo`
and every executable that takes `dbg_webui`'s objects link it; `dbg_webui.cpp` and `test_webui.cpp` include
`nulldc_version.h`, and `test_webui.cpp` compares the page's title with `nulldc_version()`. (6) H8-4. (7) Tests:
`version.derive` (scratch repositories under the build directory, inline identities: every row of 5.1; the worktree,
submodule (`-c protocol.file.allow=always`), nested-repository, shallow (a `file://` clone) and non-`v`-tag cases; `<h>`
asserted to be 10 hexadecimal digits and a prefix of `git rev-parse HEAD`; a describe output of another form taken as no
tag; a later tag `v3.0.1` over base `3.0.0-pre` giving `3.0.1` and the numbers `3,0,1,0`); `version.binary` (the
emulator's `--version` equals the rule applied to the source tree; skipped when no emulator is known); the selftests'
expectations (`dbg_selftest_core`, `dbg_selftest_regs`), `test_webui.cpp`, `system.help`. (8) The consumers' contract of
5.2 documented in `nulldc3/README.md`.
**Owns.** `nulldc3/version-base.txt`, `nulldc3/cmake/version.cmake`, `libswirl/version.cpp`,
`libswirl/version_title.cpp`, `libswirl/nulldc_version.h`, the version lines of `CMakeLists.txt`,
`libswirl/version.h.in` (deletion), `libswirl/.gitignore`, the version readers of 5.3 in `libswirl/**`,
`libswirl/types.h` (`VER_*` and the include), `libswirl/cfg/cl.cpp` (`--version`, help), `libswirl/debugger/debugger.h`
(H8-4), the selftest expectations named, `tests/debugger/net/test_webui.cpp` and the version step of
`tests/debugger/net/CMakeLists.txt`, `tests/CMakeLists.txt` (`version/` and the version step),
`tests/common/libswirl.cmake` and `tests/cfg/CMakeLists.txt` (`version.cpp`, `version_title.cpp` and the include), the
paused title of `tests/debugger/selftest/window_checklist.sh`, `tests/version/**`, `tests/system` (help case, a version
case), the man page's switch, `README.md`, `docs/nulldc3/automated-runs.md` and `nulldc3/README.md` (their version
parts). **Must not touch.** The `.rc`, the plist (W64a, MACa); the UI repository.
**Depends on.** IC merged.
**Gate.** Linux: the five builds (each says the same version); ctest green twice, none skipped, the number stated; P0
identical; a second build at the same commit runs the version step and recompiles nothing (`ninja -d explain`); after a
commit made in the worktree, the next build, without a reconfigure, recompiles `version.cpp` alone and relinks (`ninja
-d explain`); the default build's `ninja -t commands` against the base's: `version.cpp`'s and `version_title.cpp`'s
commands are new, and only the files VTe edits differ; the standalone `tests/debugger/net` project configured, built and
its ctest green by itself (`cmake -S tests/debugger/net`, as its header documents); the version derived from its own
`HEAD` in a plain `git clone` of the VTe branch under `agents/nulldc3/vte/` (`.git` a directory), in the worktree
(`.git` a file), and in that clone used as a submodule of a scratch superproject, every one of these builds configured
with `-DNULLDC_DEVTOOLS_DIST=OFF`.
**Verifier.** Mutations: the header written into the source tree; the version made at configure time only (a commit
leaves it stale: a named test red); the worktree case via the old `.git` parsing; `<h>` from describe's abbreviation
(the length assertion); untracked files counted dirty; the `v` kept; `.dirty` lost; the numbers taken from the base
while a tag is reachable (the `v3.0.1` case); `dc.BUILD` back to `3.0.0`; `__DATE__` back in a title (read the diff);
the paused title literal; `nulldc3_version.h` included by a second translation unit, `version_title.cpp` among them (the
`ninja -d explain` check after a commit); `version.cpp` including `types.h` (the standalone net build stops on `TARGET_*
not defined`); the net project's version step under the tests project's target name (the tests project's configure
stops).

### 2.8 W64a — the MSVC build and Windows headless

**Delivers.** (1) The MSVC flags (PL-D32): `config.cmake` appends to CMake's MSVC defaults instead of replacing them,
`/EHsc /GR /utf-8 /bigobj /Zc:__cplusplus /permissive-`, static CRT, `/Z7`, `/STACK:8388608`, `/CETCOMPAT:NO`; the tests
project inherits them; Linux compile commands unchanged; `winmain.cpp` and one translation unit of `tests/common` carry
`#if defined(_MSC_VER) && !defined(_CPPUNWIND)` / `#error "MSVC build without /EHsc: JSON_THROW would be std::abort"`.
(2) The codecs on MSVC (PL-D33). (3) `nulldc3.rc` compiled: the icon of IC, `VERSIONINFO` from VTe's header
(`FILEVERSION NULLDC_VERSION_NUMBERS`, the strings `NULLDC_VERSION`, "nullDC Team"; the generated directory on the
`.rc`'s include path alone); the window class's large and small icons (`RegisterClassEx`). (4) Windows headless
(`windows.md` 11.2 H1 to H10; PL-D34 to PL-D36): no window, no input, no GL, no `CoInitialize` (it moves into the
windowed path of `os_CreateWindow`); the no-dialog policy on every run; `die` / `verify` without `__debugbreak` unless a
debugger is attached; the vectored fatal handler with the scope of PL-D34 and the line and exit codes of PL-D35;
binary-mode stdout and stderr; `main` returns `runctl_exit_code()`; DirectSound on the desktop window when there is
none; a message box only for a run whose output nobody keeps and which is not headless. (5) The ABI fixes of PL-D37:
XMM6-15 saved by `Mainloop` / `RunBlock`; the vmem placeholders; the unwind table over the real code region;
`_mm_setcsr`; and the cleanups (`decoder.cpp`'s `HASH` lines, `oslib.h`'s `__assume`, `winmain.cpp`'s dead `WinMain`
branch). (6) `tests/winrun/` (4.3, PL-D69): `nulldc3-winrun.exe` (the private desktop in the caller's window station,
the job, the sampling of windows, the verdicts 124, 125 and 126, `--timeout`, `--own-console`, `--detach`, `--probe`),
built by the tests project and by a bootstrap script. (7) The test hooks `NULLDC_TEST_FATAL` (eight kinds on Windows:
`segv`, `ill`, `trap`, `abort`, `div`, `stack`, `invparam`, `purecall`) and `NULLDC_TEST_UI_REPORT` (`nulldc-ui:
app=<0|1> windows=<n> errmode=<hex> wer=<hex> invparam=<0|1> purecall=<0|1> sigabrt=<0|1> abort=<hex> classes=<list>` on
Windows: `invparam`, `purecall` and `sigabrt` are 1 when `_get_invalid_parameter_handler()`, `_get_purecall_handler()`
and the SIGABRT handler read back with `signal(SIGABRT, SIG_GET)` are the policy's own; `abort` is
`_set_abort_behavior(0, 0)`, which reads the flags without changing them; `app=<0|1> windows=<n>` on Darwin), Windows
and Darwin builds only. (8) `tests/windows/`: `windows.winrun.probe`, `windows.headless.nowindow` (the report with
`windows=0`, `wer=` with `WER_FAULT_REPORTING_NO_UI`, `invparam=1 purecall=1 sigabrt=1`, `abort=` without
`_WRITE_ABORT_MSG` and `_CALL_REPORTFAULT`), `windows.fatal.<kind>` (`invparam` and `purecall` positive tests of a
correct build), `windows.helpbox` (three cases), `windows.abi.xmm` (an Xbyak canary thunk around `RunBlock` /
`Mainloop`), `windows.binary.stdout`, `windows.exitcode`. (9) The C++ suites of the tests project on MSVC; the
emulator-driven directories and the sh4bp suite skipped on Windows with the reason "W64b" (`system`: "the Linux-only
system-test harness"); the four objdump oracles a platform skip on hosts that are not Linux; portability fixes (PL-D68:
compile-only, and runtime fixes of `tests/rebrand`, `tests/icons`, `make_icons.py` and `tests/version` where the Windows
host is the first to run them). (10) `README.md` Windows quick start; `docs/nulldc3/automated-runs.md` Windows headless.
(11) `try\nulldc3.exe` for the owner (PL-D72): a Release build that embeds the pin's UI, its build time and peak memory
reported.
**Owns.** `libswirl/windows/**`, `libswirl/oslib/windows/**`, `audiobackend_directsound.cpp`, the Windows lines of
`CMakeLists.txt` and `config.cmake`, `nulldc3/windows/**` but the `.ico`, `rec_x64.cpp` (`Mainloop`, `RunBlock` only),
`sh4_core_regs.cpp` (the MSVC branch), `decoder.cpp` (the `HASH` lines), `oslib/oslib.h` (`__assume`), the codecs' MSVC
configuration headers, `tests/winrun/**`, `tests/windows/**`, the MSVC lines of the tests project's CMake files, the
W64a lines of 2.2 (the Windows skip reasons of the emulator-driven CMake files and of
`tests/debugger/sh4bp/CMakeLists.txt`, the oracles' platform skip in `tests/debugger/disasm/CMakeLists.txt`, the
`#error` in one translation unit of `tests/common`), `version.binary`'s launcher on Windows, the portability fixes of
PL-D68 (compile-only; runtime in the earlier packages' test scripts), the Windows rows of the READMEs. **Must not
touch.** The x64 JIT's debugger stub and `DebugChecks` (W64b), the harnesses (W64b), any Linux path's behaviour, the
frozen headers.
**Depends on.** VTe merged.
**Gate.** Linux: the five builds; ctest green twice, none skipped, the number stated; P0 identical; the default build's
compile commands equal the base's but for the files W64a edits (`ninja -t commands`). Windows host: `winrun` built by
the bootstrap script, then `windows.winrun.probe`'s run (`winrun --probe`: the private desktop's name, verdict 125); the
default (Release), `-DNULLDC_DEBUGGER=OFF` and `-DTARGET_NO_REC=ON` builds with VS 2026 (each long step through `winrun
--detach`); the first-run protocol of 4.3 in its order; through `winrun`, with P0's keys (PL-D71): `--version`,
`--help`, a refused command line (exit 2), the guest 90 vblanks and 2ndmix 600 on the JIT and the interpreter twice each
(one hash per engine), the eight fatal kinds (the line, the code, no window, nothing new in the crash-report places);
`dumpbin`: console subsystem, no CET-compatible flag, no VC runtime DLL; the version resource (the numeric file version
`3.0.0.0`, the `FileVersion` and `ProductVersion` strings equal to the build's version, `CompanyName` "nullDC Team");
the tests project's ctest inside an outer `winrun`: the C++ suites and `tests/windows` green twice with the skip list.
Reported, not gated: the Windows hashes against Linux's, a VS 2019 build, the first build's and the `try\` build's time
and peak memory. Expected duration: 4 to 6 hours of machine time.
**Verifier.** From its own clone on the Windows host, through `winrun` only, after reading `winrun.cpp`; its first run
is `windows.winrun.probe`. No mutation turns off `winrun`'s error mode or job flags (PL-D70). Mutations, each caught by
a named target: the headless guard of `os_CreateWindow` removed (`windows.headless.nowindow`: the report says
`windows=1`, `winrun`'s sampled list names the class, verdict 125); `WerSetFlags` removed (`windows.headless.nowindow`
reads `wer=`); the CRT invalid-parameter or pure-call handler, the SIGABRT handler or the abort behaviour removed
(`windows.headless.nowindow` reads `invparam=0`, `purecall=0`, `sigabrt=0` or the abort flags set; no crashing mutant:
without its handler an invalid parameter ends in `__fastfail`, which goes past every handler and the job's flag straight
to WER, and a pure call ends in `abort()`, which the SIGABRT handler turns into the same line and code, so
`windows.fatal.invparam` and `.purecall` would show one mutant unsafely and miss the other; PL-D70); `__debugbreak` back
in `os_DebugBreak` (`windows.fatal.trap`: a `fault_handler` line appears); `CoInitialize` back on the headless path (the
report's `classes` lists `OleMainThreadWndClass`); text-mode stdout (a target that checks for `\r`); `main` returning 0
(an exit-code target); XMM not saved (`windows.abi.xmm`); the CRT back to `/MD` (the `dumpbin` check); `/EHsc` lost (the
`#error` stops the build); the message box shown for a headless refused command line (`windows.helpbox` case 2).
`SetErrorMode` removed from the emulator is judged by reading: `winrun`'s inherited mode masks it by design.

### 2.9 W64b — the gates on Windows

**Delivers.** (1) `tests/common/emurun.py` (PL-D5's helper; Python 3.9, standard library, PL-D74): the run of an
emulator or a node process for the three harnesses — environment (`SystemRoot`, `TEMP`/`TMP` in the work directory), no
`resource` / `preexec_fn` on Windows, `winrun` on Windows, `.exe`, `\r\n`-tolerant reading, `npm.cmd`, tree kill;
`selftest.py`, `contract.py`, `soundlines.py` use it and keep P0's keys (PL-D71). (2) The emulator-driven CMake
directories opened on Windows (Darwin stays skipped with the reason "MACb"); `NULLDC_EMULATOR` required on hosts where
`tests/system` builds none; `${CMAKE_EXECUTABLE_SUFFIX}`; `npm.cmd`, `tsx.cmd`. (3) `tests/debugger/sh4bp/selftest.sh`
ported to Python, run on every host (its Linux results equal). (4) The x64 JIT's debugger mechanism on Windows: the
check stub with 32 bytes of shadow space, the Windows flush budget, `DebugChecks` on for `_WIN32` (PL-D38); the sh4bp
gates on Windows. (5) `debugger.selftest.hostcycles` on the thread's CPU cycles (`QueryThreadCycleTime`;
`GetThreadTimes` counts in clock ticks of about 15.6 ms). (6) `soundlines.py` does not run `gen.sh` on Windows (it
prints `gen.sh --check was not run: no POSIX shell on Windows`); no test or script started on Windows resolves `bash` or
`sh`. (7) The Windows skip list with reasons; `tests/README.md` Windows section. (8) `try\nulldc3.exe` replaced by a
build of W64b's head (PL-D72).
**Owns.** `tests/common/emurun.py`, the three harnesses, the CMake files of `tests/debugger/{selftest,contract,guest}`,
`tests/aica_dsp/CMakeLists.txt` (the emulator-driven part), `tests/debugger/sh4bp/**`,
`tests/debugger/disasm/CMakeLists.txt` (`tsx` on Windows), `rec_x64.cpp` (the check stub, the budget, the `_WIN32`
guard), `dbg_selftest_hostcycles.cpp`, the READMEs' Windows rows. **Must not touch.** The product's Windows shell
(W64a's; a defect found there is reported to the integrator, who gives W64b the file or sends a fix round to W64a); the
frozen headers; the UI repository.
**Depends on.** W64a merged.
**Gate.** Linux: ctest green twice, none skipped, the number stated; P0 identical; the Linux sound lines and the sh4bp
sessions' compared notes as before the port. Windows host (VS 2026, the default build, `NULLDC_ELF_DIR` and the data
subsets of 4.1; ctest inside an outer `winrun`): every target that runs green twice, the skipped listed with reasons and
the number of each; the contract gates: M1 (18 passed each) and M2 (19 passed each) on the JIT (native), the interpreter
and the forced fallback, the M2 guest run (2 passed, both notes); M3 run 1 (20 passed each, three engines), run 2 on the
guest (3 passed with `sgc: captured with the arm7 held`, and `--only S00,S21` 2 passed with `dsp: stepped with the arm7
held`); the sh4bp gates; the sound neutrality targets; `windows.headless.nowindow` first. Expected duration: 5 to 7
hours of machine time.
**Verifier.** Mutations: the shadow space left out (sh4bp gates red on Windows); the Windows budget left at the Linux
value (the bound test red); `DebugChecks` off (the engine check of the harnesses red); `winrun` bypassed by the helper
(`harness.emurun`: a target of the helper checks that it ran through `winrun`); `\r` left in a parsed field; `env={}`
again (Winsock fails: the contract config mode red); `bash` resolved on Windows (`debugger.guest.dsptest.files` prints
the no-shell line there); `hostcycles` back on `GetThreadTimes` (read; the selftest's rates in the report).

### 2.10 MACa — the macOS bundle and macOS headless

**Delivers.** (1) The bundle by CMake (PL-D42): `add_executable(... MACOSX_BUNDLE ...)` on Darwin; `project(...
LANGUAGES C CXX OBJC OBJCXX)` there; `nulldc3/apple/Info.plist.in` (`git mv` of `Info.plist`; `CFBundleIdentifier
org.emudev.nulldc3`, `CFBundleIconFile nulldc3`, `NSHighResolutionCapable`, `LSMinimumSystemVersion` = the deployment
target, the copyright of PL-D13); the `.icns` and the licences of PL-D80 in `Resources` (`LICENSE-devtools` only when
the build embeds a UI, `DBG_WEBUI_BUNDLE_DIR` not empty; the configure stops naming `devtools/LICENSE` when it is
missing then, and never needs it otherwise); the version keys of 5.1 and the ad-hoc signature written by a custom
command with a stamp file (PL-D42: it runs at every build where the executable, `nulldc3-version.cmake`, the bundle's
plist or a resource is newer than the stamp, and verifies with `codesign --verify --strict`);
`CMAKE_OSX_DEPLOYMENT_TARGET` 11.0 by default; with `-G Xcode` no custom signing step; `build_app_bundle.sh` deleted.
(2) macOS headless (PL-D43): decided after `nulldc_init`, `NSApplication` made lazily in `os_CreateWindow` for a
windowed run; `os_SetupInput` returns headless; the UI loop on the main thread headless; exit codes 0 / 1 / 2 as Linux;
an alert only by PL-D43's rule; a windowed run that ends by itself terminates with `runctl_exit_code()`; window
restoration off; the About item shows the version (`orderFrontStandardAboutPanelWithOptions:`). (3) `darw_printf` and
its macro gone (PL-D44). (4) Automated runs end fatal signals with `_exit(128+N)` after the line of PL-D35 (PL-D45). (5)
arm64: `FEAT_AREC` none on arm64 hosts (PL-D46); 16 KiB pages for arm64 Darwin only (PL-D47); `shm_open` on Darwin, 8
MiB `cThread` stacks, `-ffp-contract=off` on arm64 hosts (PL-D48). (6) App Translocation named in the "cannot be
written" error (PL-D49). (7) The test hooks of W64a on Darwin (`NULLDC_TEST_FATAL`: `segv`, `ill`, `trap`, `abort`;
`div` only on x86_64 hosts); `tests/macos/`: `macrun.py` and `no-windowserver.sb` (PL-D69: the profile applied only when
the caller is not sandboxed already, 4.3), `macos.sandbox.canary` (it also prints the kernel's view: sandboxed or not,
and whether the window server's lookup is denied), `macos.headless.nowindow`, `macos.fatal.<kind>` (four kinds on
arm64), `macos.bundle.codesign` (the licence set that matches the embed; also after a bare reconfigure and a build),
`macos.icns.iconutil` (the read-only round trip of PL-D20), `macos.refused`; every one that starts the emulator does so
through `macrun.py`, under the profile it applies or under the caller's, and fails, never falls back, when
`sandbox-exec` is missing and `CI` is not set. (8) The C++ suites of the tests project on the Mac; the emulator-driven
directories skipped on Darwin with the reason "MACb"; portability fixes (PL-D68: compile-only, and runtime fixes of
`tests/rebrand`, `tests/icons`, `make_icons.py` and `tests/version` where the Mac is the first to run them). (9)
`README.md` macOS quick start; `docs/nulldc3/automated-runs.md` macOS headless. (10) `try/nulldc3.app` for the owner
(PL-D72): a Release bundle that embeds the pin's UI, its build time and peak memory reported.
**Owns.** `nulldc3/apple/**` but the `.icns` (unless `iconutil` refuses it: then the `.icns` and the generator's `.icns`
part), the Darwin lines of `CMakeLists.txt` and `config.cmake`, `libswirl/types.h` (the `printf` block),
`libswirl/linux/common.cpp` / `posix_vmem.cpp` / `context.cpp` (Darwin branches), `libswirl/stdclass.h` (the page rule),
`oslib/posix/threading.cpp` (the stack size, Darwin), `cfg/cl.cpp` (the translocated case), `rec_arm64.cpp` only for
what the Mac build needs, `tests/macos/**`, the Darwin lines of the tests project's CMake files (the skip reason "MACb"
in the emulator-driven ones), `tests/version/**` (the Darwin launcher, if needed), the portability fixes of PL-D68
(compile-only; runtime in the earlier packages' test scripts), the macOS rows of the READMEs and the `apple/` part of
`nulldc3/README.md`. **Must not touch.** Linux behaviour; the harnesses (MACb); the frozen headers; the UI repository.
**Depends on.** W64b merged; the owner's prerequisites on the Mac (4.4).
**Gate.** Linux: the five builds; ctest green twice, none skipped, the number stated; P0 identical; Linux compile
commands unchanged but for the edited files. Mac (every emulator run through `macrun.py` under the profile, with P0's
keys): the default, `-DNULLDC_DEBUGGER=OFF` and `-DTARGET_NO_REC=ON` builds; the first-run protocol of 4.3
(`macos.sandbox.canary`, `--version`, `--help`, `macos.refused`, then `macos.headless.nowindow`); the canary in both
shapes of 4.3 (from a ctest that is not sandboxed, and from a ctest started under `macrun.py --detach`); the guest and
2ndmix on the JIT and the interpreter twice each (one hash per engine); the four fatal kinds (128+N, the line, no crash
report); `codesign --verify --strict --deep`; `plutil -lint`; the plist's keys and the licence set that matches the
embed; `iconutil` round trip; the C++ suites green twice with the skip list (RB's, IC's and VTe's scripts among them,
PL-D68's runtime fixes listed if any), every Python file of the round run with the Mac's `python3`. The windowed start
is the owner's, by hand (PLG's list). Expected duration: 3 to 5 hours of machine time.
**Verifier.** On the Mac from its own clone, every run through `macrun.py` under the profile. No mutant that reaches
AppKit or that would leave a crash report by design is run (PL-D70): `NSApplication` created before `nulldc_init`, the
headless guard of `os_CreateWindow` removed, the alert rule without its LaunchServices condition, `_exit` replaced by a
re-raise are judged by reading, and `macos.sandbox.canary` proves the profile. Mutations run, each caught by a named
target: the `printf` macro back (the tests do not link); the version keys written after the signature, or a key of the
template wrong (`macos.bundle.codesign`); the stamp command's dependency on the bundle's plist dropped
(`macos.bundle.codesign` after a bare reconfigure); `die` ending through `__builtin_trap` again in an automated run
(`macos.fatal.trap`: a `fault_handler` line); `FEAT_AREC` back on arm64 (after `macos.fatal.*` is green: the guest with
the ARM7 enabled under the fatal policy; name what caught it); `macrun.py` applying `sandbox-exec` whatever its caller
(the ctest under `macrun.py --detach`: its `tests/macos` targets red with `sandbox_apply`); `macrun.py` skipping the
profile for an unsandboxed caller (the canary from the unsandboxed ctest: its kernel view says not sandboxed);
`LICENSE-devtools` required without an embed (a gate configure with `-DNULLDC_DEVTOOLS_DIST=OFF` in a clone without the
submodule stops).

### 2.11 MACb — the gates on the Mac

**Delivers.** (1) The emulator-driven CMake directories opened on Darwin (with W64b's helper; its Darwin branch runs
through `macrun.py`), the bundle's executable as `NULLDC_EMULATOR`. (2) Whatever the harnesses need on Darwin beyond
W64b's helper (Python 3.9, standard library: PL-D74). (3) The Mac's skip list with reasons (x64-only suites, the objdump
oracles without the binutils, emsdk, the Linux harness). (4) Fixes of defects the gates find in Darwin code, each named;
a failure that is a gap of the design's arm64 rules (`registersStale`, the fallback engine) is reported to the
integrator, who decides (8.4); S04 on the recompiler, `--fallback` included, follows PL-D78: its rule in `contract.py`'s
`run_runner`, with a test of the rule on synthetic summaries that runs on every host. (5) `tests/README.md` macOS
section. (6) `try/nulldc3.app` replaced by a build of MACb's head (PL-D72).
**Owns.** The Darwin lines of the emulator-driven CMake files and of `emurun.py`; in
`tests/debugger/contract/contract.py`, PL-D78's rule in `run_runner` and its test; Darwin and arm64 branches of
`libswirl/**` a gate shows wrong (named in the report; 2.2's rows); the READMEs' macOS rows. **Must not touch.** Windows
and Linux behaviour; W64b's Windows logic; the frozen headers.
**Depends on.** MACa merged.
**Gate.** Linux: ctest green twice, none skipped, the number stated; P0 identical. Mac (the default build, the data of
4.2, every emulator run through `macrun.py`): every target that runs green twice, the skipped listed with reasons and
numbers; the contract gates M1, M2, M3 (run 1 on the JIT with its fallback debug engine, the interpreter and the forced
fallback; run 2 on the guest, both runs) with M3G's numbers, except S04 on the recompiler's runs, which is recorded, not
gated (PL-D78): on arm64 the JIT's runs with and without `--fallback` are the same engine (`engine.fallback` is true
there either way, `dbg_emu.cpp` `CaptureEngine`), behind the arm64 loop that restarts its slice, so on both every
scenario passes but S04, and S04 may fail there only on `dc.timing.SH4_CYCLES` moved by less than a slice; the
interpreter keeps M3G's numbers in full; `macos.headless.nowindow` first. Expected duration: 4 to 6 hours of machine
time.
**Verifier.** Mutations: the harness's engine expectation for arm64 inverted (red); `NULLDC_EMULATOR` pointed at a
missing file (configure says why; no silent skip under `NULLDC_TESTS_REQUIRE_DATA`); a sound line comparison across
hosts (none may exist: read); S04's exemption widened to another scenario, another field, or the interpreter, or its
slice bound dropped (the rule's test on synthetic summaries red; the result lines).

### 2.12 LA64 — the arm64 build on Linux

**Delivers.** (1) `rec_arm64.cpp` builds for ELF: a symbol-prefix macro for its assembly, `.hidden` back on ELF,
`pthread_jit_write_protect_np` under `__APPLE__`; `linux/context.cpp` reads `uc_mcontext.pc` on Linux arm64; the cache
flush and code region paths on Linux arm64 checked; a Linux arm64 kernel whose page size is not 4 KiB refused at start
with one clear line and exit 1 (PL-D52). (2) A cross build here: GCC 13 from Ubuntu's amd64 cross-toolchain packages or
clang 18 with `ld.lld`, an Ubuntu 24.04 arm64 sysroot, both unpacked from downloaded packages into the package's scratch
(no `apt install`, no `dpkg` change). (3) Runs under `qemu-aarch64-static`: headless guest and 2ndmix runs on the JIT
and the interpreter, twice each; the C++ suites that run in reasonable time under qemu (named). (4) The scripts of (2)
and (3) under `agents/nulldc3/la64/`, for CIe and PLG.
**Owns.** `libswirl/jit/backend/arm64/**`, `libswirl/linux/context.cpp` and `common.cpp` (Linux arm64 branches),
`stdclass.h` (the Linux arm64 refusal), the Linux arm64 lines of the CMake files, the compile-only fixes the cross build
needs (PL-D68). **Must not touch.** x86_64 code paths (the default build's compile commands unchanged), Darwin
behaviour.
**Depends on.** MACb merged.
**Gate.** Linux x86_64: the five builds; ctest green twice, none skipped, the number stated; P0 identical. Linux arm64
(cross, qemu): the build of the default configuration and of `-DTARGET_NO_REC=ON`; the runs of (3), one hash per engine.
Mac (every emulator run through `macrun.py` with P0's keys; long steps detached, inside which `macrun.py` runs under the
caller's profile, PL-D69; the submodule `devtools` initialised from the UI clone, 4.2): the default build;
`macos.headless.nowindow` first; 2ndmix and the guest on both engines equal to MACb's merge (the arm64 JIT unchanged for
macOS); the C++ suites green. Expected duration: 3 to 5 hours of machine time (qemu is slow). What only CI can show: a
native Linux arm64 build with GCC on `ubuntu-24.04-arm`, first on the round's PR.
**Verifier.** Mutations: the prefix macro wrong for Mach-O (the Mac's build fails to link); the protect calls left
unconditional (the Linux cross build fails); the context's PC from the Darwin layout (a fault run under qemu dies); the
page refusal inverted (a 4 KiB run refuses).

### 2.13 CIe — the emulator's CI and its release files

**Delivers.** (1) `nulldc3/ci/`: `linux.sh` (`deps`: the apt packages and `ccache`; `ui`; `build <dist-native>`: the
release emulator and the `-DHAS_TA_LLE=OFF` emulator; `test <ci|full>`: the tests project, ctest with the skip list),
`appimage.sh` (PL-D51, PL-D81: pinned `linuxdeploy` and the static type-2 runtime per architecture, each with its
SHA-256, `--runtime-file`, nothing downloaded while packaging; it ends with a smoke run of the file it made),
`windows.ps1` (Windows PowerShell 5.1, PL-D82: vswhere, `vcvars64`, the release build with `<dist-native>`, the tests
project, ctest inside an outer `winrun`, the zip made with `tar.exe -a -c -f`, a smoke run of the unpacked zip through
`winrun`), `macos.sh` (the release bundle with `<dist-native>`, `codesign --verify --strict --deep`, the tests project,
ctest, the zip with `ditto`, a smoke run of the bundle unpacked with `ditto -x -k`; on a runner ctest is not sandboxed
and each `macrun.py` applies the profile, on the Mac `macos.sh` runs under `macrun.py --detach` and the inner ones run
under it, PL-D69), `release.sh` (PL-D76, PL-D77: two explicit invocations, `check` and `publish`, the four configure
lines, a draft that is checked and then published by `publish` alone), `release-notes.md` (a template), `hygiene.py`
(6.6: `--allow <file>`, `--extra <file>`, whose patterns it matches without regard to letter case), `hygiene-allow.txt`,
`hygiene-allow-ui.txt`, `check_workflows.py`; every script takes its paths, its data mode, the UI bundle's directory and
**the UI checkout `<ui>`** as arguments (PL-D85: `npm ci` runs in `<ui>` for `test`, and `LICENSE-devtools` is copied
from `<ui>/LICENSE`, the script stopping with the file's name when it is missing or `<ui>` is not at the tree's
gitlink), and every Python file keeps to Python 3.9 and the standard library (PL-D74). (2) `.github/workflows/ci.yml`
(6.3). (3) The AppImage's portable directory in `find_host_dirs` (PL-D51), the help text, the man page, README "Where
the files are"; the system tests `dirs.appimage` and `dirs.appimage.inherited` (and their `.hle` twins). (4) The desktop
file's `Exec=nulldc3 %f` and `[el]`. (5) `CMAKE_VERBOSE_MAKEFILE` overridable (default as today); an option
`NULLDC_PREFIX_MAP` (default off) that adds `-ffile-prefix-map` on GCC and Clang and `/PDBALTPATH:%_PDB%` on MSVC
(PL-D61). (6) `tests/ci/`: `ci.hygiene`, `ci.workflows` (the rule check of 6.5). (7) `README.md`: downloads, the release
files, CI; `.gitignore` for the AppDir and outputs; the licences in every release file (PL-D80). **Owns.**
`.github/workflows/**`, `nulldc3/ci/**`, `find_host_dirs` in `libswirl/linux-dist/main.cpp` and its comment,
`cfg/cl.cpp` (help), the man page's directory text, the desktop file's `Exec` and locale, the two CMake options,
`tests/system` (the new cases), `tests/ci/**`, `tests/CMakeLists.txt` (one line), the rows of
`tests/rebrand/allowed.txt` its edits touch, `README.md`, `.gitignore`. **Must not touch.** The platform shells'
behaviour (W64a, MACa); the harnesses; the UI repository. **Depends on.** LA64 merged; LA64's cross scripts. **Gate.**
Linux: the five builds; ctest green twice, none skipped, the number stated; P0 identical; `linux.sh deps` is not run
here (the packages are present: the report says so); `linux.sh build`, `test` and `appimage.sh` here, with `<ui>` =
`agents/nulldc3/wt/ui-pin` (PL-D85): the x86_64 AppImage from a release build that embeds the UI, its smoke run
(`--version`, a headless guest run, the portable directory equal to the AppImage's directory), its
`usr/share/doc/nulldc3/` listed from a copy extracted with `--appimage-extract` in a scratch directory (exactly the five
licences of PL-D80), and the release binary running P0's default rows equal to P0 (PL-D83). `windows.ps1` on the Windows
host and `macos.sh` on the Mac (`<ui>` = the UI clone `src\ui` / `src/ui`), each in `ci` mode (the counts and skip lists
a PR's job summary must show) and once in `full` mode (W64b's and MACb's numbers), each with its smoke run; the Windows
zip's entries listed (no `\`, the top folder `nulldc3-<v>-windows-x86_64/`, the five licences). `release.sh` against a
`gh` stub: `publish` in a scratch clone with a scratch tag and `GITHUB_EVENT_NAME=push` (draft, assets checked,
published; a re-run with the draft present; a published Release refused; a UI version `3.0.0-pre+g<h>` refused; a
scratch tag `v3.0.1` refused; a configure line that names another UI version refused; the same tag with
`GITHUB_EVENT_NAME=workflow_dispatch` refused) and `check` (a pull request; a dispatch with `tag`, and with
`require_ui_tag`; a run with `GITHUB_EVENT_NAME=push` and `GITHUB_REF_TYPE=tag`: no `gh` call recorded); nothing created
outside `publish`. `actionlint` clean on both repositories' workflows; `ci.workflows` and `ci.hygiene` green;
`hygiene.py --extra <the full patterns of PL-D61>` clean on both repositories (the UI's with `--allow
nulldc3/ci/hygiene-allow-ui.txt`). What only GitHub shows: the runner images, the `ubuntu-24.04-arm` native build, the
token's permissions, the artifact transfer between jobs, the Release — first on the round's emulator PR (`release-check`
included), then the dispatch checks on `main`, then the tag (sections 6.5, 7). Expected duration: 4 to 6 hours of
machine time across the three systems. **Verifier.** Mutations: the `APPDIR` check dropped (`dirs.appimage.inherited`
red); `$APPIMAGE` taken as the directory (`dirs.appimage` red); the release job given `contents: write` at the top
level, `release` without its `if:` or with its event condition dropped, `release` needing `release-check`,
`release-check` given `contents: write`, or a third-party action by tag (`actionlint` does not catch these:
`ci.workflows` must); the tag check removed from `release.sh`, or `publish` without its event check (its stub test red);
the dist argument dropped from `windows.ps1` (the configure-line check of `release.sh` red); the `LICENSE-devtools` copy
made optional or dropped in `appimage.sh` or `windows.ps1` (the licence listings red; and with `<ui>` pointed at a
directory without `LICENSE` the script must stop); `Compress-Archive` instead of `tar.exe` (the entry listing); a home
path added to a tracked file (`ci.hygiene` red); the macOS zip made with `zip -r` (the signature check of the unpacked
bundle red); a download during packaging (read the scripts).

### 2.14 PLG — the round's gate, the documents, the PR texts; the PRs and the tags by the integrator

On the merge of every other package, with the UI pinned at VCu's verified head. PLG runs through the package workflow
for items 1 to 9; item 10 is the integrator's, outside the workflow (PL-D84). (1) Linux: the five builds; ctest green
twice, none skipped, the number stated; P0 identical; the contract gates M1 to M3 against emulators that carry the UI's
bundle (as M3G). (2) The Windows host: W64b's gate again on the merged tree, and the zip made by `windows.ps1` (its
smoke run through `winrun`; its version resource). (3) The Mac: MACb's gate again, the zip made by `macos.sh` (unzipped
in the work folder, `codesign --verify`, the inner executable's `--version` and a headless guest run through
`macrun.py`). (4) LA64's cross runs again. (5) The release files here: the x86_64 AppImage by `appimage.sh` from a build
that carries the bundle, with `<ui>` = `agents/nulldc3/wt/ui-pin` (PL-D85), its smoke run, its `usr/share/doc/nulldc3/`
listed (the five licences of PL-D80), and its binary running P0's default rows equal to P0 (PL-D83). (6) `actionlint`,
`ci.workflows`, `ci.hygiene`, and `hygiene.py --extra` with the full patterns (PL-D61) on both repositories, every
document the round commits and both PR texts; `agents/nulldc3/plg/` scanned with the remote patterns by 4.3's scan
(PL-D61, PL-D73): the KOS banner lines of items 1, 4 and 5's runs of 2ndmix and hello are counted, not failures.
(7) The documents of Appendix D, with PLG's rows of `tests/rebrand/allowed.txt` and `nulldc3/ci/hygiene-allow.txt` for
them (PL-D79). (8) The two PR texts drafted under `agents/nulldc3/plg/` (the owner's by-hand list of PL-D40 in the
emulator's; PL-Q3's answer, or that work proceeded on PL-D61; the AWS key note). (9) The integrator's notes of Appendix
I and `PLAN.md`'s status. (10) The pushes, the PRs, the submodule move, the dispatch checks, the tags and the checks of
7 item 10, in section 7's order, by the integrator.
**Owns.** The documents; the rows of the two allow files its documents need; the `README.md` part 2.2 gives it;
`agents/nulldc3/plg/`. No product code, no test logic, no CI logic. The integrator (item 10): the pushes, PRs, the
submodule commit, dispatches and tags.
**Gate.** Items 1 to 9 with their numbers; the gate ends there.
**Verifier.** Items 1 to 9 only: reruns item 1's contract gates and items 2's and 3's suites; checks one row of Appendix
D per section; checks the PR texts and every committed document with the full patterns, and `agents/nulldc3/plg/` with
the remote patterns (4.3's scan).
**After the package (the integrator, outside the workflow; PL-D84; not part of the package's gate).** Item 10: section
7, step by step with the owner; then the integrator's own checks of 7 (the emulator PR's gitlink names the UI's `main`
commit; the tags point at the merged heads; the logs of the first CI runs read for host details, downloaded with `gh run
view --log` and scanned with the full patterns), recorded in Appendix I.
**Since A64** (2026-10-23): PLG runs on A64's merge; its items 1, 3 and 4 include the arm64 targets A64 opened (the 13,
`emurun.pauses`, the sh4bp suite) and the arm64 recompiler's new state-hash references (PL-D86); the release notes list
no arm64 known limit.

### 2.15 A64 — the arm64 recompiler's native debugger (added 2026-10-23)

At the owner's request, between CIe and PLG. **Delivers.** In two steps, both before PLG (the owner): (1) the slice
counter kept across `Mainloop`'s exits, tested in front of a block's charge (PL-D86), the guest pc stored at every slice
end and `CtxAtSlice` stated, the full `d8-d15` frame; the 13 targets of `nulldc_a64_known_limits` pass on every arm64
host and the list, `debugger.a64.knownlimits` and PL-D78's S04 rule go; (2) `DebugChecks`: the check stub, the budget
and `rdv_BlockRest`, flow reports, `RunBlock`, temporary blocks that never link (a shared flag), the out-of-space rule;
the engine on arm64 is `sh4-debug-native`; the sh4bp suite runs on arm64. **Owns, gate, verifier.** The brief's.
**Depends on.** CIe merged.

---

## 3. Order and bases

One package at a time. "Based on" is the head of the integration branch of its repository at that moment. The UI
checkout an emulator gate runs with is the submodule's gitlink, moved by the integrator in a commit of its own on
`skmp/nulldc3` to VCu's verified head before RB is cut, with `agents/nulldc3/wt/ui-pin` (a detached worktree with
`node_modules`) at the same commit, given as `NULLDC_DEVTOOLS_DIR` and as the CI scripts' UI checkout `<ui>` (PL-D85;
the worktrees' own `devtools/` stays an uninitialised, empty directory). On the Windows host and the Mac the UI is a
clone of the same commit with `npm ci` run there (4.1, 4.2), and it is `<ui>` there. No other pin move happens until
PLG.

| # | Package | Based on | What the owner can try when it is merged, and where |
| --- | --- | --- | --- |
| 1 | **VCu** | devtools `3cf8b15` | the UI branch: `npm run build:native` says `3.0.0-pre+g<hash>` in the About dialog and the status bar; the workflow file to read |
| 2 | **RB** | devcast at the pin move to VCu | the tree with `nulldc3/`: builds as before; `git log --follow` of a moved file; `nulldc3 --help` unchanged |
| 3 | **IC** | RB | Linux: `nulldc3` started windowed on the desktop shows the snail in the title bar, the task bar and Alt-Tab |
| 4 | **VTe** | IC | `nulldc3 --version`; the About and welcome screens; the UI's About dialog row "Version" against the emulator |
| 5 | **W64a** | VTe | the Windows host: `C:\nulldc3-work\try\nulldc3.exe` (a Release build that embeds the UI, PL-D72): started by double-click (a window with the icon, a console beside it), Explorer's Properties → Details (version, "nullDC Team") |
| 6 | **W64b** | W64a | the Windows host: `try\nulldc3.exe` (now W64b's build) started with `--config Debugger:Enabled=yes`; the UI it serves, in a Windows browser at `http://127.0.0.1:55543/`, debugging on the native engine |
| 7 | **MACa** | W64b | the Mac: `~/nulldc3-work/try/nulldc3.app` (a Release bundle that embeds the UI, PL-D72) opened from the Finder: the Dock icon, About nullDC with the version and "nullDC Team" |
| 8 | **MACb** | MACa | the Mac: `try/nulldc3.app` (now MACb's build) started from a Terminal with `--config Debugger:Enabled=yes`; the UI it serves, in a browser |
| 9 | **LA64** | MACb | nothing on the owner's machines (no arm64 Linux machine is named) |
| 10 | **CIe** | LA64 | this machine: the x86_64 AppImage in `agents/nulldc3/cie/out/` started from the desktop (its files beside it); the Windows host's and the Mac's zips in their work folders |
| 11 | **A64** | CIe | the Mac: a `--config Debugger:Enabled=yes` run of the arm64 build, debugging on `sh4-debug-native` (breakpoints and steps on the recompiler); the arm64 host: the same, headless |
| 12 | **PLG** | everything | the two PRs with their CI runs and the PR artifacts (7 days); then the two Releases |

- **Why this order.** RB first among the emulator packages: it moves the directory every later package edits. IC and VTe
  before the platforms: W64a compiles the `.rc` with IC's `.ico` and VTe's header, MACa builds the bundle with IC's
  `.icns` and VTe's file. Windows before macOS: the shared run helper (W64b) is written once, against the harder system,
  and MACb only opens it for Darwin. LA64 behind MACb: its change of the arm64 JIT is checked against the Mac's runs
  that MACa and MACb established. CIe last: its scripts call every platform's build and headless mode.
- **No red window.** No package leaves a system that builds broken for the next: RB's move commit builds, its path lines
  in the same commit; between RB and W64a the `.rc` names an `.ico` that IC adds, but no build compiles the `.rc` until
  W64a; between RB and MACa `build_app_bundle.sh` no longer copies an icon, and no build of macOS is made until MACa.
  Nothing is pushed between packages.
- **What can be prepared while another package has the machine** (documents only): for W64a, the bootstrap script of
  `winrun` and the list of the Windows host's tools; for MACa, the sandbox profile and the Mac's environment line; for
  LA64, the list of arm64 packages of the sysroot; for CIe, the pinned SHAs of the actions (`gh api`, read-only).
- **The package workflow** (`agents/nulldc3/resume/pkg-brief.workflow.js`) is started for every package of this round
  with `rules` naming section 9 of this file (by its absolute path) and with the access details of the Windows host and
  the Mac in `paths` for the packages that use them (W64a, W64b, MACa, MACb, LA64, CIe, PLG; RB for its optional
  configure; a package that works on the Mac gets the Windows host's host name too, `PL_WIN_HOST`, which 2ndmix and
  hello print there: 4.3), and the remote patterns of PL-D61 as a file outside `agents/nulldc3/` for the same packages'
  scan: `paths` reaches the engineer, the verifiers, the reviewers and the fixers; `extra` reaches the engineer
  only. The workflow forbids pushes, so PLG's item 10 (the pushes, PRs, the submodule move, dispatches and tags) is not
  run by it: the integrator makes those steps in the orchestrating session, step by step with the owner (PL-D84).

---

## 4. The remote systems

### 4.1 The Windows host

- **What is there** (the round's task text, and a read-only listing of 2026-10-19): Windows 11, 24 logical processors,
  32 GB shared with this VM (which holds 15 GB); Visual Studio 2026 Community (MSVC 14.51, with its CMake and Ninja) and
  Visual Studio 2019; Git for Windows; Node.js under `C:\Program Files\nodejs`; **a Python 3.11 at `C:\Python311`**
  (`windows.md` 12.2 did not know it); Windows PowerShell 5.1 (`powershell.exe`; no `pwsh` is assumed). The work folder
  `C:\nulldc3-work` does not exist yet: W64a makes it.
- **Reaching it.** WSL interop: `cmd.exe` and `powershell.exe` from WSL's mount of the Windows system directory.
  `cmd.exe` cannot use a WSL network path as its working directory: every command runs as `cmd.exe /d /c "<script>.cmd"`
  with the script written into `C:\nulldc3-work\run\` from WSL, or `powershell.exe -NoLogo -NoProfile -NonInteractive
  -ExecutionPolicy Bypass -File C:\nulldc3-work\run\<script>.ps1`. **Nothing opens a console of its own**: no `start`,
  no `Start-Process`, no `CREATE_NEW_CONSOLE` (on Windows 11 a new console can be handed to Windows Terminal, whose
  window opens on the owner's desktop); a program without a console gets `CREATE_NO_WINDOW`. Output comes back through
  the scrubber of 4.3 into the package's logs under `agents/nulldc3/<pkg>/logs/`.
- **Layout.** `C:\nulldc3-work\{in, src, b, try, data, tools, tmp, run, npm-cache}` (short paths: nested builds make
  long object paths).
- **Feeding the tree.** `git bundle create` from WSL into `C:\nulldc3-work\in\` (the emulator's branch, the UI at the
  pin; with `--tags` where tags exist), cloned on Windows with `git clone -c core.autocrlf=false -c core.eol=lf` into
  `src\nulldc3` and **`src\ui`** — never a directory named `dreamcast-devtools` beside the checkout, which is the bundle
  look-up's second candidate (PL-D72); **the submodule `devtools` initialised from the UI clone** (`git config
  submodule.devtools.url <the UI clone>` in the clone's local config, then `git -c protocol.file.allow=always submodule
  update --init devtools`: the file transport is blocked for submodules since git 2.38.1), so that the emulator clone is
  complete as a runner's checkout is (PL-D80; the CI scripts take the UI checkout as an argument, here `src\ui`:
  PL-D85); the two vector submodules cloned from GitHub
  (public). Updates by a new bundle, `git fetch`, `git checkout --detach <sha>`, and `git rev-parse HEAD` compared with
  the WSL commit before every gate step. **Nothing is committed or pushed from a Windows clone**: a change is made and
  committed in the package's worktree here and sent again. No `git worktree` under the C: drive's WSL mount.
- **Tools.** VS 2026 located with `vswhere -latest`, `vcvars64.bat` in every build script, `VSCMD_SKIP_SENDTELEMETRY=1`,
  VS's CMake and Ninja. Python: `C:\Python311\python.exe`, used as is (nothing installed into it; `-I` where a script is
  run directly; `PYTHONDONTWRITEBYTECODE=1`); every Python file of the round keeps to Python 3.9 and the standard
  library (PL-D74). Node: the installed one if `node --version` is at least 22.13, else the official Windows zip of Node
  22 unpacked into `tools\node` (PL-D41). `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never`.
- **Inside the work folder.** Every script sets `TEMP` and `TMP` to `C:\nulldc3-work\tmp`, `npm_config_cache` to
  `C:\nulldc3-work\npm-cache`, `npm_config_update_notifier=false`, `npm_config_fund=false`, `npm_config_audit=false`,
  `PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1`; MSVC writes `/Z7` (no `mspdbsrv.exe` left running); every emulator run has
  `--portable <dir under the work folder>` and P0's keys (PL-D71).
- **Data.** `data\elf\` (2ndmix.elf, hello.elf, copied from the ELF directory of the gates), and subsets of the sibling
  checkouts the suites read (never whole trees: `windows.md` 10.3): the package derives each list from what the suites
  open and names it in its report.
- **Running tests and the contract runner.** The gate's emulator builds are configured with `-DNULLDC_DEVTOOLS_DIST=OFF`
  (only `try\` and the release files carry the UI, with an explicit path). The tests project is configured with
  `-DNULLDC_EMULATOR=<the build's nulldc3.exe>`, the data paths, `-DPython3_EXECUTABLE=C:/Python311/python.exe`,
  `-DNULLDC_NPM=<npm.cmd>`, `-DNULLDC_TESTS_REQUIRE_DATA=ON`; `ctest -j4` runs inside an outer `winrun` (4.3). The four
  objdump oracles are platform skips here (W64a). The contract runner runs **on Windows**, against `127.0.0.1`, from the
  Windows UI clone; never from WSL (a non-loopback bind raises the firewall's dialog).
- **Long steps.** A step longer than a few minutes (a build, a ctest run, a contract gate) is started detached, writes
  its own log and exit-code file inside the work folder, and is polled from WSL: `nulldc3-winrun.exe --detach --log
  C:\nulldc3-work\run\<step>.log --rc C:\nulldc3-work\run\<step>.rc --timeout <s> -- <command>` (4.3). Before `winrun`
  exists, only its bootstrap build and configures run, in the foreground. The log is copied back through the scrubber.
- **Processes.** Every program built in the round runs inside `winrun` (4.3): a job object with kill-on-close, a private
  desktop, a timeout. Kill only by handle or recorded PID; tree kill of `npm.cmd` by PID; never `taskkill /IM`. A final
  sweep may only end processes whose executable lies under `C:\nulldc3-work`. A freshly linked `.exe` held by Defender
  is retried, never excluded.
- **Cleaning up.** A package deletes `b\`, `src\`, `tmp\` and `run\` before it returns; `data\`, `tools\` and
  `npm-cache\` stay for the round; `try\` holds the owner's build (PL-D72): W64a makes it, W64b replaces it, PLG removes
  it. PLG empties all but `data\` at the round's end and leaves a `README.txt` that says what the folder is.

### 4.2 The Mac

- **What is there.** Apple M2 (arm64, 8 cores, 24 GB), macOS 26.4, Xcode 26.6 (its `python3` is 3.9); Homebrew under
  `/opt/homebrew` with the tools of 4.4; reached over passwordless ssh. The work folder is `~/nulldc3-work` (MACa makes
  it), laid out as on Windows: `{in, src, b, try, data, tools, tmp, run, npm-cache}`.
- **Environment of every ssh command**, inline: `PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin`,
  `TMPDIR=~/nulldc3-work/tmp`, `CCACHE_DISABLE=1`, `npm_config_cache=~/nulldc3-work/npm-cache` (and the npm switches of
  4.1), `PYTHONDONTWRITEBYTECODE=1`. Before the first compiler or git call of a session: `xcode-select -p` must name a
  developer directory (else stop: the first `/usr/bin/git` would open a dialog).
- **Feeding the tree.** `git bundle` copied with `scp` into `~/nulldc3-work/in/`, cloned into `src/nulldc3` and
  **`src/ui`** (never `dreamcast-devtools`, PL-D72); **the submodule `devtools` of `src/nulldc3` initialised from the UI
  clone** as on the Windows host (4.1), so that `devtools/LICENSE` is there for the bundle that embeds the UI (its
  configure reads it, PL-D80; the zip's `LICENSE-devtools` comes from the CI script's `<ui>`, here `src/ui`, PL-D85);
  the vector submodules from GitHub; the UI clone at the pin with `npm ci` there. No remote is added
  to a local repository's configuration (a submodule's URL in the clone's local config is not a remote). **Nothing is
  committed in a clone on the Mac**: every change is made in the package's worktree here, committed here and sent again
  as a bundle; `git rev-parse HEAD` on the Mac equals the worktree's commit before every gate step.
- **Data.** `~/nulldc3-work/data/` as on Windows (the ELFs; the subsets of caique-rtl and of wren7-rtl's build outputs;
  shrike4 is not needed: `sh4.fpu` is x64-only).
- **Running.** `ninja -j6`, `ctest -j4`, at most four emulators; the gate's builds with `-DNULLDC_DEVTOOLS_DIST=OFF`;
  the bundle's inner executable (`nulldc3.app/Contents/MacOS/nulldc3`) by path, **always through `tests/macos/macrun.py`
  under the sandbox profile** (4.3); never `open`, never LaunchServices. The contract runner runs on the Mac against
  `127.0.0.1`.
- **Long steps.** `macrun.py --detach --log ~/nulldc3-work/run/<step>.log --rc ~/nulldc3-work/run/<step>.rc --timeout
  <s> -- <command>`: a new session (`setsid`), under `caffeinate -i`, its PID in `run/<step>.pid`; never bounded by the
  ssh connection (a non-pty ssh command that is killed leaves its remote process running); polled, and the log copied
  back through the scrubber. The step runs under the profile, and every `macrun.py` the step starts (a `tests/macos`
  target, `emurun.py`'s Darwin branch) runs its command under that same inherited profile (4.3). `macrun.py` is MACa's
  first deliverable on the Mac; before it is in the clone, nothing but configures runs there.
- **Processes and files.** No `sudo`, no `defaults write`, no `launchctl`, nothing written outside `~/nulldc3-work`.
  Kill only PIDs the run recorded (or the process group `macrun.py` made); a final sweep only of processes whose
  executable lies under `~/nulldc3-work`. A package lists `~/Library/Logs/DiagnosticReports` (read-only, names only: `ls
  -1`) before and after its gate and reports every new entry, whatever program it names (there must be none of ours:
  PL-D45).
- **Cleaning up** as on Windows (`try/` too).

### 4.3 Rules for both

- **The host-detail rule.** The remote systems' details — the ssh alias of the Mac, the Mac's and the Windows host's
  user names, their host names and their home paths — are given to a package at run time, in the `paths` of its prompt.
  They are never written into a repository file, a commit message, a PR text, a CI file, a CI log, a report or a
  document under `agents/nulldc3/`: scripts take them from environment variables (`PL_MAC`, `PL_MAC_USER`,
  `PL_MAC_HOME`, `PL_MAC_HOST`, `PL_WIN_USER`, `PL_WIN_HOME`, `PL_WIN_HOST`) and texts say "the Mac" and "the Windows
  host". `C:\nulldc3-work` and `~/nulldc3-work` are the work folders' names and may be written. **This machine's own
  paths** in local tool output (CMake, ninja, ctest, sanitizer and contract logs under `agents/nulldc3/<pkg>/`) and in
  P0's recipe (`agents/nulldc3/m3g/scripts/hashes.py`, copied into `rb/p0/`) stay as in M1 to M3: `agents/` is not
  published. What leaves the machine — committed files, the documents PLG copies into `docs/nulldc3/`, the PR texts, CI
  files and CI logs — is checked with the full patterns of PL-D61, this machine's home included.
- **The Windows host's host name is also this machine's** (WSL2 takes the Windows computer name; this machine's
  `/etc/wsl.conf` sets none). **The gate ELFs print it**: 2ndmix.elf and hello.elf (KallistiOS 2.2.2, built on the
  Windows host) each print a build banner line `<u>@<host>:/opt/toolchains/dc/kos`, and guests built here with
  KallistiOS print it as `<u>@<host>.localdomain:/opt/toolchains/dc/kos`. Every local log that keeps a run's output of
  them carries that line, as it carries this machine's home: a read-only grep of 2026-10-19 found the host name in 3833
  files of earlier packages' directories, all from the banner in its two forms but a few wine prefixes' registries
  (which also hold it in capitals) and third-party code in a `node_modules` (where it is part of a longer word). Neither
  repository's tracked files hold it, in any letter case. The committed guests (`tests/debugger/guest/`) print no banner.
- **The scrubber** (PL-D73). Remote output passes through a scrubber before it reaches any file here: it replaces the
  remote home paths, the remote user names in path form (`/Users/<u>/`, `C:\Users\<u>\`, `C:/Users/<u>/`) and
  `<u>@<host>`, the Mac's alias and the host names (from the variables above; both host names on every system's output,
  since 2ndmix and hello print the Windows host's name on the Mac too) with `~`, "the Mac" and "the Windows host" — a
  host name only as a whole word (no letter or digit directly before or after it) and in any letter case (Windows
  prints its computer name in capitals in places) — **never a bare user name**: it can be the GitHub owner's name, and
  replacing it would rewrite branch and repository names (`<owner>/nulldc3`) in every log (PL-D61). Remote listings are
  made without owner columns (`ls -1`, `dir /b`), so that a bare user name is never printed. JUnit files from ctest
  (they carry the host name) are not copied back.
- **The package-directory scan** (PL-D73). Each package ends by scanning `agents/nulldc3/<pkg>/` with **the remote
  patterns** of PL-D61, given at run time from outside the repositories and outside `agents/nulldc3/`, without regard
  to letter case (`grep -rliF -f` for the files, then their lines; `hygiene.py` is for what is committed and does not
  sort), and sorts every hit. **A KOS banner line** — every host name on it stands in the banner's form,
  `<u>@<host>:/opt/toolchains/dc/kos` or `<u>@<host>.localdomain:/opt/toolchains/dc/kos`, after whatever prefix a
  harness puts before it — is counted and reported as `KOS banner: N lines in M files`. **A host name inside a longer
  word** (a letter or digit directly before or after it, as in third-party code under a UI clone's `node_modules`) is
  counted and reported as `inside a word: N lines`, with the files. Neither is a failure. **The scan is clean when no
  other hit remains**; every other hit, on a host name or any other pattern, is reported with its file and where it
  came from. This machine's home path is not one of the remote patterns (it is in every local log). The full patterns,
  for what leaves the machine, keep the host names and sort nothing: no committed file, document copy, PR text or CI
  log may quote the banner (CI runs no gate ELF: PL-D58).
- **One heavy job at a time** across this machine and the Windows host (one computer); the Mac may build while this
  machine reads logs. `free -m` here and the Windows host's free memory are checked before a heavy step.
- **Every emulator run on the Windows host and the Mac carries P0's keys** (PL-D71): `--portable <dir under the work
  folder>` and the `--config` items `config:Headless=yes`, `audio:backend=none` and `config:Dreamcast.RTC=946684800`,
  plus the run's `Exit.*` keys (P0's recipe: `agents/nulldc3/m3g/scripts/hashes.py`). A DirectSound or CoreAudio path is
  checked by reading, or with a guest that writes no sound, never with audible output.
- **No emulator runs on a system before its headless mode exists and is proven not to open a window.** On the Windows
  host that is W64a's protocol, on the Mac MACa's; before them, nothing but builds and scripts runs there (a CMake
  configure, the generator's `--check`).
  - **Windows** (W64a; PL-D69). `tests/winrun/winrun.cpp` builds `nulldc3-winrun.exe` (also standalone with `cl.exe`
    before the tests project exists). It sets its error mode (`SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
    SEM_NOOPENFILEERRORBOX`, inherited by every descendant); makes a job object (`KILL_ON_JOB_CLOSE`,
    `DIE_ON_UNHANDLED_EXCEPTION`); makes a desktop of its own in the caller's window station (`GetProcessWindowStation`)
    under a unique name (`winrun-<pid>`); starts the child suspended on it (`STARTUPINFO.lpDesktop`), assigns it to the
    job and resumes it — if `CreateDesktopW` or the assignment fails it exits 126 without starting the child. The child
    shares `winrun`'s console, or gets `CREATE_NO_WINDOW` when `winrun` has none; with `--own-console` it gets a pseudo
    console of its own (`CreatePseudoConsole`), whose output `winrun` copies to its own, never a new console window.
    While the child runs `winrun` samples `EnumDesktopWindows` on its desktop every 100 ms, and once at the end; it
    records each top-level window's class, visibility and owning process and prints them on stderr. It exits with 126
    for a failed set-up, 125 when any visible window or any window of a class the emulator registers was seen, 124 on a
    timeout (`--timeout <s>`; the job is ended), and otherwise with the child's code. `--detach --log <f> --rc <f>`
    starts a worker copy of `winrun` with `DETACHED_PROCESS` and returns at once with its PID; the worker runs the
    command as above with the log as its output and writes the exit code into the rc file. `--probe` makes `winrun` a
    probe: it prints its thread's desktop name (`GetUserObjectInformation`, `UOI_NAME`), exits 3 without a window when
    that name is not `winrun-<n>`, and otherwise makes a visible window and waits. **The first program `winrun` starts
    on the Windows host is the probe** (`winrun --timeout 10 -- winrun --probe`): it must print the private desktop's
    name and end with 125. **The first emulator run is `nulldc3 --version` through `winrun`; the first emulation run is
    the headless guest through `winrun` with `NULLDC_TEST_UI_REPORT=1`, which must report `windows=0` and end with exit
    0 and its exit line.** **Every program built in the round runs inside `winrun`** — an emulator, a C++ suite, ctest
    itself (`winrun --timeout <s> -- ctest --test-dir ...`), a harness's Python, node: its error mode, its job and its
    private desktop are inherited by every descendant; the inner `winrun` calls of `emurun.py` stay. **What the private
    desktop hides and what it does not**: a window or message box the child makes itself is invisible on that desktop.
    Crash reporting, the just-in-time debugger, system error pop-ups and the firewall show on the owner's desktop
    whatever desktop the child is on; only `winrun`'s error mode, its job flag and the emulator's own policy keep them
    away, and **no mutation or test on the Windows host may turn off `winrun`'s error mode or job flags** (PL-D70).
  - **Mac** (MACa; PL-D69). `tests/macos/macrun.py` (Python 3.9, standard library) runs a command under `sandbox-exec -f
    tests/macos/no-windowserver.sb` (a profile that allows everything but the Mach services of the window server, the
    Dock and LaunchServices) in its own process group, with a timeout (exit 124, `SIGKILL` to the group), `TMPDIR`
    inside the work directory and its PID written to `run/<name>.pid`. **It applies the profile only when the calling
    process is not sandboxed already**: it asks the kernel, `sandbox_check(getpid(), NULL, 0)` through `ctypes` from
    libSystem (1 means sandboxed; the three arguments are the function's fixed ones, so the call is safe on arm64). When
    the caller is sandboxed it runs the command directly — the inherited profile binds every descendant and cannot be
    loosened, and macOS refuses a second one (`sandbox-exec: sandbox_apply: Operation not permitted`, the nesting
    failure known from Homebrew, Nix and Bazel) — and prints `macrun: inside the profile already` on stderr; the process
    group, the timeout, `TMPDIR` and the PID file are the same either way. That is the normal shape of a gate: a long
    step (a build, the tests project's ctest, `macos.sh`) runs under `macrun.py --detach`, and the `tests/macos` targets
    and `emurun.py`'s Darwin branch call `macrun.py` again inside it. It refuses to run when the kernel cannot be asked,
    and when `sandbox-exec` is missing and `CI` is not set. **Every emulator run an agent makes on the Mac goes through
    it**, `--version`, `--help` and the refused command line included; so do `emurun.py`'s Darwin branch (MACb) and
    every `tests/macos` target that starts the emulator. The headless path is read by the verifier before the first run
    (in headless mode `main` reaches `nulldc_term` without `NSApplication`, `NSWindow`, `NSAlert` or LaunchServices).
    **The first run is `macos.sandbox.canary`** (a small program of `tests/macos` that tries to reach the window server
    without AppKit and prints the kernel's view, from `sandbox_check`: whether it is sandboxed, and whether the window
    server's `mach-lookup` is denied; under the profile it must fail to reach the window server and say sandboxed and
    denied, so the proof does not depend on whether a non-GUI ssh session could reach the window server at all; its
    result outside the profile is reported, not gated, since it makes no window and no Dock icon). **The canary runs in
    both shapes in every Mac gate**: from a ctest that is not sandboxed (`ctest -R macos.sandbox.canary` in the
    foreground: `macrun.py` applies the profile) and from a ctest started under `macrun.py --detach` (`macrun.py` finds
    its caller sandboxed); both must fail to reach the window server. **Then `--version`, `--help` and a refused command
    line; the first emulation run is the headless guest with `NULLDC_TEST_UI_REPORT=1`, which must report `app=0
    windows=0` and end with exit 0 and its exit line.** If `sandbox-exec` is not on the Mac, MACa stops before its first
    run and reports (the integrator asks the owner). `macos.headless.nowindow` then holds it in every gate. **No
    mutation or test reaches AppKit or leaves a crash report by design** (PL-D70).
- **The crash-report places** (Windows: `%LOCALAPPDATA%\CrashDumps`, the WER `ReportQueue` and `ReportArchive`; the Mac:
  `~/Library/Logs/DiagnosticReports`) are listed (names only) before and after every gate; every new entry is reported,
  whatever program it names, with the program it belongs to.
- **What the gate counts on a remote system.** "Every target that runs green twice; the skipped listed with their
  reasons and the number of each" (PL-D5): the Linux rule "none skipped" cannot hold where the owner's decision skips
  the Linux-only harness.
- **Hashes across hosts** are reported, never gated: a line or a hash is good for one build on one host (design 5.6). A
  difference between Windows x86_64 and Linux x86_64 on the same engine and workload is named in the report with its
  first differing part; the integrator decides whether it is a defect.
- **Expected durations.** Each remote brief states the expected machine time of its gate; a step that runs far past it
  is looked at (its log, its process list), never left running unobserved.

### 4.4 What the owner does first (the Mac)

Before MACa (and, if wanted, before RB, whose optional configure uses CMake on the Mac), in a Terminal on the Mac:

```
xcode-select -p
#   must print the Xcode developer directory (/Applications/Xcode.app/Contents/Developer);
#   if it prints something else or an error:
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
sudo xcodebuild -runFirstLaunch
brew install cmake ninja node
# optional, so that the ARM disassembler's oracle tests run on the Mac instead of being skipped:
brew install arm-none-eabi-binutils
# check, and tell the integrator if this prints nothing:
ls -l /usr/bin/sandbox-exec
```

Nothing else is installed: Xcode's `python3` (3.9) is the Mac's Python, and every Python file of the round keeps to
Python 3.9 and the standard library (PL-D74); Xcode's `git` and `codesign` are used; no ccache. The Windows host needs
nothing from the owner (Python, Node, Git and both Visual Studios are there; a Node older than 22.13 is replaced by a
zip in the work folder, PL-D41).

---

## 5. Versions

### 5.1 The string

One rule for both repositories (PL-D24). The base is `3.0.0-pre`: in the emulator the one line of
`nulldc3/version-base.txt`, in the UI `package.json`'s `version`.

- **`<h>`** is the first 10 characters of `git rev-parse HEAD`, always exactly 10 (never describe's abbreviation, which
  is only a minimum length: a full and a shallow clone could disagree).
- **The tag** is found only by `git describe --tags --long --match v[0-9]*` (run without a shell, so the pattern is one
  argument), and its output is parsed from the right as `v<T>-<N>-g<hash>`: `<T>` is the tag without its `v`, `<N>` the
  commits since it. An output that does not parse is "no tag reachable".
- **"Dirty"** means a tracked change: `git status --porcelain --untracked-files=no` is not empty (staged or unstaged
  changes of tracked files, a submodule at another commit than its gitlink or with tracked changes); untracked and
  ignored files never count.
- **"A checkout"** means that `git -C <root> rev-parse --show-prefix` succeeds and prints an empty line: the root is the
  top of a work tree (a clone, a worktree, a submodule). A copy of the tree inside another repository prints its prefix
  and is no checkout; no path comparison is needed.
- **How git is run**: by an argument list, never through a shell (`execFileSync('git', ['-C', root, ...])` in the UI,
  `execute_process` in CMake), with `GIT_OPTIONAL_LOCKS=0`, so that a build never holds `index.lock` against a
  concurrent commit.

| State | `git describe --long` | Version |
| --- | --- | --- |
| on the tag `v3.0.0-pre`, clean | `v3.0.0-pre-0-g…` | `3.0.0-pre` |
| N commits after it (N ≥ 1), clean | `v3.0.0-pre-N-g…` | `3.0.0-pre+N.g<h>` |
| on the tag, dirty | `v3.0.0-pre-0-g…` | `3.0.0-pre+dirty` |
| N after, dirty | `v3.0.0-pre-N-g…` | `3.0.0-pre+N.g<h>.dirty` |
| a checkout with no tag reachable (none yet; a shallow clone), or an output that does not parse | fails | `<base>+g<h>` (`.dirty` appended when dirty) |
| no git, or not a checkout (a tarball, a copy) | — | `<base>+nogit` |

When a tag is reachable, the version starts with the tag's `<T>`; the base is then used only by the tag jobs' check that
tag and base agree (PL-D26). When none is, it starts with the base. Every character is in the embed set
`[A-Za-z0-9_.+~:/ -]`; the string is valid SemVer, and every later build sorts with `3.0.0-pre` (build metadata). Until
the round's tag exists every build says `3.0.0-pre+g<h>` (no reachable tag); the cases on a tag are tested in scratch
repositories only (VCu, VTe), never with a tag in the shared repositories.

**Derived forms.** `X.Y.Z` are the three numbers at the start of the version string itself (the tag's when one is
reachable, else the base's), so that every form says the same release; `N` is describe's count, 0 on a tag and without
one, capped at 65535.

| Consumer | Form | On the tag | N = 5 after |
| --- | --- | --- | --- |
| Windows `FILEVERSION` / `PRODUCTVERSION` (Explorer's "File version") | `X,Y,Z,N` | `3,0,0,0` | `3,0,0,5` |
| Windows `StringFileInfo` `FileVersion` / `ProductVersion` | the version | `3.0.0-pre` | `3.0.0-pre+5.g<h>` |
| macOS `CFBundleShortVersionString` | `X.Y.Z` | `3.0.0` | `3.0.0` |
| macOS `CFBundleVersion` | `X.Y.Z.N` (four parts, PL-D29) | `3.0.0.0` | `3.0.0.5` |
| macOS `NullDCVersion` (a key of the project's own) and the About panel's version | the version | `3.0.0-pre` | `3.0.0-pre+5.g<h>` |
| release file names | the version | `nulldc3-3.0.0-pre-linux-x86_64.AppImage` | `nulldc3-3.0.0-pre+5.g<h>-linux-x86_64.AppImage` (PR artifacts) |
| the UI's `bundle-info.json` `version` | the UI's version | `3.0.0-pre` | `3.0.0-pre+5.g<h>` |
| the UI's `gitRev` | the full hash, `-dirty` by the rule above | | |

How each form is read back in a check: Windows' numbers from `FileMajorPart`, `FileMinorPart`, `FileBuildPart` and
`FilePrivatePart` of PowerShell's `(Get-Item nulldc3.exe).VersionInfo` (or `FileVersionRaw`), the strings from its
`FileVersion` and `ProductVersion`, `CompanyName` from the same object; macOS's from `plutil -extract`.

### 5.2 Where it is made

- **Emulator (VTe): at build time.** A target that runs at every build executes `nulldc3/cmake/version.cmake` (script
  mode, about 10 ms) and rewrites, only when their text changes, `<build>/generated/nulldc3_version.h` (`NULLDC_VERSION`
  the string; `NULLDC_VERSION_NUMBERS` e.g. `3,0,0,5`; `NULLDC_VERSION_DOTTED` e.g. `3.0.0.5`; `NULLDC_VERSION_SHORT`
  `3.0.0`; `NULLDC_GIT_HASH` the full hash or `""`; `NULLDC_GIT_DIRTY` 0 or 1; plain `#define`s, no `#include`, no
  `#pragma`, so that `rc.exe` reads it), `<build>/nulldc3-version.txt` (the version, one line) and
  `<build>/nulldc3-version.cmake` (the same as CMake variables). **One translation unit reads the header** (PL-D75):
  `libswirl/nulldc_version.h` (no `#include`) declares `nulldc_version()`, `nulldc_title()`, `nulldc_git_hash()` and
  `nulldc_git_dirty()`; `libswirl/version.cpp`, which includes only that header and the generated one, defines all but
  `nulldc_title()`; `libswirl/version_title.cpp` (it includes `types.h` for `VER_EMUNAME`, never the generated header)
  defines `nulldc_title()` from `nulldc_version()`; `types.h` includes `nulldc_version.h`, and `VER_SHORTNAME` /
  `VER_FULLNAME` expand to `nulldc_title()`; the generated directory is on `version.cpp`'s include path alone (and on
  the `.rc`'s). A new commit, tag or dirty change is seen at the next build without a reconfigure and recompiles
  `version.cpp` alone; every other compile command of the build is as before. Nothing goes into the source tree
  (`libswirl/version.h` and its template go; a stale ignored `libswirl/version.h` in an old checkout is harmless because
  nothing includes that name any more).
- **The tests projects (VTe).** The tests project runs the same `version.cmake` into its own `<build>/generated` through
  a target that the library holding `cfg/cl.cpp` depends on, and compiles `version.cpp` and `version_title.cpp` into it
  (it has the `TARGET_*` macros through `config.cmake`); the standalone `tests/debugger/net` project (a project of its
  own, added before `common/libswirl.cmake`: no `TARGET_*`, no `types.h` in its sources) runs its own step under a
  target name of its own (it is also a subdirectory of the tests project) and compiles `version.cpp` alone into the
  `dbg_webui` object library, which every consumer of its objects links; `dbg_webui.cpp` and `test_webui.cpp` include
  `nulldc_version.h`, and `test_webui.cpp` compares with `nulldc_version()`.
- **Windows (W64a)**: the `.rc` includes `nulldc3_version.h`. **macOS (MACa)**: a custom command with a stamp file reads
  `nulldc3-version.cmake`, writes `CFBundleShortVersionString`, `CFBundleVersion` and `NullDCVersion` into the bundle's
  `Info.plist` (`plutil -replace`), then signs ad hoc and verifies; it depends on the linked executable,
  `nulldc3-version.cmake`, `Info.plist.in`, the bundle's `Contents/Info.plist` (which CMake rewrites from the template
  at generate time) and every bundled resource, and belongs to an `ALL` target that depends on `nulldc3`, so that it is
  the last step of every build that changes the bundle, after a bare reconfigure too (PL-D42). Nothing touches the
  bundle after the signature. **AppImage and zips (CIe)**: names from `nulldc3-version.txt`.
- **UI (VCu)**: `vite.config.ts` calls `scripts/version.ts` at every build; `npm run print-version` prints it.

### 5.3 Every reader

Emulator (VTe unless named; every one through the functions of 5.2): `debugger.describe`'s `emulator.version` (the
version; design 3.1 changes, Appendix D); `dc.BUILD` (`nullDC <version>`); the window title while parked (`nullDC
<version> [paused by debugger]`, `dbg_emu.cpp`, H8-4); the page served without a bundle (`<title>nullDC <version>
debugger</title>`); the window title every second (`VER_SHORTNAME`, now `nullDC <version>`); the Windows initial title
(`VER_FULLNAME`, the same, no build time); the imgui About (Version, Git Hash with `-dirty`; Build Date goes); the
welcome screen (the version whole, no parsing: today it would show `v3.0.0` and `(re)`); `--version` (prints `nullDC
<version>` and a newline on stdout, exit 0; on every system); `--help` (lists `--version`); the man page (lists
`--version`; its header keeps "nullDC 3.0.0" as the product's name, as `README.md`'s title does); Windows `VERSIONINFO`
(W64a); the macOS plist and the About panel (MACa: `orderFrontStandardAboutPanelWithOptions:` with `ApplicationVersion`
the version and `Version` the `CFBundleVersion`, so that a pre-release does not present itself as `3.0.0`); the release
file names and the AppImage's `X-AppImage-Version` (CIe); the HTTP User-Agent stays `nulldc3` (PL-D10). UI (VCu): the
About dialog, the status bar, the `client` text of `debugger.describe`, `bundle-info.json` (`version`, `gitRev`), which
the emulator embeds and reports as `ui`. Nothing version-like is in a state hash, a sound line, `emu.cfg` or a save
state.

---

## 6. CI

### 6.1 Shape

GitHub-hosted runners only (PL-D55), on public repositories. Each repository has one workflow file,
`.github/workflows/ci.yml`, with three triggers: `pull_request` (any base), `push` of tags matching `v*`, and
`workflow_dispatch` with the input `tag` (string, required: the tag a dispatch checks against) and, in the emulator's,
`require_ui_tag` (boolean, default false). There is no `dry_run` input: **a dispatch never creates anything** (PL-D77).
No trigger on a push to a branch (PL-D56). `permissions: contents: read` at the top; only the job `release` has
`contents: write`. `concurrency: ci-${{ github.ref }}`, cancelled in progress for pull requests only. Every action
pinned by its full commit SHA with its version in a comment (PL-D60). Checkouts with `fetch-depth: 0` (the version needs
the history to the tag; the emulator's submodule `devtools/` is then cloned whole, with its tags; if a run shows the
submodule's tags missing, the `ui` job may fetch them explicitly from the public repository, and the `require_ui_tag`
dispatch of section 7 proves it). A job's work is a committed script the packages ran on that system before any PR
(PL-D54); the YAML only provisions, calls, and moves artifacts. Every artifact is kept 7 days.

| Event | What runs | What it makes |
| --- | --- | --- |
| pull request | the build and test jobs, which also make the release files; `release-check` (`contents: read`) | artifacts only |
| dispatch (on `main`, or on any ref) | the same; `release-check` against the input `tag` (and `require_ui_tag`) | artifacts only; the would-be Release printed |
| tag push `v*` | the same but `release-check`, then `release` (`if: github.event_name == 'push' && github.ref_type == 'tag'`, the tag from `github.ref_name`) | a draft Release with its files, checked, then published |

**The jobs' conditions and needs.** `release-check` has `if: github.event_name != 'push'` (pull requests and
dispatches). `release` has `if: github.event_name == 'push' && github.ref_type == 'tag'` — the event, not the ref alone,
since a dispatch run on a tag ref (`gh workflow run ci.yml --ref v3.0.0-pre`) has `github.ref_type` `tag` too — and
`needs` exactly the jobs that make or check its files: in the UI `check`, `e2e`, `package`; in the emulator `ui`,
`linux-x86_64`, `linux-arm64`, `windows-x86_64`, `macos-arm64`. **`release` never needs `release-check`**: that job is
skipped on a tag push, and GitHub skips every job that needs a skipped one unless its `if:` carries a status function,
which `release`'s condition does not. `ci.workflows` (the emulator's) and VCu's read of the UI's workflow check both
conditions and `release`'s `needs` list.

**The scripts' invocations** (PL-D77). Each release script has explicit invocations, never a mode guessed from the
environment: in the UI `release-files.ts pack` (`package`: the two zips, `SHA256SUMS` and the notes; it never calls
`gh`, whatever the event), `release-files.ts check` (`release-check`) and `release-files.ts publish` (`release`: it
takes the three files and the notes from `package`'s artifact); in the emulator the platform scripts make the files, and
`release.sh check` (`release-check`) and `release.sh publish` (`release`) take them from the platform jobs' artifacts.

- **`check`** (it creates nothing): the release files are named with the version; without a tag input (a pull request)
  nothing more is asked of the version; with one (a dispatch) the base equals the tag without its `v`; in the emulator
  the four configure lines of the platform jobs name the `ui` artifact's version (PL-D76), and the UI's version is the
  base exactly or the base followed by `+` — the base exactly when `require_ui_tag` is set; it prints the `gh` commands
  `publish` would run, with the tag's version substituted, and the notes.
- **`publish`** refuses unless `GITHUB_EVENT_NAME` is `push` and `GITHUB_REF_TYPE` is `tag`; then the version equals the
  tag without its `v` and equals the base; in the emulator the UI's version equals it too and so does every configure
  line; `SHA256SUMS` is verified against the files; then `gh release create <tag> --verify-tag --draft --prerelease ...`
  with the files, the draft's asset list checked against the files and `SHA256SUMS`, then `gh release edit <tag>
  --draft=false`. On a re-run with the draft present it uploads with `--clobber` and publishes; a published Release
  already present is refused.

### 6.2 The UI's workflow (VCu)

| Job | Runner | Steps | Artifacts | Time |
| --- | --- | --- | --- | --- |
| `check` | `ubuntu-24.04` | checkout (depth 0), Node 22 with the npm cache, `npm ci`, `npm run print-version` (into the job summary), `npm run check` | — | ~10 min (measured: 9 min 48 s) |
| `e2e` | `ubuntu-24.04` | as today: `npm ci`, Playwright's Chromium with its dependencies, `npm run test:e2e` | the Playwright report | ~14 min (measured: 13 min 34 s) |
| `package` | `ubuntu-24.04` | `npm ci`, `build:native`, `build:wasm`, `release-files.ts pack` (the two zips, each carrying the UI's `LICENSE`, `SHA256SUMS`, the notes; no `gh`) | the three files and the notes | ~3 min |
| `release-check` (`if: github.event_name != 'push'`) | `ubuntu-24.04` | needs `package`; `release-files.ts check` | — | ~1 min |
| `release` (`if: github.event_name == 'push' && github.ref_type == 'tag'`) | `ubuntu-24.04` | needs exactly `check`, `e2e`, `package`; `release-files.ts publish` on `package`'s artifact: `--title "Dreamcast DevTools <version>"`, the notes | the Release | ~1 min |

### 6.3 The emulator's workflow (CIe)

| Job | Runner | Steps (each a script of `nulldc3/ci/`) | Tests | Artifacts | Time (estimated) |
| --- | --- | --- | --- | --- | --- |
| `ui` | `ubuntu-24.04` | checkout with submodules (depth 0), Node 22, `npm ci` and `npm run build:native` in `devtools/` | — | `dist-native` | 2-3 min |
| `linux-x86_64` | `ubuntu-24.04` | needs `ui`; `linux.sh deps` (the X11, GL, EGL, ALSA, Pulse, udev, evdev, curl dev packages, Ninja, `ccache`, the SH and ARM binutils); ccache restored (`CCACHE_DIR` under `$RUNNER_TEMP`); `dist-native` downloaded into `$RUNNER_TEMP/dist-native`; `linux.sh build`: the release emulator (`Release`, `-DNULLDC_DEVTOOLS_DIST=$RUNNER_TEMP/dist-native`, `-DNULLDC_PREFIX_MAP=ON`) and the `-DHAS_TA_LLE=OFF` emulator, its configure line saved; `linux.sh test ci`: the tests project with `-DNULLDC_EMULATOR`, `-DNULLDC_EMULATOR_TA_HLE`, the submodules, the UI checkout `<ui>` (on a runner the submodule `devtools`, PL-D85) with `npm ci`, `NULLDC_TESTS_REQUIRE_DATA=OFF`; `ctest -j4 --output-junit`; the skipped tests with reasons into the job summary; `appimage.sh` (the licences of PL-D80, `LICENSE-devtools` from `<ui>/LICENSE`) with its smoke run | about 205 of the tree's targets (the rest skip for data: PL-D58) | the AppImage and its configure line | 20-25 min cold, ~12 warm |
| `linux-arm64` | `ubuntu-24.04-arm` | the same scripts | the same, the x64-only suites skipped | the AppImage and its configure line | similar |
| `windows-x86_64` | `windows-2025` (or the current image with the newest MSVC: CIe checks the runner-images list) | needs `ui`; `git config --global core.autocrlf false`; checkout; Python 3.12 and Node 22 by their setup actions; `dist-native` downloaded; `windows.ps1` with `shell: powershell` (Windows PowerShell 5.1) and `<ui>` = `devtools`: vswhere and `vcvars64`, the release emulator with `-DNULLDC_DEVTOOLS_DIST`, its configure line saved, the tests project, ctest inside `winrun`, the zip (`tar.exe -a -c -f`; `LICENSE-devtools` from `<ui>/LICENSE`), its smoke run through `winrun` | the portable part (W64b's list) | the zip and its configure line | 30-40 min |
| `macos-arm64` | `macos-15` (or the current arm64 image) | needs `ui`; `brew install ninja ccache`; ccache restored; `dist-native` downloaded; `macos.sh` with `<ui>` = `devtools`: the bundle (`Release`, deployment target 11.0, `-DNULLDC_DEVTOOLS_DIST`), its configure line saved, `codesign --verify --strict --deep`, the tests project, ctest, the zip with `ditto` (`LICENSE-devtools` from `<ui>/LICENSE`), its smoke run | MACb's list | the zip and its configure line | 25-35 min |
| `release-check` (`if: github.event_name != 'push'`) | `ubuntu-24.04` | needs `ui` and the four platform jobs; `release.sh check` | — | the would-be notes | 1-2 min |
| `release` (`if: github.event_name == 'push' && github.ref_type == 'tag'`) | `ubuntu-24.04` | needs exactly `ui`, `linux-x86_64`, `linux-arm64`, `windows-x86_64`, `macos-arm64` (never `release-check`); `release.sh publish`: the four files, the version against the tag and `nulldc3/version-base.txt`, the `ui` version and the four configure lines against it, `SHA256SUMS`, the draft, its check, then published; `--title "nullDC <version>"`, notes from `nulldc3/ci/release-notes.md` | — | the Release | 2-3 min |

**The smoke run** of each release file, on its own runner (PL-D76), with P0's keys: the AppImage
(`APPIMAGE_EXTRACT_AND_RUN=1 ./<file> --version` prints `nullDC <version>`; a headless guest run from a scratch
directory leaves its portable files beside the AppImage); the Windows zip unpacked (its `nulldc3.exe --version` and a
headless guest run through `winrun`); the macOS zip unpacked with `ditto -x -k` (`codesign --verify --strict --deep`,
the inner executable's `--version` and a headless guest run); in each, a headless run with the debugger enabled whose
`debugger.describe` (the tree's Python client) names `ui` as the `ui` artifact's version.

ccache on Linux and macOS through `actions/cache` (key: system, architecture, compiler, a hash of the CMake files;
restore by prefix), installed by the job (`linux.sh deps`, `brew install`), `CCACHE_DIR` under `$RUNNER_TEMP`; none on
Windows (PL-D64). The npm cache keyed on `devtools/package-lock.json`. The emulator's workflow does not run the UI's
suites (the UI's own CI does).

### 6.4 The release files and the Release

| Repository | Files on a tag | Release |
| --- | --- | --- |
| UI | `dreamcast-devtools-<v>-dist-native.zip` (the bundle an emulator build embeds, `-DNULLDC_DEVTOOLS_DIST`), `dreamcast-devtools-<v>-dist-wasm.zip`, each with the UI's `LICENSE`; `SHA256SUMS` | pre-release "Dreamcast DevTools <v>" |
| Emulator | `nulldc3-<v>-linux-x86_64.AppImage`, `nulldc3-<v>-linux-arm64.AppImage` (the licences under `usr/share/doc/nulldc3/`), `nulldc3-<v>-windows-x86_64.zip` (folder `nulldc3-<v>-windows-x86_64/`: `nulldc3.exe`, the licences, `README.txt`), `nulldc3-<v>-macos-arm64.zip` (folder `nulldc3-<v>-macos-arm64/`: `nulldc3.app` with the licences in its `Resources`, the licences, `README.txt`), `SHA256SUMS` | pre-release "nullDC <v>" |

**The licences of every emulator release file** (PL-D80): `LICENSE`, `LICENSE-GPL`, `LICENSE-LGPL`, the shell's BSD
licence `nulldc3/LICENSE` as `LICENSE-shell`, and the embedded UI's MIT licence as `LICENSE-devtools`, copied by the
scripts from `<ui>/LICENSE`, the UI checkout at the tree's gitlink (PL-D85: on a runner the submodule `devtools`, on the
Windows host and the Mac the UI clone, here `agents/nulldc3/wt/ui-pin`); the bundle's `Resources` take it from
`devtools/LICENSE` at configure (every release build embeds the UI, PL-D76; a macOS bundle built without a UI carries
the other four, and its configure never needs the submodule).

A tag with a pre-release part (`-pre`) makes a published pre-release; a tag without one would make a release (PL-D59).
The notes come from `nulldc3/ci/release-notes.md` (and the UI's template): what it is; the files; portable by default
(the folder that holds the program; for the AppImage the folder that holds the AppImage); the Linux floor (glibc 2.39, a
4 KiB-page kernel on arm64; FUSE 3 (`fusermount3`) for an AppImage, or `--appimage-extract-and-run`); Windows
SmartScreen on an unsigned download; macOS Gatekeeper ("Open Anyway" in System Settings, or `xattr -dr
com.apple.quarantine` on the unpacked folder) and App Translocation (start it from the unpacked folder after clearing
the quarantine); the UI's Release of the same version; the licences. No source tarball (PL-D63).

### 6.5 Checking CI before the PRs, and what only GitHub shows

Before a PR exists: every script of 6.2 and 6.3 has run on its system in a package gate (VCu, CIe, PLG), in each of its
invocations; the workflow files pass `actionlint` (a release binary pinned by version and SHA-256 in
`agents/nulldc3/tools/`; this machine has no `act`, and its docker client has no access to a daemon); `ci.workflows`
(CIe's `check_workflows.py`, which reads the emulator's workflow as text; VCu's read does the same for the UI's) checks
them against 6.1's rules: pinned SHAs; `contents: read` at the top and on `release-check`, `contents: write` only on
`release`; `release`'s `if:` is exactly `github.event_name == 'push' && github.ref_type == 'tag'` and its `needs` the
list of 6.1, without `release-check`; `release-check`'s `if:` is `github.event_name != 'push'`; each job's invocation of
the release script (`publish` in `release` alone); the triggers and inputs; no secrets beyond the token; no `env` dumps;
no owner-specific text; and a stub test shows that no invocation but `publish` calls `gh` to create anything and that
`publish` creates nothing unless `GITHUB_EVENT_NAME` is `push` and `GITHUB_REF_TYPE` is `tag`. **Only GitHub shows**:
the runner images and their tools, the `ubuntu-24.04-arm` native build (LA64's first native run), `actions/checkout`'s
submodule and tag handling, the artifact passing between jobs, the token's permissions, the Release. **The first real
run is the round's PR itself** (the UI's, then the emulator's), `release-check` included, so the release jobs' plumbing
(the artifacts, their paths, the scripts' invocation, `SHA256SUMS`) is first run there; a failure goes back as a fix
round to the package that owns the file, with commits on the PR's branch. Then the dispatch checks on `main` after both
merges (only a workflow on the default branch can be dispatched), the emulator's second dispatch check with
`require_ui_tag` after the UI's tag, and the `release` jobs for real on the tags (section 7).

### 6.6 Host details in CI

The owner's words (1.3 item 6): nothing in CI files or CI logs may name a host, a user, a home path or a LAN address.
The owner's machines are never runners (PL-D55). Workflows and scripts name no host, user, home path or LAN address;
they use `$GITHUB_WORKSPACE`, `$RUNNER_TEMP`; no step prints the environment. GitHub-hosted logs always carry the
runner's own home and work paths, its user `runner` and, in "Set up job", the runner machine's name; they cannot be
removed without hiding compiler output. Work proceeds on PL-D61's reading (they are GitHub's, not the owner's, and
allowed) until the owner answers PL-Q3. Release builds use `NULLDC_PREFIX_MAP=ON` (GCC and Clang
`-ffile-prefix-map=<source>=.`; MSVC `/PDBALTPATH:%_PDB%`); an MSVC `__FILE__` keeps the runner's path. `ci.hygiene`
checks every tracked file of the emulator for the generic patterns (home directories of Linux, macOS and Windows, WSL's
network and mount paths, the private IPv4 ranges, `.local` host names) against the allow list
`nulldc3/ci/hygiene-allow.txt`; CIe's and PLG's gates run the same script on the UI's tracked files with `--allow
nulldc3/ci/hygiene-allow-ui.txt` (the UI's own hygiene checks name those patterns in its scripts and tests). The
owner-specific patterns (PL-D61) live outside the repositories: the full patterns (the remote systems' user names in
path form and `<u>@<host>`, the Mac's alias, the host names, and this machine's home in path form; not the GitHub owner
name, which is public) are checked, without regard to letter case, by PLG, the verifiers and the integrator on
everything that leaves the machine, the logs of the first runs included. The host names stay in them although the gate
ELFs' KOS banner names the Windows host (4.3): CI runs no gate ELF (PL-D58), and no committed file, PR text or CI log
may quote the banner.

---

## 7. The tags

Items 2 to 11 are the integrator's, in the orchestrating session, outside the package workflow, step by step with the
owner (PL-D84). Section 9's "push nothing; never tag a shared repository" binds the packages, not these steps.

1. **PLG passes** (items 1 to 9 of 2.14, the two PR texts drafted under `agents/nulldc3/plg/`).
2. The integrator pushes the UI's `skmp/nulldc3` to `skmp/dreamcast-devtools` and opens the UI PR into `main` (its CI:
   `check`, `e2e`, `package`, `release-check`). The integrator reads the run's logs (failures, and host details with the
   full patterns of PL-D61); a failure goes back as a fix round to VCu's files, verified as a package's fix round is.
   **The owner merges the UI PR.**
3. The integrator fetches the UI's `main`; checks that its tree equals the UI PR's final head (VCu's verified head plus
   its verified fix-round commits; `git diff` empty); moves the emulator's submodule to the UI's `main` commit in a
   commit of its own, and `agents/nulldc3/wt/ui-pin` to the same commit (the CI scripts' `<ui>` here must stay at the
   gitlink, PL-D85); reruns the targets that read the UI (`devtools.*`, `aica.dsp.wasm.devtools`,
   `debugger.disasm.dsp_playground`, `debugger.regs.panels`, `debugger.net.interop_ws`) and one contract gate with an
   emulator that carries the bundle.
4. The integrator pushes the emulator's `skmp/nulldc3` and opens the emulator PR into `main` (its CI: `ui`, the four
   platform jobs, `release-check`). Logs read; failures as fix rounds; the PR's artifacts tried on the three systems
   (each binary's `--version`, a headless guest run with P0's keys; on the Windows host through `winrun`, on the Mac
   through `macrun.py`). **A UI defect found now** goes as a second UI PR; after the owner merges it, the pin moves
   again in a commit of its own on the emulator PR's branch, item 3's reruns are repeated, and the emulator PR
   continues.
5. **The owner merges the emulator PR.**
6. **With the owner's go** (one go for items 6 to 10): the dispatch checks on `main` of both repositories, input `tag`
   `v3.0.0-pre` (the emulator's with `require_ui_tag` false); both green, the would-be Releases printed as expected.
7. The UI's tag: `git tag -a v3.0.0-pre -m "Dreamcast DevTools 3.0.0-pre" <the UI main commit the emulator pins>` —
   **the very commit of the emulator's gitlink** — pushed; its CI makes the UI's Release. Wait until it is green.
8. A second dispatch check of the emulator on `main`, `tag` `v3.0.0-pre` and `require_ui_tag` true: the `ui` job now
   builds the tagged UI, and `release.sh check` checks that its version is `3.0.0-pre` exactly. This is the first run
   that shows `actions/checkout` bringing the submodule's tag. The emulator's tag waits until it is green.
9. The emulator's tag: `git tag -a v3.0.0-pre -m "nullDC 3.0.0-pre" <the emulator main commit>`, pushed; its CI makes
   the four files and the Release; `release.sh publish` refuses unless the emulator's version, the embedded UI's and the
   four configure lines all say `3.0.0-pre`.
10. **What the first tag runs must produce, and the checks after them.** UI: a pre-release "Dreamcast DevTools
    3.0.0-pre" with the two zips and `SHA256SUMS`, `bundle-info.json` saying `3.0.0-pre`. Emulator: a pre-release
    "nullDC 3.0.0-pre" with the two AppImages, the two zips and `SHA256SUMS`. The integrator downloads them (`gh release
    download`; a download by `gh` or `curl` carries no quarantine attribute), checks the sums, and runs on each system
    (headless with P0's keys; Windows through `winrun`, the Mac through `macrun.py`): `--version` prints `nullDC
    3.0.0-pre`; a guest run ends with its exit line; `debugger.describe` says `emulator.version` `3.0.0-pre` and `ui`
    `3.0.0-pre`; Windows: the numeric file version `3.0.0.0` (Explorer's "File version"), the `FileVersion` and
    `ProductVersion` strings `3.0.0-pre`, `CompanyName` "nullDC Team"; macOS: `CFBundleShortVersionString` `3.0.0`,
    `CFBundleVersion` `3.0.0.0`, `NullDCVersion` `3.0.0-pre`, the identifier `org.emudev.nulldc3`, `codesign --verify`
    of the unzipped bundle; each file's licences present. The x86_64 AppImage and the Windows zip are also left for the
    owner to start by hand.
11. **A failed tag run** (PL-D67). First, with the owner's go, the failed jobs of the same run are re-run (`gh run rerun
    <id> --failed`): a flake costs nothing more. Only a defect of the tagged commit goes through a PR. A tag whose
    Release is still a draft or absent may then be deleted on GitHub and made again on the fixed commit, only with the
    owner's go. If the UI's tag has to move, the emulator's pin moves to it in a PR first, and the emulator's tag waits.
    A tag with a published Release is never moved.

---

## 8. Decisions, questions, risks

### 8.1 Decisions of this cut

Each: what is decided, and the alternative not taken.

**The round**

- **PL-D1. This file is the contract until PLG.** Where it and the design or the plan differ, it wins; Appendix D is
  applied at PLG. *Rejected:* editing the design first (it would be edited again by every finding).
- **PL-D2. Eleven packages in this order** (2.0, 2.1, section 3). *Rejected:* the suggested shape's separate UI version
  and UI CI packages (a second pin move and verification for one workflow whose tag job checks that version); one
  Windows and one macOS package (one verifier for the no-dialog policy and the contract gates at once); the AppImage as
  a package of its own (its only consumer is CIe's script, its test a system test of CIe's).
- **PL-D3. One baseline, P0.** RB takes it on its base: the state hashes of design 5.7 (2ndmix 600 and 4000 vblanks,
  hello 300, the guest 90; both engines; 2ndmix 600 in the five builds) and the sound lines of the guest and of 2ndmix
  (both engines, the default and `-DNULLDC_DEBUGGER=OFF` builds), 38 rows and 12 entries as at M3G; it must equal B4.
  Every emulator package's gate compares with it; each verifier takes it again on the base with this machine's settings.
  *Rejected:* a baseline per package (nothing guest-visible moves in the round).
- **PL-D4. Header revision 8 is comment-only and its texts are fixed in 2.3**; RB and VTe apply them in the commits that
  make the old texts false. *Rejected:* an integrator package for four comments (M3's R7 carried declarations; this
  revision has none).
- **PL-D5. On Windows and the Mac a gate is "every target that runs green twice, the skipped listed with reasons and
  numbers"**; on Linux "green twice, none skipped". The emulator-driven harnesses share one run helper,
  `tests/common/emurun.py`, written by W64b. *Rejected:* a separate harness per system; skipping the sound tests on
  Windows and the Mac (they are portable in the same way and hold time neutrality there).
- **PL-D6. The first run on each remote system is a proof** (4.3): a private desktop and a job on Windows, whose probe
  runs before any emulator; a sandbox that denies the window server on the Mac, whose canary runs before any emulator;
  and the emulator's own report line. *Rejected:* trusting the code review alone for the first run on the owner's
  desktop.

**Names and files (RB)**

- **PL-D7. The move is a commit without content changes of moved files**, carrying the path lines of files that do not
  move; the deletions and the renames follow in commits of their own. *Rejected:* one commit (the small files lose
  rename pairing; branches in flight merge badly); a move commit that does not build.
- **PL-D8. Layout**: `nulldc3/apple/` flat (the sources include only same-directory and `libswirl` headers);
  `nulldc3/icon/` for the icon sources and the generator. *Rejected:* keeping `apple/emulator-osx/emulator-osx/` (an
  Xcode convention with no Xcode project); `branding/icon/` at the root (the platform files live under `nulldc3/`).
- **PL-D9. The new names**: `reicast_root_path`, `reicast_core_path`, `reicast_shell_path`, `reicast_SRCS` →
  `nulldc_root_path`, `nulldc_core_path`, `nulldc_shell_path`, `nulldc_SRCS` (the tests project's names already);
  `REICAST_VERSION` → `NULLDC_VERSION`; `REICAST_S/US/SA/USA` → `NULLDC_S/US/SA/USA` (whole words); `reicast_init`,
  `reicast_ui_loop`, `reicast_term` → `nulldc_init`, `nulldc_ui_loop`, `nulldc_term`; `ReicastUI_impl` → `ImguiGUI`;
  `reicastHelpTitle` / `reicastHelpMenuItem` → `helpTitle` / `helpMenuItem`; the Xcode jump-bar label `Reicast OS
  Functions` → `OS functions`; the comments that call the program reicast say nullDC or "the emulator". *Rejected:*
  `NULLDC3_*` (the tree's convention is `NULLDC_*`).
- **PL-D10. The Lua module is `nulldc3`, with no `reicast` alias; the HTTP User-Agent is `nulldc3`.** *Rejected:* an
  alias (no script in the tree, nothing tests it); a version in the User-Agent (it would change at every commit for a
  header nobody reads).
- **PL-D11. Names derived from reicast that do not say it stay**: `rc_serialize`, `rc_unserialize`, `reios`, `reprof`,
  `RZDCY_SRC_DIR`. *Rejected:* renaming component names (churn in 180 places for no reader).
- **PL-D12. The imgui button "Report a game bug" goes** (its target is a dead reicast site; PLAN track 10 drops it with
  the store and social remnants). *Rejected:* pointing it at the issues page (track 10 removes it anyway).
- **PL-D13. History is kept**: `LICENSE` (unchanged; no line is added), `nulldc3/LICENSE` (reicast's shell licence), the
  licence blocks of `libswirl/license/`, credits, provenance notes, the vixl markers, the macOS file headers ("This file
  is part of reicast-osx", "Copyright © 2020 reicast"), `docs/nulldc3/**`, the path strings of the legacy directories in
  `tests/system/system_test.py` and the docstrings of its `dirs.*` cases (the directories those tests prove are not
  read; the two identifiers that hold them become `LEGACY_CONFIGS` and `LEGACY_DATAS`, since an identifier is live
  code), the README's history sentences, the joyconfig man page's AUTHORS (with the nullDC repository added). **New
  metadata say "Copyright (c) 2026 nullDC Team"** (no "All rights reserved": the code is BSD, GPL and LGPL). *Rejected:*
  rewriting copyright notices; "All rights reserved"; keeping `REICAST_CONFIGS` / `REICAST_DATAS` as history (the owner:
  identifiers get new names).
- **PL-D14. Deleted** (history keeps them): the Xcode project, workspace, test target and asset catalog;
  `reicast/imgs/`; the reicast icons (`reicast.ico`, `reicast.png`, `AppIcon.icns`, replaced by IC); `appveyor.yml`,
  `azure-pipelines.yml`, `wercker.yml`; `versioning/`; `docs/CONTRIBUTING.md` and `docs/Release schedule and
  structure.md` (reicast's CLA and release policy; GitHub shows the first on every new issue and PR);
  `issue_template.md` (reicast's form: "Downloaded from build site?"); `gdtool/` (it cannot build: its `..\core\` paths
  are gone) and the `NOT_REICAST` branch; `reicast.entitlements` and the `XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS` lines
  (no hardened runtime, PL-D42); the Android-only `LOG_TAG` blocks in `deps/imgui/imconfig.h` and
  `deps/vixl/platform-vixl.h`; the dead `.gitignore` patterns. `build_app_bundle.sh` stays until MACa replaces it.
  **`reidbg/` stays**: it is the client of a live, built server — `libswirl/rend/soft/refrend_debug.cpp` (TCP port
  8828), registered as the renderer backend `refsw-dbg` in `refsw.cpp` under `FEAT_TA == TA_LLE` (the default build) —
  and `rebrand.md` 1.6 called it dead in error; RB relabels its "Reicast Host" to "nullDC host" (`MainWindow.cs`,
  `gui.stetic`) and leaves its CRLF files as they are; whether the client and the server go together is a backlog item.
  *Rejected:* a short nullDC `CONTRIBUTING.md` (nothing to say yet that the README does not); keeping
  `reicast.entitlements` for `-G Xcode` (Xcode signs ad hoc without it); deleting `reidbg/` (it removes the only client
  of a built server, more than a rebrand).
- **PL-D15. `DEVCAST_DIR` becomes `nulldc_root_path`** in the four test projects. *Rejected:* leaving the older name.
- **PL-D16. Both repositories get `.gitattributes` with `* -text`**: every file is checked out byte for byte as
  committed, whatever `core.autocrlf` says (the generated header, the hashed DSP module files, `dsptest/SHA256SUMS`, the
  sh4bp configure-time patching, the baselines). The emulator keeps its two CRLF files
  (`docs/neil_corlett_aica_notes.txt`, `libswirl/hw/pvr/Renderer_if.cpp`) as they are. *Rejected:* `* text=auto eol=lf`
  (renormalises tracked files); `core.autocrlf=false` in CI only (a user's Windows clone still breaks).
- **PL-D17. `rebrand.names`** (`tests/rebrand/`): `git grep -I -i -n reicast` of the tree, minus a committed allow list
  of history (`tests/rebrand/allowed.txt`: whole paths for `docs/nulldc3/**` and the licence files, rows of path,
  pattern and reason for the rest; PL-D79), must be empty, and every row must still match; skipped without a checkout (a
  failure under `NULLDC_TESTS_REQUIRE_DATA`). *Rejected:* no test (the next package could add a live name unseen).

**Icons (IC)**

- **PL-D18. A standard-library Python generator with committed outputs and a check target**; the PNGs are the inputs; no
  SVG rendering in the emulator repository (the PNGs were rendered with the UI checkout's Playwright, as the UI's
  `scripts/build-icon.ts` renders its own; the README says how). *Rejected:* generating at build time (a Python build
  dependency on every system); a render script that needs the UI's `node_modules`.
- **PL-D19. The `.ico` has seven PNG entries: 16, 24, 32, 48, 64, 128, 256.** *Rejected:* 20 and 40 px (not in the
  approved set; Windows scales); BMP entries.
- **PL-D20. The `.icns` holds PNG elements** (`icp4` 16, `icp5` 32, `ic11` 16@2x, `ic12` 32@2x, `ic07` 128, `ic13`
  128@2x, `ic08` 256, `ic14` 256@2x, `ic09` 512, `ic10` 512@2x), no `TOC `; MACa's `macos.icns.iconutil` runs Apple's
  `iconutil -c iconset` on it (read-only, in the work folder) and, if Apple's reader refuses the small PNG elements,
  MACa switches 16 and 32 px to `is32`/`s8mk` and `il32`/`l8mk` in the generator. *Rejected:* deciding the format
  without the Mac.
- **PL-D21. `_NET_WM_ICON` with 16 to 128 px** (24,652 values, inside the core X11 request size), `WM_CLASS`
  `nulldc3`/`nulldc3`, `StartupWMClass=nulldc3`. *Rejected:* 256 px (needs BIG-REQUESTS); no `WM_CLASS` (the desktop
  could not tie the window to the AppImage's launcher).
- **PL-D22. Adoption where each system is built**: the `.rc` and the window class by W64a, the bundle by MACa, the
  AppImage's hicolor PNGs (16 to 512, copied from `nulldc3/icon/png/` at packaging, not committed twice) and `.DirIcon`
  by CIe. *Rejected:* IC adopting them blind.
- **PL-D23. The art as approved on every system** (no macOS margin variant) until the owner answers PL-Q2.

**Versions (VCu, VTe)**

- **PL-D24. The format of 5.1**: `<h>` the first 10 characters of `git rev-parse HEAD`; `describe --tags --long --match
  v[0-9]*` parsed from the right, an output that does not parse taken as no tag; the version's leading part the tag's
  when one is reachable, else the base, and every derived number from that same string; "a checkout" by `rev-parse
  --show-prefix`; git run with an argument list, no shell, `GIT_OPTIONAL_LOCKS=0`. *Rejected:* describe's own
  `3.0.0-pre-N-g<h>` (sorts before a later `3.0.0`; build metadata does not); the UI's current `<base>+<describe>`
  (`3.0.0-pre+v3.0.0-pre` on the tag); describe's abbreviation (`--abbrev` is a minimum: a full and a shallow clone may
  disagree, and a test in a small repository cannot see it dropped); the numbers from the base (they disagree with the
  string after a later tag or a base bump); a real-path comparison for "a checkout" (`C:/` against `C:\` on Windows); a
  git command line through a shell (`cmd.exe` keeps the quotes of `'v[0-9]*'`).
- **PL-D25. The emulator's version is made at build time** (5.2). *Rejected:* configure time with more dependencies (a
  new tag stays unseen: a tag is a new file in a directory, no dependency); a header in the source tree (two build
  directories of one checkout write one file). One translation unit reads the generated header (PL-D75).
- **PL-D26. The base lives in a file of each repository** (`nulldc3/version-base.txt`, `package.json`), and each tag job
  refuses a tag that differs from it. *Rejected:* the base in `project(VERSION)` (cannot hold `-pre`); no check (the
  next tag would keep an old fallback silently).
- **PL-D27. Every reader of the emulator's version carries the derived string** (5.3), and the build time leaves the
  titles and the About tab (`__DATE__`, `__TIME__`). *Rejected:* keeping "3.0.0" in `debugger.describe` (the owner asked
  for the version from the tag; the protocol allows any string; the UI shows it).
- **PL-D28. `--version`** prints `nullDC <version>`, exit 0, on every system. *Rejected:* no switch (CI and the release
  checks would have to start a machine to read the version).
- **PL-D29. Windows numbers `X,Y,Z,N`; Apple `X.Y.Z` and `X.Y.Z.N`** (5.1), the whole string in `StringFileInfo`, in
  `NullDCVersion` and in the About panel (5.3). A check reads the Windows numbers from `FileMajorPart` ...
  `FilePrivatePart` (or `FileVersionRaw`) and the strings from `FileVersion` / `ProductVersion` (5.1). `CFBundleVersion`
  keeps four parts: Apple documents one to three integers, a rule its App Store enforces; outside the store
  LaunchServices compares the parts in order and `plutil` and `codesign` take it (MACa's gate shows both). *Rejected:*
  parsing `-pre` into a number (Windows and Apple have no pre-release field); a three-part `CFBundleVersion` (`X.Y.N`
  collides across tags, `N` alone sorts wrongly across releases).
- **PL-D30. "Dirty" is a tracked change, the same in both repositories**, computed by the script, not by `describe
  --dirty`. *Rejected:* the UI's current rule (untracked files make `gitRev` dirty while `describe` does not).

**Windows (W64a, W64b)**

- **PL-D31. The released `nulldc3.exe` stays a console program** (as the CMake build makes it today): headless, CI, exit
  codes and `--help` work with no special code; a double-click shows a console beside the window, which for a
  developer's tool is where the log is. The dead `WinMain` / `CommandLineToArgvA` / `--console` branch goes; `--console`
  stays an accepted switch that does nothing in this build, as N1 documented. *Rejected:* a GUI program (`--help` from a
  terminal needs `AttachConsole`, `cmd.exe` does not wait for it, and the harnesses lose nothing either way).
- **PL-D32. MSVC flags**: CMake's defaults kept and extended (`/EHsc /GR /W3 /utf-8 /bigobj /Zc:__cplusplus
  /permissive-`), the static CRT (`CMP0091` new, `MultiThreaded$<$<CONFIG:Debug>:Debug>`), `/Z7` (`CMP0141` new),
  `/STACK:8388608` (the Linux thread stack), `/CETCOMPAT:NO` (the JIT leaves the main loop without `ret`). Linux flags
  unchanged. *Rejected:* `/MD` and the redistributable beside a lone exe; `/Zi` (a lingering `mspdbsrv.exe`).
- **PL-D33. CHD's LZMA and FLAC codecs and the 7z / zip archives are built with MSVC**; picoTCP (the modem) stays
  GCC/Clang-only and the Windows modem a stub. A codec that needs a configuration header gets an MSVC one under its
  directory. *Rejected:* a Windows build that cannot open most CHD images.
- **PL-D34. The no-dialog policy applies to every run** (error mode, `WerSetFlags(WER_FAULT_REPORTING_NO_UI)`, CRT
  abort, invalid-parameter, pure-call and report handlers, a vectored fatal handler, `SetUnhandledExceptionFilter`,
  `os_DebugBreak` without `__debugbreak` unless a debugger is attached); the help and error message box remains only for
  a run that is not headless (an `argv` scan for `Headless=yes` in a `--config` item) and whose output nobody keeps.
  **The vectored handler's scope**: it takes `EXCEPTION_ACCESS_VIOLATION`, `IN_PAGE_ERROR` and `DATATYPE_MISALIGNMENT`
  only when the exception address lies in `nulldc3.exe`'s image or in the code cache; another module's exceptions go
  through that module's SEH frames (a driver that handles its own first-chance fault keeps working in a windowed run)
  and, unhandled, reach the unhandled-exception filter, which prints the same line and ends with the same code; illegal
  or privileged instruction, integer divide, stack overflow and breakpoint (no debugger attached) stay in the vectored
  handler. **A headless run calls no `CoInitialize`** (it moves into the windowed path of `os_CreateWindow`; an STA
  makes hidden windows). *Rejected:* headless only (a crash dialog helps no one; the decision would depend on a
  configuration not yet read); a vectored handler for every module's access violations (it would end a windowed run on a
  fault a graphics driver handles itself).
- **PL-D35. Fatal ends use Linux's wording and 128+N exit codes on Windows and macOS**; on macOS in automated runs only
  (PL-D45). The line is `fault_handler: unhandled <SIG> at address <a>, pc <p>, not a guest memory fault: terminating`
  on stderr; `<SIG>` is `SIGSEGV` for an access violation, an in-page error or a stack overflow (`SIGBUS` where Darwin
  delivers it), `SIGILL` for an illegal or privileged instruction, `SIGTRAP` for a breakpoint instruction, `SIGABRT` for
  abort, `terminate` and the CRT's handlers, `SIGFPE` for an integer divide. Codes: 139, 138 (`SIGBUS`, Darwin), 132,
  133, 134, 136. `die` / `verify` with no debugger attached end the process themselves with 133 after their own `Fatal
  error : ...` line on stdout (`msgboxf`), with no `fault_handler` line, so that a test tells them from a breakpoint
  instruction. The test kinds of `NULLDC_TEST_FATAL`: on Windows `segv`, `ill`, `trap` (`die`), `abort`, `div` (a divide
  by a volatile zero), `stack`, `invparam` (a CRT invalid-parameter call) and `purecall`; on macOS arm64 `segv`, `ill`,
  `trap`, `abort` (`div` is no kind on arm64: `SDIV` / `UDIV` return 0 and Apple silicon has no floating-point traps).
  Linux is unchanged (its handler words `SIGSEGV`, `SIGBUS` and `SIGILL`; `die` raises `SIGTRAP` silently). *Rejected:*
  NTSTATUS values (the harnesses would need a rule per host); a `div` kind on arm64 (nothing would raise).
- **PL-D36. DirectSound uses the desktop window when there is no window**, and a failure logs and falls back to no sound
  instead of `verifyc`. *Rejected:* forcing `none` in headless on Windows only (Linux headless keeps `auto`).
- **PL-D37. The Windows ABI and memory fixes are in the round**: XMM6-15 saved by `Mainloop` and `RunBlock` (W64a); the
  check stub's shadow space and the Windows flush budget (W64b); the vmem mapping through placeholders (`VirtualAlloc2`
  / `MapViewOfFile3`, found at run time); the unwind table registered over the prepared code region; `_mm_setcsr` for
  the host FP mode on MSVC; the `HASH` debug lines and the `__assume` macro out. *Rejected:* leaving the XMM defect (an
  ABI violation whatever the debugger does).
- **PL-D38. The x64 JIT's native debugger checks are switched on for Windows in W64b**, when the sh4bp gates pass on the
  Windows host (design 6.3's W64). If a gate cannot be made to pass, W64b stops and reports. *Rejected:* keeping the
  fallback on Windows (the harnesses would need a Windows exception, and design 5.8 has waited for this since E4b).
- **PL-D39. Floor and toolset**: Windows 10 1803 or later (placeholders); the Windows host builds with VS 2026 (MSVC
  14.51), CI with the runner image's newest MSVC; a VS 2019 build is attempted in W64a and reported, not gated.
- **PL-D40. The window items of W64 go to the owner, by hand**: PLG's PR text lists them per system (start windowed; the
  icon in the title bar, task bar or Dock; About; pause with the debugger and close the window; the E4a item; the window
  sessions of design 6.1.2). *Rejected:* an agent opening a window on the owner's desktop.
- **PL-D41. The Windows host's Python 3.11 is used as it is; Node is the host's if it is at least 22.13**, otherwise the
  official Node 22 zip unpacked into `C:\nulldc3-work\tools\node`. *Rejected:* an embeddable Python (one is installed);
  asking the owner to install anything on Windows.

**macOS (MACa, MACb)**

- **PL-D42. The bundle is made by CMake**: `MACOSX_BUNDLE`, `Info.plist.in`, `org.emudev.nulldc3`, the `.icns`, the
  licences of PL-D80 in `Resources`; the version keys and `codesign --force --sign - --timestamp=none` (no `--options
  runtime`, no entitlements), then `codesign --verify --strict --deep`, by **a custom command with a stamp file**: it
  depends on the linked executable, `nulldc3-version.cmake`, `Info.plist.in`, the bundle's `Contents/Info.plist` and
  every bundled resource, belongs to an `ALL` target that depends on `nulldc3`, and is the last step of every build that
  changes the bundle (5.2); deployment target 11.0; `-G Xcode` signs ad hoc through Xcode itself. *Rejected:* keeping
  `build_app_bundle.sh`; the hardened runtime with `allow-jit` (only needed for notarization, which the owner does not
  want); a `POST_BUILD` step (it runs only when the executable relinks, while the Ninja and Makefile generators rewrite
  `Contents/Info.plist` from the template at every generate: after a reconfigure the version keys would be gone and the
  seal broken).
- **PL-D43. macOS headless is decided after `nulldc_init`**: `NSApplication` is made only by a windowed
  `os_CreateWindow`; a headless run never touches AppKit, runs the UI loop on the main thread and returns
  `runctl_exit_code()`. **An alert** (a refused command line ends before the configuration is read, so "windowed" is not
  known there) **only when no `--config` item of `argv` sets `Headless=yes` (PL-D34's scan), stderr is `/dev/null` or
  closed, and the process was started by LaunchServices (`__CFBundleIdentifier` in the environment, or `getppid()` 1)**;
  otherwise the message is printed and the exit code is 2. *Rejected:* an `argv` scan before AppKit for the whole run
  (`emu.cfg` can set `Headless` too); an alert keyed on stderr alone (a non-pty ssh session's stderr can be a socket,
  and `2>/dev/null` is a character device: an alert on the owner's Mac).
- **PL-D44. `darw_printf` and `#define printf darw_printf` go**; stdout is line-buffered as on Linux. *Rejected:*
  keeping it (the harness protocol, thread safety and the tests' link all break with it).
- **PL-D45. Automated runs on Darwin (headless or with an `Exit.*` key) end fatal signals with `_exit(128+N)`** after
  the stderr line of PL-D35 (`SIGSEGV`/`SIGBUS` that are not guest faults, `SIGILL`, `SIGTRAP`, `SIGABRT`, `SIGFPE`;
  `SIGBUS` ends with 138), so no crash report is written into `~/Library`; interactive runs keep re-raising (a user's
  crash report). Linux unchanged. *Rejected:* accepting crash reports in the owner's Library.
- **PL-D46. The arm64 ARM7 recompiler is not built in this round** (`FEAT_AREC` none on arm64 hosts, as on x86_64; the
  code stays, source-consistent, for A64). *Rejected:* fixing it now (write-protect toggling, a near region, range
  checks, a free: a JIT on a system the round meets for the first time, which A64 owns); off at run time only (it would
  still have to build on Linux arm64).
- **PL-D47. 16 KiB pages for arm64 Darwin only**; macOS x86_64 stays source-consistent and unbuilt. *Rejected:* refusing
  x86_64 Darwin at configure (PLAN keeps it a target).
- **PL-D48. `shm_open` on Darwin** (the per-process name of D1 fits the 31-character limit; the file fallback stays),
  **8 MiB stacks for `cThread` on Darwin**, **`-ffp-contract=off` on arm64 hosts**. *Rejected:* guest memory in a file
  of the portable directory (disk writes, and it fails where that directory is read-only).
- **PL-D49. App Translocation is named** in the "cannot be written" error (a bundle path under `/AppTranslocation/`);
  the zips have a top-level folder; window restoration is off (`restorable = NO`), so a windowed run writes nothing
  outside the portable directory but what macOS itself keeps. *Rejected:* the generic message only.
- **PL-D50. The Darwin sent-signal rule (`si_code`) is not changed** (backlog). *Rejected:* a change nothing tests.

**Linux (CIe, LA64)**

- **PL-D51. The AppImage's portable directory**: when `$APPIMAGE` is set **and** `/proc/self/exe` lies under `$APPDIR`,
  the executable's directory is the directory of `$APPIMAGE`; otherwise as today (an inherited `APPIMAGE` from another
  AppImage is ignored). The desktop file says `Exec=nulldc3 %f`. *Rejected:* `$APPIMAGE` alone (children of other
  AppImages inherit it); `--shared` in the desktop file (the owner: portable).
- **PL-D52. Linux arm64 runs the arm64 SH4 JIT**, made to build for ELF by LA64, verified here by a cross build and
  `qemu-aarch64-static`, natively first in CI; a kernel whose page size is not 4 KiB is refused at start with one line.
  *Rejected:* an interpreter-only arm64 AppImage (the JIT is the one shared with macOS, which the round runs natively);
  waiting for CI alone (the first native build would be on the PR with no evidence before).
- **PL-D53. The Linux builds of CI use `ubuntu-24.04` and `ubuntu-24.04-arm`** (GCC 13, the gates' compiler), so the
  AppImages need glibc 2.39 or later. *Rejected:* an older image (a compiler the tree has never met, first met in CI).

**CI and releases (VCu, CIe)**

- **PL-D54. Thin workflows over committed scripts** that the packages run on the three systems. *Rejected:* logic in
  YAML (testable only on GitHub).
- **PL-D55. GitHub-hosted runners only.** *Rejected:* self-hosted runners on the owner's machines (a public repository's
  PRs could run code there; logs would name them).
- **PL-D56. Triggers: pull requests, tags `v*`, dispatch.** *Rejected:* builds of every push to `main` (the owner asked
  for PRs and tags).
- **PL-D57. PR runs also make the release files**, kept 7 days as artifacts, so packaging is tested on every PR and the
  owner can try a PR's build; only tags attach them to a Release. *Rejected:* packaging on tags only (the first run of
  the packaging would be the public tag).
- **PL-D58. CI's test data is what public repositories with licences give**: the two vector submodules, `devtools/` with
  `npm ci`, the SH and ARM binutils from apt (if CIe shows their output matches the oracles; else those oracles skip in
  CI), `NULLDC_TESTS_REQUIRE_DATA=OFF`, the skipped tests listed in the job summary. No 2ndmix/hello ELF, no RTL
  checkout, no emsdk in CI: the full gates run on the three machines in the packages and in PLG. *Rejected:* bringing
  the data in without the owner (PL-Q1).
- **PL-D59. A tag with `-pre` makes a published pre-release**, titled "nullDC <v>" / "Dreamcast DevTools <v>", with
  `SHA256SUMS` and notes from a template. The tag job makes it as a draft with its files, checks the draft's asset list,
  and publishes it in the same job (PL-D77): a failed upload leaves a draft, never a published Release with files
  missing. *Rejected:* a draft left for the owner to publish (a manual step the owner did not ask for; the tag itself
  needs the owner's go).
- **PL-D60. Actions pinned by full commit SHA**, `contents: read` at the top and on `release-check`, `contents: write`
  only for `release`, which runs only on tag pushes (`if: github.event_name == 'push' && github.ref_type == 'tag'`) and
  needs only the jobs that make or check its files, never `release-check` (6.1); `concurrency` per ref. *Rejected:*
  version tags (the release job holds a write token); `if: github.ref_type == 'tag'` alone (a dispatch on a tag ref
  would publish); `release` needing all jobs (`release-check` is skipped on a tag push, and `release` would be skipped
  with it).
- **PL-D61. The host-detail rule in CI** (6.6): the runner's own paths, user and machine name are taken as GitHub's, not
  the owner's, and allowed until the owner answers PL-Q3; prefix maps in release builds; `ci.hygiene` on tracked files
  with its allow lists (PL-D79); owner-specific patterns outside the repositories. **The owner-specific patterns come in
  two sets**, given at run time from outside the repositories and outside `agents/nulldc3/`: **the remote patterns** —
  the Mac's and the Windows host's user names in path form (`/Users/<u>/`, `C:\Users\<u>\`, `C:/Users/<u>/`) and
  `<u>@<host>`, the Mac's ssh alias, the host names — and **the full patterns**, the remote ones plus this machine's
  home in path form (`/home/<u>/`). Which scan uses which: a package's directory under `agents/nulldc3/` (its logs carry
  this machine's paths, as in M1 to M3) and the scrubber use the remote patterns; everything that leaves the machine —
  committed files, the documents copied into `docs/nulldc3/`, the PR texts, CI files and CI logs — the full patterns.
  Both sets are matched without regard to letter case (Windows prints its computer name in capitals in places).
  **The Windows host's host name is also this machine's** (WSL2 takes it), and the gate ELFs 2ndmix.elf and hello.elf
  print it in a KOS build banner, `<u>@<host>:/opt/toolchains/dc/kos` (guests built here with KallistiOS:
  `<u>@<host>.localdomain:...`): every local log of a run of them carries that line, as it carries this machine's
  home. The package-directory scan therefore sorts its hits (4.3, PL-D73): KOS banner lines and a host name inside a
  longer word are counted and reported, not failures; the scan is clean when nothing else matches. The full patterns
  keep the host names and sort nothing (CI runs no gate ELF, and nothing that leaves the machine may quote the banner).
  The GitHub owner name in `<owner>/<repo>`, `github.com/<owner>/` and branch names is public and allowed (it is the
  prefix of the branch `skmp/nulldc3` and of both repositories). *Rejected:* scrubbing runner paths from logs (they are
  generic; compiler output cannot be filtered without hiding errors); the bare user name as a pattern or a scrubber rule
  (it matches, and would rewrite, the repositories' own names in every document and log); this machine's home in the
  package directories' scan (every local CMake, ctest and contract log carries it, and P0's recipe names it: no package
  could report clean); dropping the host names from the remote patterns (a remote log that escaped the scrubber would
  pass), or keeping no emulator output of 2ndmix and hello locally, as P0's recipe does (LA64's qemu runs, the contract
  gates' logs and PLG's need it) (R84).
- **PL-D62. The UI's release files are `dist-native` and `dist-wasm` zipped.** *Rejected:* no files (the owner asked for
  releases on tags in both repositories).
- **PL-D63. No source tarball.** The tags of both public repositories are the source (the UI at the pinned commit is a
  public tag too). *Rejected:* a tarball with the submodule and a `VERSION` file (a second version path to test).
- **PL-D64. ccache on Linux and macOS through `actions/cache`, installed by the job** (`linux.sh deps`; `brew install
  ninja ccache`; runner images do not ship it), `CCACHE_DIR` under `$RUNNER_TEMP`; none on Windows.
- **PL-D65. File names** as 6.4: `nulldc3-<v>-<system>-<arch>`, `arm64` (not `aarch64`) on Linux and macOS alike.

**Tags**

- **PL-D66. Annotated tags `v3.0.0-pre`, made and pushed by the integrator after both merges and with the owner's go,
  the UI's first at the emulator's gitlink, each repository's dispatch check first, and a second emulator dispatch check
  with `require_ui_tag` between the two tags** (section 7). *Rejected:* lightweight tags (no tagger, no message); the
  emulator first (its job would embed an untagged UI).
- **PL-D67. A failed tag run** is first re-run, failed jobs only, with the owner's go (`gh run rerun <id> --failed`);
  only a defect of the tagged commit goes through a PR; a tag is moved only while its Release is a draft or absent, and
  only with the owner's go; if the UI's tag moves, the emulator's pin moves to it in a PR first and the emulator's tag
  waits (7 item 11). *Rejected:* a PR and a new tag for every failure (a flake would move the UI's tag away from the
  emulator's gitlink).

**Added by revision 1**

- **PL-D68. Portability fixes have owners.** W64a (MSVC), MACa (AppleClang on Darwin) and LA64 (the Linux arm64 cross
  build) may change, in their own turn, any file of `libswirl/**` (deps included) and `tests/**` that their compiler
  refuses: no behaviour change, the Linux default build's `ninja -t commands` equal to the base's but for the files
  edited, P0 identical, each fix listed in the report as file, error and change; a frozen header excluded (a declaration
  a compiler refuses there goes to the integrator as header revision 9). W64a (Windows) and MACa (the Mac) may also make
  runtime portability fixes, on their system, in the earlier packages' test scripts that their gate is the first to run
  there: `tests/rebrand/**`, `tests/icons/**` and `make_icons.py`'s file input and output, `tests/version/**`; each fix
  leaves what the test checks unchanged and the Linux results equal, and is listed as file, failure and change.
  *Rejected:* stopping the package at every compile error (the first non-GCC builds of three rounds of code will have
  many, in files no brief names); stopping it at the first runtime failure of RB's, IC's or VTe's scripts on a system
  those packages cannot reach (a `file://` URL with a drive letter, a text-mode write, a construct newer than Python
  3.9: each would block the first remote gate on another package's file).
- **PL-D69. The remote run envelopes** (4.1 to 4.3). On the Windows host every program built in the round runs inside
  `nulldc3-winrun.exe`, ctest included (an outer `winrun` around `ctest`; the inner ones of `emurun.py` stay), and a
  long step through `winrun --detach`; `winrun` makes its desktop in the caller's window station under a unique name,
  samples the windows of its desktop while the child runs, exits 126 when its set-up fails, gives the child its own
  console, `CREATE_NO_WINDOW` or a pseudo console but never a new console window, and has a `--probe` that runs first.
  On the Mac every emulator run goes through `tests/macos/macrun.py` (the sandbox profile, a process group, a timeout,
  `TMPDIR` in the work folder, a PID file; `--detach` for long steps), and a `tests/macos` target fails rather than run
  without the profile unless `CI` is set. **`macrun.py` applies the profile only when the calling process is not
  sandboxed already**: it asks the kernel (`sandbox_check(getpid(), NULL, 0)` through `ctypes` from libSystem; 1 means
  sandboxed); when the caller is sandboxed it runs the command directly, because the inherited profile binds every
  descendant and cannot be loosened, and prints `macrun: inside the profile already` on stderr; it still refuses when
  the kernel cannot be asked, and when `sandbox-exec` is missing and `CI` is not set. `macos.sandbox.canary` runs twice
  in every Mac gate, from a ctest that is not sandboxed and from a ctest started under `macrun.py --detach`; both must
  fail to reach the window server, and the canary prints the kernel's view (sandboxed; the window server's lookup
  denied). *Rejected:* `winrun` for emulator runs only (a C++ suite that crashes under MSVC with the default error mode
  raises the just-in-time debugger or the crash dialog on the owner's desktop); counting windows when the child has
  ended (its windows are gone by then); `CREATE_NEW_CONSOLE` for `--own-console` (Windows 11 can hand a new console to
  Windows Terminal, whose window opens on the owner's desktop); `nohup` by hand on the Mac (every caller would apply the
  profile, the temporary directory and the timeout separately); `sandbox-exec` at every level (macOS refuses a profile
  inside a sandboxed process: `sandbox_apply: Operation not permitted`, and every `tests/macos` target and harness run
  inside a detached ctest would go red); an environment variable as the "already sandboxed" mark (the harnesses start
  the emulator with an environment of their own and would drop it).
- **PL-D70. No mutation or test disables a protection of the owner's desktop.** On the Windows host none turns off
  `winrun`'s error mode or job flags: crash reporting, the just-in-time debugger, hard-error pop-ups and the firewall
  show on the interactive desktop whatever desktop the child is on. On the Mac none reaches AppKit (`NSApplication`
  before `nulldc_init`, the headless guard removed, an alert path made reachable) or leaves a crash report by design (a
  re-raise in an automated run). **No crashing mutant of the no-dialog policy is run either**: without its handler a CRT
  invalid parameter ends in `__fastfail`, which passes every handler and the job's flag and goes straight to WER (a
  report queued on the owner's host), and a pure call ends in `abort()`, which the policy's SIGABRT handler turns into
  the same line and code (an equivalent mutant). Such mutants are judged by reading, or shown safely by targets: the
  `wer=`, `invparam=`, `purecall=`, `sigabrt=` and `abort=` fields of the UI report on Windows (read back from the CRT,
  no fault raised; `windows.headless.nowindow` requires them), `macos.sandbox.canary` for the profile on the Mac; the
  `invparam` and `purecall` kinds stay as positive tests of a correct build. `SetErrorMode` removed from the emulator
  cannot be seen under `winrun` (the child inherits `winrun`'s mode) and is judged by reading. *Rejected:* the round-0
  mutation "SetErrorMode and WerSetFlags removed with winrun's error mode off" (unsafe, and it survived: the vectored
  handler and the job flag still ended every fatal kind with 128+N); the round-1 mutations "the CRT invalid-parameter or
  pure-call handler removed, caught by `windows.fatal.invparam` / `.purecall`" (the first unsafe, the second equivalent:
  R74).
- **PL-D71. Every emulator run on the Windows host and the Mac carries P0's keys**: `--portable <dir under the work
  folder>`, `config:Headless=yes`, `audio:backend=none`, `config:Dreamcast.RTC=946684800`, plus the run's `Exit.*` keys;
  the same for CI's smoke runs and the release checks of section 7. *Rejected:* `Headless=yes` alone (headless keeps
  `audio:backend=auto`: the guest and 2ndmix would play through the owner's speakers, and the hashes would not be
  comparable with P0's recipe).
- **PL-D72. The remote work folders' layout.** The UI clone is `src\ui` / `src/ui`, never `dreamcast-devtools` (the
  bundle look-up's second candidate, `<tree>/../dreamcast-devtools/dist-native`); every gate build there is configured
  with `-DNULLDC_DEVTOOLS_DIST=OFF`; only builds that must carry the UI pass a directory explicitly. Before returning,
  W64a and W64b (MACa and MACb) leave `try\nulldc3.exe` (`try/nulldc3.app`): a Release build of the package's head
  configured with `-DNULLDC_DEVTOOLS_DIST=<the pin's dist-native>`, named in the report with its build time and peak
  memory (the first MSVC and AppleClang builds of the 6.7 MB embed); the next package replaces it; PLG removes it.
  Nothing is committed in a remote clone. *Rejected:* the owner trying a build in `b\` (deleted before the package
  returns); a separate `out\` folder (one name is enough).
- **PL-D73. Remote output is scrubbed before it reaches a file here** (4.3): the remote home paths, the remote user
  names in path form and `<u>@<host>`, the Mac's alias and the host names (`PL_MAC_HOST`, `PL_WIN_HOST`; both on every
  system's output; a host name as a whole word, in any letter case) become `~`, "the Mac" and "the Windows host"; a bare
  user name is never replaced (PL-D61) and remote listings name no owner; JUnit files are not copied back; each package
  ends by scanning its directory with the remote patterns of PL-D61, sorting the hits as 4.3 says: `KOS banner: N lines
  in M files` and `inside a word: N lines` are reported, not failures, and the scan is clean when no other hit remains
  (this machine's own paths in its local logs stay, as in M1 to M3). *Rejected:* trusting each command's output (CMake,
  compilers, ctest, `codesign` and the emulator's own "Config dir is:" line all print the remote home); scanning the
  package directories with the full patterns (R76: no package could pass it); an unsorted scan with the host names in
  it (R84: every local log of 2ndmix and hello carries the KOS banner, which names this machine, the Windows host's
  namesake).
- **PL-D74. Python 3.9 and the standard library** for every Python file the round adds or changes: the Mac's Python is
  Xcode's 3.9, the Windows host's is 3.11, nothing may be installed into either; `check_workflows.py` reads the workflow
  as text. Every package that adds or changes a Python file checks it in its gate with `ast.parse(source,
  feature_version=(3, 9))` (best effort: the parser refuses most syntax of 3.10 and later, `match` among it), so a
  construct newer than 3.9 is caught in the writer's own turn; what only runs differently (a library call of 3.10 or
  later) stays with the 3.9 runs of MACa and MACb. *Rejected:* Homebrew's Python on the Mac (one more prerequisite for
  the owner, and the Windows host would still need 3.11 compatibility).
- **PL-D75. One translation unit reads the generated version header**: `libswirl/nulldc_version.h`, a header with no
  `#include`, declares `nulldc_version()`, `nulldc_title()`, `nulldc_git_hash()`, `nulldc_git_dirty()`;
  `libswirl/version.cpp` includes only that header and the generated `nulldc3_version.h`, and defines
  `nulldc_version()`, `nulldc_git_hash()`, `nulldc_git_dirty()`; `nulldc_title()` needs `VER_EMUNAME` (`types.h`, which
  needs a `TARGET_*`), so it is defined in `libswirl/version_title.cpp`, which includes `types.h`, calls
  `nulldc_version()` and never reads the generated header; `types.h`, `dbg_webui.cpp` and `test_webui.cpp` include
  `nulldc_version.h`; `VER_SHORTNAME` / `VER_FULLNAME` expand to `nulldc_title()` (both are used as `const char*`
  arguments, in `spg.cpp` and `win_common.cpp`); the generated directory is on `version.cpp`'s include path alone (and
  the `.rc`'s). The tests project runs `version.cmake` and compiles both units into the library that holds `cfg/cl.cpp`;
  the standalone `tests/debugger/net` project (`project(dbg_net_tests)`: no `config.cmake`, no `TARGET_*`, no `types.h`
  in any of its sources) runs its own step under a target name of its own — it is also a subdirectory of the tests
  project, and two `add_custom_target`s of one name stop the configure — and compiles `version.cpp` alone into the
  `dbg_webui` object library, which `test_webui`, `dbg_net_demo` and every other consumer of its objects link.
  *Rejected:* the generated header behind `types.h` (nearly every translation unit includes it: every commit would
  recompile the whole emulator, a full MSVC rebuild per commit, and every Linux compile command would change); the
  include path on the whole emulator target (the same compile-command change); the declarations in `types.h` with
  `version.cpp` including it (round 1's text: through `build.h` it stops with `#error Invalid Target: TARGET_* not
  defined` in the net project, and on MSVC pulls in `<Windows.h>` there: R78).
- **PL-D76. Every release build embeds the UI explicitly, and every release file is started where it is made.** The four
  platform jobs `need: ui` and configure with `-DNULLDC_DEVTOOLS_DIST=<the downloaded ui artifact>` (an explicit path
  makes a missing bundle a configure error); each saves its configure line `Dreamcast DevTools: embedding ...
  (<version>, protocol N)` with its release file, and `release.sh` refuses unless all four name the `ui` artifact's
  version. Each script ends with a smoke run of the file it made (6.3), the `ui` of `debugger.describe` included.
  *Rejected:* relying on the look-up (on Windows and macOS it finds nothing and only prints "not embedded"); checking
  only the `ui` artifact (it says nothing of what each binary embedded); leaving the arm64 AppImage unstarted until the
  owner's users start it.
- **PL-D77. CI's modes**: `release-check` (`contents: read`; `if: github.event_name != 'push'`) on pull requests and
  dispatches, `release` (`contents: write`) only on tag pushes (`if: github.event_name == 'push' && github.ref_type ==
  'tag'`), needing only the jobs that make or check its files (PL-D60); no `dry_run` input, a dispatch never creates
  anything, on whatever ref it runs. The release scripts have **explicit invocations** (6.1): `release-files.ts pack`
  (the UI's `package` job: the zips, `SHA256SUMS`, the notes; never `gh`), `release-files.ts check` / `release.sh check`
  (`release-check`: creates nothing) and `release-files.ts publish` / `release.sh publish` (`release` alone: it takes
  the files from the artifacts and refuses unless `GITHUB_EVENT_NAME` is `push` and `GITHUB_REF_TYPE` is `tag`);
  `publish` makes a draft, checks its assets and publishes, and on a re-run uploads with `--clobber` into a draft and
  refuses a published Release; every artifact is kept 7 days. *Rejected:* a `dry_run` input (a dispatch with it false
  would make a Release of `main`'s files under the tag's name with a write token); running the release plumbing first on
  `main` after the merges (a defect would cost another PR and merge before the tags); a version check on pull requests
  (a PR has no tag, and its version is never the base); a mode chosen by `GITHUB_REF_TYPE` alone (round 1's text: the
  UI's `package`, which runs on every event and calls the same script, would try `gh release create` on the tag push
  with a read token and before `check` and `e2e`, fail, and leave the UI's tag without a Release — and the fix would
  move the tag off the emulator's gitlink: R79; a dispatch on a tag ref would also enter it: R80).
- **PL-D78. S04 on arm64's recompiler is recorded, not gated.** The design's contract notes say that behind a backend
  that restarts its slice counter (the arm64 JIT) a switch of loops can move `dc.timing.SH4_CYCLES` by less than a slice
  and "S04 is then not a gate for that backend's fallback". On arm64 the recompiler's runs with and without `--fallback`
  are that fallback (`CaptureEngine` in `dbg_emu.cpp`: `engine.fallback = engine.jit && (forceFallback || backend ==
  nullptr || !(backend->DebugCaps() & SH4DBG_NATIVE))` is true there either way, and the `.fallback` targets are
  `--engine jit --fallback`), so both record S04; the interpreter keeps M3G's numbers in full. **The rule lives in
  `tests/debugger/contract/contract.py`'s `run_runner`** (MACb's, in W64b's file): when `platform.machine()` is `arm64`
  or `aarch64` and the engine is jit, a runner result whose only failed scenario is S04 (the summary's failed 1, partial
  0, not applicable 0, no invariant violation, no clean-up problem, the required notes present, S04 the only required
  scenario not passed) passes the target if the `--json-file` summary shows S04 failing on one of its
  `dc.timing.SH4_CYCLES` step checks — "went back from `<a>` to `<b>` in a step" with `<a> - <b>` below `SH4_TIMESLICE`
  (448, `sh4_interpreter.h`), or "did not increase in a step that did not end at an exception vector" (moved by 0) — and
  the target then prints `S04: recorded (PL-D78): <details>`. Every other result is unchanged, on every host. A test of
  the rule on synthetic summaries runs on every host. No change of the UI's runner in this round; PLG writes S04's arm64
  rule into Appendix D row 3. *Rejected:* gating S04 on arm64's recompiler (a failure the design predicts would stop
  MACb); gating it on the forced fallback there (the same engine: a correct implementation would be called a defect);
  "recorded" without a mechanism (`run_runner` fails a target on any failed scenario, and the gate counts targets: R73);
  a runner change (no UI change or pin move is allowed after VCu).
- **PL-D79. Allow lists are shared rows files.** `tests/rebrand/allowed.txt`: `docs/nulldc3/**` and the licence files as
  whole paths (no rows, no "gone" check), every other piece of history a row; every package, PLG included, that adds,
  moves, rewraps or deletes a historical occurrence edits its rows in its own turn. `nulldc3/ci/hygiene-allow.txt`
  (CIe's, then PLG's rows for the documents it commits) and `nulldc3/ci/hygiene-allow-ui.txt` (the UI's tracked files,
  whose own hygiene checks name the generic patterns; passed with `--allow` when the UI is scanned). *Rejected:* one
  owner per allow list (PLG, which owns no test, could not commit this cut's copy or the plan's rebrand lines without
  turning `rebrand.names` and `ci.hygiene` red).
- **PL-D80. Every emulator release file carries the licences**: `LICENSE`, `LICENSE-GPL`, `LICENSE-LGPL`,
  `nulldc3/LICENSE` (reicast's BSD shell licence, which asks for its notice in binary distributions) as `LICENSE-shell`,
  and the embedded UI's MIT licence as `LICENSE-devtools` — in the Windows and macOS folders and the AppImage's
  `usr/share/doc/nulldc3/` copied by the scripts from `<ui>/LICENSE`, the UI checkout they are given (PL-D85), and in
  the bundle's `Resources` from `devtools/LICENSE` (copied into the build directory under those names first, since
  three are named `LICENSE`); the UI's two zips carry the UI's `LICENSE`. A missing `<ui>/LICENSE` stops the script
  with the file's name; the copy is never optional. **The bundle carries
  `LICENSE-devtools` only when it embeds a UI** (`DBG_WEBUI_BUNDLE_DIR` not empty): then the configure stops with a
  message naming `devtools/LICENSE` if it is missing; without a UI the bundle carries the other four, and its configure
  never needs the submodule (a plain clone without `--recursive` configures on macOS as on Linux and Windows);
  `macos.bundle.codesign` expects exactly the set that matches the embed. On the remote systems the submodule `devtools`
  is initialised from the UI clone (4.1, 4.2), so the Mac's `try` bundle and `macos.sh`'s bundle find
  `devtools/LICENSE`; `<ui>` there is the UI clone, at the same commit. *Rejected:* copying `devtools/LICENSE` on every
  macOS configure (round 1's text: the Mac's clone had no submodule, and every gate configure there would stop: R81);
  the scripts reading `devtools/LICENSE` themselves (R85: the worktrees here leave the submodule uninitialised, and the
  x86_64 AppImage is made here).
- **PL-D81. The AppImage's runtime is pinned**: the static type-2 runtime (it needs only `fusermount3`), one release
  file per architecture with its SHA-256, passed with `--runtime-file`; no tool downloads anything while packaging.
  *Rejected:* pinning only `appimagetool` (it fetches the runtime from a moving "continuous" release unless given one);
  the old runtime (it needs `libfuse.so.2`, which a stock Ubuntu 24.04 does not have).
- **PL-D82. `windows.ps1` is written for Windows PowerShell 5.1** (the Windows host's), and the workflow runs it with
  `shell: powershell`; the zip is made with `tar.exe -a -c -f` (bsdtar, in Windows 10 and later) or .NET's `ZipFile`,
  never `Compress-Archive` (version 1.0.1.0 writes `\` into entry names). *Rejected:* `pwsh` (not on the Windows host;
  the script would be proven on one PowerShell and run on another).
- **PL-D83. The shipped binaries meet P0.** The release AppImage's binary (Release, the prefix map, the embedded UI)
  runs P0's default rows (2ndmix 600 and 4000, hello 300, the guest 90, both engines, P0's recipe) in CIe's gate and in
  PLG, equal to P0; a difference stops the package and goes to the integrator with its first differing part. *Rejected:*
  gating only the RelWithDebInfo builds (the files users get would never be compared).
- **PL-D84. PLG's pushes, PRs, submodule move, dispatches and tags are the integrator's**, done in the orchestrating
  session, outside the package workflow, step by step with the owner (section 7). The workflow forbids pushes and cannot
  wait days for the owner's merges; section 9's "push nothing; never tag" binds packages, not these steps. *Rejected:*
  running them inside the workflow (blocked, or the rule broken).
- **PL-D85. The CI scripts take the UI checkout as an argument, `<ui>`**: `linux.sh` (`test`: `npm ci` in `<ui>`, which
  the tests project gets as `NULLDC_DEVTOOLS_DIR`), `appimage.sh`, `windows.ps1` and `macos.sh` (`LICENSE-devtools`
  copied from `<ui>/LICENSE`; `windows.ps1` and `macos.sh` also run `npm ci` in `<ui>` for their tests project). The
  value is the submodule `devtools` on a
  runner (the job's checkout with submodules), the UI clone `src\ui` / `src/ui` on the Windows host and the Mac (the
  clone of 4.1 and 4.2, where the contract runner already runs), and `agents/nulldc3/wt/ui-pin` here (section 3 keeps
  it at the gitlink). A script stops, naming what it compared, when `<ui>/LICENSE` is missing or `git -C <ui> rev-parse
  HEAD` is not the tree's gitlink `HEAD:devtools` (the comparison `devtools.commit` makes), so the licence shipped is
  the pinned UI's. Every worktree here has an empty, uninitialised `devtools/` (the package workflow initialises only
  the two vector submodules), and the gitlink at CIe and PLG is VCu's verified head, which is on GitHub only from
  section 7 item 2. *Rejected:* initialising each worktree's submodule from the local UI repository (`git -c
  protocol.file.allow=always submodule update --init devtools`; it works, the shared config points there, but it makes
  a second UI checkout and a second `node_modules` per worktree beside the pin every gate already gives as
  `NULLDC_DEVTOOLS_DIR`, and a step every package and verifier must remember); the submodule `devtools` as `<ui>` on the
  remote systems (a second `npm ci` there beside the UI clone the contract runner uses); a `LICENSE-devtools` copy that
  is skipped when the file is missing (the AppImage would ship without the UI's licence) (R85).
- **PL-D86. The arm64 recompiler takes the x86-64 slice model** (2026-10-23, A64): the counter is tested in front of a
  block's charge, so a slice ends behind the block that used it up, as on x64 and the interpreter fallback, not in front
  of the block whose charge overdraws it. The frozen fallback contract allows no other model. It moves every arm64
  slice end, so the arm64 recompiler's state-hash references change on both arm64 hosts (re-derived by A64); the
  owner accepted that the same day. Supersedes PL-D78 when A64 merges. *Rejected:* keeping arm64's model and listing
  the 13 as known limits (PL-D78; the owner asked for them fixed before PLG).

### 8.2 Questions only the owner can answer

Work proceeds on the recommendation; an answer to the contrary changes the package named.

- **PL-Q1. Test data in CI.** May CI fetch data that is not in a public repository with a licence — the KallistiOS ELFs
  `2ndmix.elf` and `hello.elf` (from where?), and the RTL checkouts caique-rtl (MIT), wren7-rtl and shrike4-rtl (public,
  no licence file) at pinned commits? With them, CI would run the contract gates M1 to M3 proper and about 100 more
  targets; without, it runs about 205 targets on Linux and the `.guest` variants of M2 and M3. **Recommendation: not in
  this round** (CI as PL-D58; the full gates on the three machines in the packages and at PLG); a later package adds the
  data once the licences are settled (PLAN, "Needs the user"). Note for that answer: the two ELFs as they are print a
  KOS build banner that names the Windows host and its user (4.3, R84), so a CI run of them would put both into public
  logs; CI would need copies built without the owner's details (or with the banner's text replaced).
- **PL-Q2. A macOS icon with Apple's margin.** The approved tile fills its canvas; macOS 11 and later draw app icons on
  an ~824/1024 body, so nullDC will look about 20 % larger than its Dock neighbours, and macOS 26 may put a
  non-conforming icon on a grey tile. **Recommendation: ship the approved art for 3.0.0-pre**; the owner looks at the
  Dock after MACa (section 3, row 7) and, if wanted, approves a padded variant for a later release (IC's generator then
  takes a second PNG set for the `.icns` only).
- **PL-Q3. The runner's own paths in CI logs.** The owner's rule is that nothing in CI files or CI logs names a host, a
  user, a home path or a LAN address. Every GitHub-hosted log carries the runner's home and work paths (for example the
  runner user's home on Linux and macOS), the user name `runner` and, in the "Set up job" step, the runner machine's
  name; they cannot be scrubbed without hiding compiler errors, and none of them is the owner's. **Recommendation:
  accept them as GitHub's, not the owner's** (PL-D61); the workflows and scripts still name nothing themselves, and the
  integrator scans the first runs' logs for the owner's own details. Work proceeds on the recommendation; both PR texts
  say so until the owner answers.

### 8.3 What the owner does (not questions)

1. Before MACa: the commands of 4.4 on the Mac.
2. Merging the UI PR, then the emulator PR (section 7, items 2 and 5).
3. One go for the dispatch checks and the two tags (section 7, items 6 to 10); a go for a re-run of a failed tag run's
   jobs, or for moving a tag (section 7, item 11).
4. An answer to PL-Q3 before the emulator PR is opened, if possible (work proceeds on the recommendation).
5. The by-hand list of PL-D40 on each system, whenever convenient (it gates nothing); the `try` builds of PL-D72 on the
   Windows host and the Mac between packages (section 3).
6. Optional: required status checks on `main` in both repositories (a repository setting; the round changes no setting).

### 8.4 Risks the round carries

- **The first MSVC build and the first full clang build of three rounds of code** (`windows.md` 8, 14; `macos.md` R1).
  Code that GCC accepts only with `-fpermissive`, Darwin's `u64` (`unsigned long long`) against `size_t`: expect fix
  rounds inside W64a and MACa, in files no brief names; PL-D68 gives them owners. A `g++ -fsyntax-only` pass without
  `-fpermissive`, and a clang build here, are allowed first steps of those packages.
- **The first real arm64 emulator meets the debugger's arm64 rules** (MACb): `registersStale`, the SH4 fallback as the
  JIT's only debug engine, S04 without a kept slice counter. S04 is settled in advance (PL-D78: recorded, not gated, on
  arm64's recompiler with and without `--fallback`, by a rule in `contract.py`, as the design's contract notes predict).
  Any other contract failure there may be a gap of the design, not a port bug; the integrator decides (a recorded known
  limit for arm64 with the scenario's arm64 rule written in Appendix D, or a fix).
- **MSVC floating point and the baselines made by GCC** (`sh4.*`, `arm7.*`, `aica.dsp.*` ratchets): a difference shows
  on Windows only. The rule: a defect to find first (`_mm_setcsr` is part of W64a for this reason); only the integrator
  accepts a per-host known difference, with its cause named.
- **Timing-sensitive tests on desktops the owner uses** (the contract's `grace` and `slow` modes against Windows' timer
  and loopback buffers; `sgc.drain`'s nap; the Mac's P and E cores): a flake is rerun once and reported; a second
  failure is a defect.
- **The vmem placeholder change on Windows** replaces a mapping that never ran there; its gate is the 2ndmix and guest
  runs and the C++ suites on the Windows host, with `vmem.*`'s Linux cases unchanged.
- **The macOS restructure touches the windowed start**, which only the owner can try (PL-D40).
- **`sandbox-exec` on macOS 26** is deprecated; if it is missing, MACa stops before its first run (4.3), and every
  `tests/macos` target fails rather than run without it (PL-D69). The owner's `ls -l /usr/bin/sandbox-exec` of 4.4 shows
  it first. macOS refuses a profile inside a sandboxed process; `macrun.py` asks the kernel first and applies the
  profile once (PL-D69), and the canary runs in both shapes in every Mac gate. That nesting rule is Apple's behaviour as
  Homebrew, Nix and Bazel met it; this cut was written without the Mac, so MACa's first gate is its proof.
- **The protections of the owner's Windows desktop are `winrun`'s error mode and job flag and the emulator's policy**,
  not the private desktop (4.3): a crash dialog or the just-in-time debugger would show on the owner's screen if all
  three failed at once. Every program built in the round runs inside `winrun`, and no mutation turns them off (PL-D69,
  PL-D70).
- **Long remote steps** outlive the agent's own command limits; they run detached with their own logs and exit-code
  files (`winrun --detach`, `macrun.py --detach`) and are polled, never left running unobserved (4.1 to 4.3).
- **The GitHub-only parts** (6.5) fail first on the PRs; fix rounds there cost CI runs, not the owner's time.
- **`appveyor.yml` carries reicast's old AWS access key id**; deleting the file leaves it in the public history. It is
  reicast's, probably long revoked; the owner is told in the emulator PR's text.
- **CRLF** is closed by PL-D16 only for checkouts made after RB and VCu; an older Windows clone keeps its CRLF files
  until it is made again.
- **Memory on the Windows host**: MSVC at `-j8` on the JSON and embed translation units while this VM holds 15 GB; W64a
  watches the first build and lowers `-j` for those units if needed.
- **The embedded UI's size under MSVC and AppleClang**: the embed file is about 6.7 MB of initialisers; the `try` builds
  of W64a and MACa (PL-D72) are the first to compile it there and report its time and memory, before CI does.

---

## 9. What every brief carries

**This section replaces `M3-ROUND.md` section 9 (and `M2-ROUND.md` Appendix A.0) for this round.** The package workflow
takes the rules as its argument `rules`; every package of this round is started with `rules` naming this section of this
file; where a prompt or a document still names A.0 or M3's section 9, read this section. Every emulator switch has two
dashes (`--config config:Headless=yes`).

What every brief carries (in its "What is known"):

- **Where you work.** Emulator: your own worktree `agents/nulldc3/wt/<key lower case>`, branch `skmp/nulldc3-<key lower
  case>` of `devcast`, cut from `skmp/nulldc3` as you find it, and your own out-of-tree build directories under
  `agents/nulldc3/<key lower case>/`. UI: the branch `skmp/nulldc3` of `dreamcast-devtools` itself. Commit as work
  lands; push nothing; never tag a shared repository (tags are shared by every worktree); nothing is committed in a
  clone on the Windows host or the Mac (a change is made and committed in your worktree and sent again). Reports and
  scratch to `agents/nulldc3/<key lower case>/`. Delete your build trees (here, and `b`, `src`, `tmp`, `run` in the
  remote work folders) before you return; keep logs and scripts. (The integrator's steps of section 7 are not a
  package's and are not bound by this item: PL-D84.)
- **Commits.** Author and committer are the configured identity (do not override it). Every message ends with the two
  lines `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and `Claude-Session:
  https://claude.ai/code/session_01MRfuVpVTnR8CCVUvBeMAV5`. No absolute home path, host name, user name, session temp
  path or LAN address in a committed file or a message.
- **Renames with `git mv`.** A file moves with `git mv`; a moved file's content changes in a later commit than its move;
  `git diff -M --name-status` of a move commit shows `R100` for every moved file. A deleted file is deleted with `git
  rm`, in a commit that says why.
- **This machine.** One heavy job at a time (this machine and the Windows host are one computer): `ninja -j8`, `ctest
  -j4`, at most four emulator processes, one sanitizer configuration at a time; check `free -m` before heavy steps.
  Gates that take more than five minutes run on a subset while you iterate and in full once at the end.
- **Python.** Every Python file the round adds or changes runs on Python 3.9 with the standard library only (PL-D74): no
  `match`, no `X | Y` types evaluated at run time, no `zip(strict=)`, nothing of 3.10 or later, no PyYAML. **Every
  package that adds or changes a Python file checks each such file in its gate with `ast.parse(source,
  feature_version=(3, 9))`** (in the report: the files and the result), so a construct of 3.10 or later is caught in the
  writer's own turn. The packages that work on the Mac run each such script with the Mac's `python3` in their gates;
  those on the Windows host with its Python 3.11.
- **No windows, no dialogs, no sound.** Every emulator run is headless and carries P0's keys (`--config
  config:Headless=yes`, `audio:backend=none`, `config:Dreamcast.RTC=946684800`; PL-D71). On the Windows host every
  program built in the round runs inside `nulldc3-winrun.exe` (ctest included), a long step through `winrun --detach`;
  on the Mac every emulator run goes through `tests/macos/macrun.py` under the sandbox profile, a long step through
  `macrun.py --detach` (inside it, `macrun.py` finds its caller sandboxed and runs under the inherited profile: PL-D69);
  the bundle's inner executable by path, never `open`. No emulator runs on a system before its headless mode exists and
  is proven not to open a window (the cut's 4.3): before W64a's proof nothing but builds and scripts runs on the Windows
  host, before MACa's nothing but builds and scripts on the Mac. **No mutation or test turns off `winrun`'s error mode
  or job flags, reaches AppKit, or leaves a crash report by design** (PL-D70): such a mutant is judged by reading. No
  `Debugger:Bind` other than loopback anywhere.
- **The remote systems** (the cut's section 4). You reach them through the access details in the `paths` of your prompt.
  **The host-detail rule**: the remote systems' details — the Mac's ssh alias, the Mac's and the Windows host's user
  names, host names and home paths — are never written into a repository file, a commit, a PR text, a CI file, a report,
  a log or a document under `agents/nulldc3/`; scripts read them from environment variables; texts say "the Windows
  host" and "the Mac"; remote output reaches a file here only through the scrubber of 4.3 (user names in path form and
  `<u>@<host>`, never a bare name; host names as whole words, in any letter case), and your package directory is scanned
  with **the remote patterns** of PL-D61 before you return, its hits sorted as 4.3 says (PL-D73): this machine's host
  name is the Windows host's, and 2ndmix and hello print it in their KOS build banner
  (`<u>@<host>:/opt/toolchains/dc/kos`; KallistiOS guests built here: `<u>@<host>.localdomain:...`), so every local log
  of their runs carries it; those lines are reported as `KOS banner: N lines in M files`, a host name inside a longer
  word as `inside a word: N lines`; neither is a failure, and the scan is clean when no other hit remains. This
  machine's own paths in your local logs stay, as in M1 to M3; what you commit carries none, and no committed file or PR
  text quotes the banner (the full patterns, PL-D61). Work only inside `C:\nulldc3-work` and `~/nulldc3-work`; change
  nothing outside them; kill only processes your run started (by handle or recorded PID; a sweep only of executables
  under the work folder); never `taskkill /IM`, `pkill` by name, `sudo`, `defaults write`, `launchctl`, `open`, `start`,
  `Start-Process`. The UI clone there is `src\ui` (`src/ui`), never `dreamcast-devtools`, and the emulator clone's
  submodule `devtools` is initialised from it (4.1, 4.2; PL-D80); the UI checkout the CI scripts take (`<ui>`, PL-D85)
  is that clone there and `agents/nulldc3/wt/ui-pin` here, never your worktree's empty `devtools/`. List the
  crash-report places (Windows: `%LOCALAPPDATA%\CrashDumps` and the WER `ReportQueue` and `ReportArchive`; the Mac:
  `~/Library/Logs/DiagnosticReports`; names only) before and after your gate and report every new entry, with the
  program it belongs to, whatever program it names (there must be none of ours).
- **A gate is green when its tests ran.** On Linux: the tests project with `-DNULLDC_ELF_DIR`, `-DNULLDC_DEVTOOLS_DIR`
  (the UI pin `agents/nulldc3/wt/ui-pin`), `-DNULLDC_WREN7_DIR`, `-DNULLDC_CAIQUE_DIR`, `-DNULLDC_SHRIKE4_DIR`, the two
  vector submodules (or `-DNULLDC_SH4_TESTS_DIR`, `-DNULLDC_ARM7_TESTS_DIR`) and `-DNULLDC_TESTS_REQUIRE_DATA=ON`; "all
  ctest targets green" means twice, none skipped, the number in your report, and `git status` empty behind them. On the
  Windows host and the Mac: the same data copied into the work folder (subsets, never whole trees), every target that
  runs green twice, the skipped listed with reasons and their numbers.
- **The five builds** of a Linux gate: the default, `-DNULLDC_DEBUGGER=OFF`, `-DTARGET_NO_REC=ON`, `-DHAS_TA_LLE=OFF`,
  the generic host (an unknown `CMAKE_SYSTEM_PROCESSOR`, as `agents/nulldc3/p-desktop/` has it). On the Windows host and
  the Mac: the default, `-DNULLDC_DEBUGGER=OFF` and `-DTARGET_NO_REC=ON`.
- **The UI bundle is the one look-up that has to miss**: gate emulators embed none, and on the Windows host and the Mac
  they are configured with `-DNULLDC_DEVTOOLS_DIST=OFF`; the UI pin is a detached worktree, never a directory named
  `dreamcast-devtools` beside your worktree or your remote clone; an emulator that must carry the UI (PLG, the owner's
  `try` build of PL-D72, CIe's release files) is configured with an explicit `-DNULLDC_DEVTOOLS_DIST=<dir>/dist-native`.
- **Time neutrality and Linux behaviour.** The state hashes and sound lines of P0 (taken by RB under
  `agents/nulldc3/rb/p0/`) are identical at your head, compared exactly. Linux behaviour changes only as the cut's 1.4
  allows. Where you change a flag for one compiler or system, show that the Linux compile commands are unchanged but for
  the files you edit (`ninja -t commands` against the base).
- **Ownership.** You edit what your brief says you own and nothing else. W64a, MACa and LA64 may also make the
  compile-only portability fixes of PL-D68 that their compiler needs, each listed; W64a and MACa also the runtime fixes
  of PL-D68 in `tests/rebrand/**`, `tests/icons/**`, `make_icons.py`'s file input and output and `tests/version/**`
  where their system is the first to run them, each listed as file, failure and change. A package that adds, moves,
  rewraps or deletes a piece of reicast history edits its rows of `tests/rebrand/allowed.txt` (PL-D79). The frozen
  headers are those of design Appendix B; the only frozen text that changes in this round is the cut's 2.3 (H8-1 to
  H8-4), by RB and VTe. If you need another file or a frozen declaration, stop and report: the integrator decides.
- **CI files.** Workflows and CI scripts name no host, user, home path or LAN address; they use the runner's variables;
  no step prints the environment; actions are pinned by full commit SHA; only the job `release` has `contents: write`,
  and it runs only on tag pushes (`if: github.event_name == 'push' && github.ref_type == 'tag'`) and never needs
  `release-check`; the release scripts are called by explicit invocations, `publish` from `release` alone (6.1).
  Workflows are checked with the pinned `actionlint` in `agents/nulldc3/tools/` and by CIe's rule check; a workflow is
  never run on GitHub by a package (no push, no PR, no dispatch).
- **Versions.** Until the round's tag exists every build says `3.0.0-pre+g<h>`; the cases on a tag are tested in scratch
  repositories only; a scratch shallow clone is made through a `file://` URL, and a scratch submodule with `-c
  protocol.file.allow=always`.
- Where this file and `TRACK1-DESIGN.md` or `PLAN.md` differ, this file is the contract.
- **Your report**: what was delivered against each item; every departure from the cut and why; the commands and results
  of the gate on each system (counts, skip lists, hashes, durations); what is open; what the next packages need to know.
  A verifier who did not write the package checks it, with mutations; expect fix rounds.

---

## 10. The briefs

One brief per package, a file of its own under `agents/nulldc3/pl-briefs/`, for the orchestrator to hand to its
engineer: `VCu-BRIEF.md`, `RB-BRIEF.md`, `IC-BRIEF.md`, `VTe-BRIEF.md`, `W64a-BRIEF.md`, `W64b-BRIEF.md`,
`MACa-BRIEF.md`, `MACb-BRIEF.md`, `LA64-BRIEF.md`, `CIe-BRIEF.md`, `PLG-BRIEF.md`. Each has the sections the package
workflow reads (a title line and the state it starts from; "What is known"; "What <KEY> delivers", numbered; "Owns";
"Gate"; "Verifier"), is complete enough to work from without this file, and names the sections of this file that are
part of it. A verifier's brief is the "Verifier" section of its package with the gate above it.

---

## Appendix D. The design text and the plan, at PLG

`TRACK1-DESIGN.md` and `PLAN.md` are not edited before PLG. PLG brings them to the round, row by row, and copies them
into `docs/nulldc3/`, with this file as `docs/nulldc3/platform-round.md` (checked by `ci.hygiene` and by PLG's run of
the full patterns of PL-D61). `docs/nulldc3/**` is allowed by `rebrand.names` as a whole path, and PLG adds the rows of
`nulldc3/ci/hygiene-allow.txt` its documents need (path, pattern, reason; PL-D79). A copy names no remote path and no
owner-specific detail; this file describes the WSL paths in words for that reason.

| # | Where | Becomes |
| --- | --- | --- |
| 1 | design D7 (targets) | the release targets of 1.3 item 3; arm64 built on Linux and macOS since this round, with the debugger's fallback engine for the SH4 and no ARM7 recompiler (A64) |
| 2 | design D10, 5 | headless exists on Linux, Windows and macOS (W64a, MACa), with the no-dialog policies of PL-D34 and PL-D45 and the fatal line of PL-D35; every program of an automated run on Windows goes through `winrun`, every emulator run on the Mac through `macrun.py` (PL-D69) |
| 3 | design 2.3.5, 2.3.7, 5.5 (S04), the contract notes | x64 on Windows sets `DebugChecks` (W64b): the fallback-on-Windows text goes; arm64's known limits as MACb found them; S04's arm64 rule (PL-D78: on a JIT that restarts its slice counter, with or without `--fallback`, S04 is recorded and may fail only on `SH4_CYCLES` moved by less than a slice; the rule held by `contract.py`'s `run_runner`) in S04's row and in the runner's backlog |
| 4 | design 3.1 | `emulator.version` is the build's version (5.1), not "3.0.0" |
| 5 | design 2.11, 2.9 | the paused title and the page without a bundle carry the version |
| 6 | design 5.4 to 5.6 | the harnesses' run helper; the gates on Windows and macOS with their skip lists (PL-D5); hashes per host |
| 7 | design 5.8 | "Windows" as built: the MSVC build, the static CRT, the ABI fixes, the gates passed on the Windows host; the syntax-only check is history |
| 8 | design 6.3 | rows VCu to PLG; W64 done as W64a and W64b; A64 gains the ARM7 recompiler and the arm64 native checks |
| 9 | design Appendix B | row 8: the comment-only revision of this cut's 2.3 |
| 10 | design 2411, 2842 (the UI's version) | the rule of 5.1 |
| 11 | `PLAN.md`, ground rules and targets | "the tree is not mass-renamed" goes (RB); the release targets; CI on PRs and tags; the tags |
| 12 | `PLAN.md`, Rebrand | the directory `nulldc3/`; reicast as history; what still said reicast at N1 is renamed |
| 13 | `PLAN.md`, backlog | A64 (arm64 native checks, the ARM7 recompiler); macOS x86_64 unbuilt; CI data (PL-Q1); the macOS icon margin (PL-Q2, if open); runner paths in CI logs (PL-Q3, if open); the Darwin sent-signal rule; `reidbg/` and the `refsw-dbg` server, kept or removed together (PL-D14); the by-hand list of PL-D40; what the round's verifiers leave |
| 14 | `DESIGN-V3-OPEN.md` | the arm64 host question answered: the Mac and CI |
| 15 | `docs/nulldc3/automated-runs.md` | as the packages left it, checked against the round |
| 16 | design 65 (D4, naming), 666-670 (`dbg::Init` / `dbg::Term` at the end of `reicast_init()` and the start of `reicast_term()`), 4044 (the sanitizer recipe's `reicast/cmake/config.cmake`) | D4 says the tree was renamed by this round (PL-D9's names; reicast as history); `nulldc_init()` / `nulldc_term()`; `nulldc3/cmake/config.cmake`, and that it now appends to MSVC's defaults (PL-D32) |

---

## Appendix I. The integrator's notes, by package

Empty at the cut. As each package is verified and merged, the integrator adds a section here (as `M3-ROUND.md` Appendix
I has one per package): the commit verified and merged, the counts, what the verifiers left for later packages, the
departures accepted, the known limits. A later brief is read with these.


### I.1 VCu (verified 2026-10-19)

- **Verified and merged:** the UI's `skmp/nulldc3` = `7b88410` (10 commits on `3cf8b15`; 10 files, +1869 / -40); the
  first verification failed (one must-fix: the default git runner went through a shell; three should-fix: the release
  files' size and digest check, checkouts keeping the token and `npm ci` running dependencies' install scripts in
  `release` / `release-check`, the version helper's environment under a git hook), the second passed with notes only.
- **Counts at the head:** `npm run check` exit 0 (41 files, 793 passed, 1 skipped; the contract runs 18 / 18 / 2 / 1);
  the unit suite three times; e2e 401 passed, 1 skipped (the smoke spec); the version cases 16 of 16 (fresh, shallow
  `--depth 1`, a submodule, a scratch annotated tag `3.0.0-pre`, dirty on the tag `3.0.0-pre+dirty`, two after
  `3.0.0-pre+2.g<10 hex>`, a lightweight `v3.0.1`, no `.git`). The bundle built at `7b88410` says
  `3.0.0-pre+g7b88410915`.
- **The integrator after it:** dist-native rebuilt at `7b88410`; `wt/ui-pin` moved there; devcast `skmp/nulldc3` =
  `f5d390aa` "devtools: the submodule pins 7b88410" (section 3's pin move before RB); the full ctest
  with `wt/ui-pin` at `7b88410`: 307 of 307 passed.
- **Left for later (notes of the second verification, no package owns them yet):** `gitFacts` is not held to
  `gitEnvironment`'s filter by a test (mutation N3: a build run from a git hook would take the hook repository's
  version); `publish`'s size check takes a longer asset with no digest (mutation N2); no unit case pins the zip entries'
  1980-01-01 time or that `v3.0.0-pre-2` is no tag (O14, O23); `e2e/support/globalSetup.ts` `BUILD_INPUTS` does not list
  `scripts/version.ts` (a change to it alone does not rebuild dist-native for e2e) and the `BundleInfo.version` comment in
  `scripts/bundle-info.ts` still says `+<git describe>`. For a later UI package; none blocks the round.

### I.2 RB (verified 2026-10-20)

- **Verified and merged:** `skmp/nulldc3-rb` = `c893e925` (30 commits on `f5d390aa`; 129 files, +1014 / -6327), merged
  with a merge commit as `60bce5b7` (the tree equal to the verified head), so the pure move `7faab69f` (55 renames at
  R100 plus the three path lines that keep it building) stays in the history `git log --follow` reads. The first
  verification failed on one should-fix: `rebrand.names` searched submodules when a host sets `submodule.recurse`
  (fixed: `-c submodule.recurse=false` in the script, exercised on every ctest run through the target's environment).
- **Counts at the head:** five builds (default, nodbg, norec, hle, generic; warnings 1400 / 1400 / 1400 / 1400 / 1399 as
  before); 308 ctest targets (307 + `rebrand.names`) green twice, none skipped; P0 38 of 38 rows equal to the base,
  sound lines identical (default and nodbg); every one of the 30 commits builds; the Mac's item 9 configure (no build,
  no run) rc 0 with the six `nulldc3/apple` sources; the package-directory scan clean (0 banner, 0 inside a word, 0
  other); 0 hits of the full patterns in what is committed. The integrator's full ctest after the merge (`integ-f4a`, `wt/ui-pin` at `7b88410`): 308 of 308 passed.
- **Non-ASCII in what RB added:** the copyright sign in `Info.plist`'s copyright line for nullDC Team and in four rows
  of `tests/rebrand/allowed.txt` that quote existing macOS file headers: accepted (no frozen header is involved).
- **Left for later (passed on as "Left by RB" bullets):** VTe: `nulldc3/cmake/config.cmake` line 5, reicast's TODO about
  a git version and `--version`. Every package that moves, rewraps or deletes an allowed history line changes its
  `allowed.txt` row in the same commit (MACa about 17 rows for the macOS headers; CIe the `main.cpp:199` row). W64a and
  MACa: `rebrand.names` sets `GIT_CONFIG_COUNT` in its test environment, replacing a host's injected git settings. P0
  for later packages: `agents/nulldc3/rb/p0/`.
- **Cut feedback (for the method):** the round-2 verifier prompt names "part B" and "part D", which the briefs do not
  define (read as the Gate and the Verifier's mutation list); whether a fix round may reuse round 1's P0 when the fix
  touches no emulator source is not said (it did).

### I.3 IC (verified 2026-10-20)

- **Verified and merged:** `skmp/nulldc3-ic` = `01b3ca10` (7 commits on `60bce5b7`; 28 files, +3957 / -2), merged with a
  merge commit (the tree equal to the verified head); passed on its first verification.
- **Counts at the head:** five builds (warnings as RB); 310 ctest targets (308 + `icons.generated` + `icons.x11`) green
  twice, none skipped; P0 38 of 38 equal, sound lines identical; the generator deterministic (two runs, outputs deleted
  between, equal sha256) and `--check` writing nothing (strace); its PNG decode equal to PIL's on all 11 PNGs (every row
  filter used), the header's 24,652 values equal to values built from PIL; the `.ico` (7 entries) and `.icns` (10
  elements) read by ImageMagick and PIL; the X11 calls in order (class hint, icon property, then the map) in the built
  binary. Merged as `c2f8ce03`; the integrator's full ctest after the merge: 310 of 310 passed.
- **Left for later:** W64a: the `.ico`'s PNG entries below 256 px are not yet proven against `rc.exe` / Explorer. MACa:
  the `.icns` format lives in `build_icns`, `ICNS_ELEMENTS` and `parse_icns` together. Optional follow-ups (no package
  owns them; PLG lists them): `icons.x11` checks the header against the generator and its own hash only, so a generator
  change made together with new outputs (premultiplied pixels, ABGR order, bottom-up rows, an `.ico` without 24 px) passes
  both targets; a few exact values would pin the pixel format (16 px top-left `0x01000000`, bottom-left `0x0c151515`, row
  3 x 7 `0xff76503b`), and literal tables in `parse_ico` / `parse_icns`. `x11.cpp` includes the 300 KB data header only
  for the declaration, so an `-O0` build carries the array twice (98 KB; RelWithDebInfo is unaffected). Builds leave the
  ignored `libswirl/version.h` in a worktree (VTe's area).
- **Cut feedback:** the brief's note on the 16 px corners was wrong (top two `(0,0,0,1)`, bottom two `(21,21,21,12)`; R67
  had it right); "one byte of a PNG changed" is caught by the chunk CRC before any comparison, its strong form is a
  re-encoded PNG with one pixel changed; the Paeth `pa <= pb` tie is an equivalent mutant.

### I.4 VTe (verified 2026-10-20)

- **Verified and merged:** `skmp/nulldc3-vte` = `e5a4fd2f` (13 commits on `c2f8ce03`; 41 files, +1529 / -292), merged
  with a merge commit (the tree equal to the verified head). The first verification failed (version.derive's untracked
  cases had no untracked files; nothing caught a version made only at configure time; `--version`'s line was not held
  exact; a Windows message box would show the help text after `--version`); the second passed with notes.
- **Counts at the head:** five builds (warnings as before), each `--version` printing `nullDC 3.0.0-pre+ge5a4fd2f34`;
  314 ctest targets green twice, none skipped; P0 38 of 38 equal, sound lines identical; the standalone
  `tests/debugger/net` project 15 of 15; incremental builds: nothing changed runs only the version step, a new commit
  recompiles `version.cpp` alone; a scratch tag gives `3.0.0-pre`, `+dirty`, `+1.g<h>`; `debugger.describe`'s
  `emulator.version` equal to `--version`; header revision 8's H8-4 exact and ASCII. Merged as `2ac231ac`; the integrator's full
  ctest after the merge: 314 of 314 passed.
- **Left for later (as "Left by VTe" bullets, with the files added to those packages' Owns):** `--version` on Linux and
  macOS prints three setup lines on stdout before the version (`common_linux_setup()` runs before the command line is
  read): CIe for Linux (`linux-dist/main.cpp`, then `setup_lines` in `tests/version/binary.cmake`, `window_checklist.sh`'s
  `tail -n 1`, `automated-runs.md`'s wording), MACa for the Mac. W64a: `winmain.cpp` shows `cl_FinishText()`,
  `cfg.h` declares it, `cl_test.cpp`'s stand-in goes; `nulldc3.rc` includes the generated `nulldc3_version.h` from the
  build directory. MACa: `types.h`'s stale comment. PLG: the design text's `version.h` line.
- **Cut feedback:** the brief named `version.binary` as the catcher of a configure-time version, which it cannot be in the
  gate's configuration (the tests project configures its emulator at every build): `version.build_time` is. P0's five
  builds are not bit-reproducible (two clean rebuilds differ by about 8 MB), so binary sha256 sums cannot show two builds
  came from the same source.

### I.5 W64a (verified 2026-10-20)

- **Verified and merged:** `skmp/nulldc3-w64a` = `eb29a4a8` (23 commits on `2ac231ac`; 49 files, +3265 / -509), merged
  with a merge commit as `7543be6b` (the tree equal to the verified head); the integrator's comment fix after it (the
  comment above `MAINLOOP_FRAME` named `win_vmem.cpp` for the unwind data, now `win_jit_unwind.cpp`). The first
  verification failed (a README table, stderr in text mode, `nulldc3-winrun`'s handle inheritance, a hang when
  DirectSound fails, RunBlock's unwind; the Windows crash policy of `test_guard.cpp` and four commits outside Owns for
  the integrator); the second passed with notes.
- **Counts:** Linux: five builds, 314 ctest targets green twice, P0 38 of 38 equal, compile commands of the default build
  (398) and the tests project (269) unchanged. The Windows host: the probe on its private desktop (125), three builds
  (default, nodbg, norec; peak about 1.3 GB), `--version` `nullDC 3.0.0-pre+geb29a4a87f`, the headless report `windows=0
  errmode=8003 wer=20 invparam=1 purecall=1 sigabrt=1 abort=0` on each, 16 runs whose state hashes equal Linux's in every
  field (only the guest's `emu_s` differs in its last digit, not a hash part), the fatal kinds with their codes and no
  window, the version resource (nullDC Team) and the icon group (7 PNG entries), no runtime DLL; the tests project
  twice: 330 targets, 107 passed, 218 skipped, 5 failed (the harness-script tests, W64b's), every `windows.*` and C++
  suite green.
- **Accepted outside Owns (integrator's decision):** `a8153ff0` (test_guard's `longjmp` on MSVC x64: the frame zeroed so
  it does not unwind through generated code), `0e2c8468` (`fixNaN` reads the SH4's NaN at run time under MSVC, which
  folded the signalling-NaN bits into a quiet constant: every NaN result of the interpreter was wrong in an MSVC build),
  `01bc109e` (debugger.net on Windows: a 1 ms timer in `test_bounds`, the embed check's file size), and the comment edit
  in `tests/common/sh4_stubs_rec.cpp` in `79243c5b`; each guarded so that GCC and Clang see the same code.
- **Left for later (as "Left by W64a" bullets in W64b's brief):** the Windows crash policy of the C++ suites
  (`test_guard.cpp`); the check stub's unwind description when `DebugChecks` goes on; the five harness tests; the host
  state (`try\nulldc3.exe`). W64a's first round left six minidumps and six WER `AppCrash_*` folders outside
  `C:\nulldc3-work` (two test programs that crashed before `a8153ff0`); deleted by the integrator on the owner's word (2026-10-21).
- **Cut feedback:** the Gate did not say how to count the harness tests that fail until W64b; a name-only `%TEMP%`
  listing cannot attribute files on a desktop in use.

### I.6 W64b (verified 2026-10-20), and the package W64c it leads to

- **Verified and merged:** `skmp/nulldc3-w64b` = `f2b234a9` (23 commits on `7822d7e8`; 29 files, +2514 / -226; libswirl:
  2 files, the check stub on Windows and hostcycles), merged with a merge commit (the tree equal to the verified head).
  The first verification failed (three must-fix: a non-emulator child started without `nulldc3-winrun`, bash resolved on
  Windows, `debugger.disasm.dsptest` unbounded on Windows; three should-fix: the guard test's coverage, tools without the
  crash policy, TEMP and npm's cache in the profile; the four `emu` / `emurun.pauses` selftests); the second passed with
  one note (mutation N3 survives). Merged as `fb5049b5`; the integrator's full ctest after it: 317 of 317 passed.
- **Counts:** Linux: 317 ctest targets green twice, P0 38 of 38 equal, contract result counts identical. The Windows
  host: 335 targets green twice inside an outer winrun (257 run, 74 platform skips, 4 open defects); the contract gates
  M1 18, M2 19, M2's guest run 2, M3 20, M3's DSP runs 3 and 2 on every engine, the same numbers as Linux; the JIT runs
  `sh4-debug-native`; the crash places 551 before and after (no new entry); TEMP and npm's cache sentinels empty.
- **Accepted outside the literal Owns (integrator's decision):** `tests/common/win_policy.cpp` (new), the policy lines of
  `tests/common/CMakeLists.txt`, `guard_windows.cmake`, `tests/aica_dsp/CMakeLists.txt` (`aica_dsp_guest_oracle`) and
  `tests/debugger/disasm/CMakeLists.txt` (the winrun wrap, the oracles' timeout, the policy): tests only, the one policy
  W64a's handover asked for.
- **Left open, and given to a package of its own, W64c** (`pl-briefs/W64c-BRIEF.md`, written by the integrator; the
  owner asked that nullDC works on Windows with its self-tests run there): the four selftests' sub-millisecond naps on
  Windows (the fix in the emulator's selftest files, not in the emulator: its one such nap, `dbg_emu.cpp` line 993, is
  on a contended path only); `dsp_playground`'s CR LF on Windows; the crash policy in the remaining test programs; the
  test that mutation N3 lacks. W64c runs before MACa.
- **For PLG:** design 2.3.5 and 5.8 ("Windows uses the fallback") are out of date: the JIT reports `sh4-debug-native` on
  Windows. `try\nulldc3.exe` is W64b's Release build of `f2b234a9` (it serves the UI on loopback).
- **Cut feedback:** the cut has no category for an open defect of a gate (W64b registered them as ctest skips with
  `OPEN DEFECT, NOT RUN`, so a summary reads "100 % passed"); doing what W64a's handover asked needed files beyond the
  literal Owns; the brief named no test for the tools' TEMP and npm cache.

### I.7 W64c (verified 2026-10-21; a package the integrator added after W64b)

- **Verified and merged:** `skmp/nulldc3-w64c` = `1d413b3c` (9 commits on `fb5049b5`; 28 files, +852 / -94), merged with
  a merge commit (the tree equal to the verified head). The first verification failed (must-fix: the POSIX branch of
  the nap was not held to `sleep_for`; should-fix: `dsp_playground` in a build of its directory by itself ran outside
  `nulldc3-winrun`); the second passed with notes. Accepted outside the literal Owns: `8b045e49` (the Windows policy lines
  of `tests/sh4_directed/CMakeLists.txt` and `tests/icons/CMakeLists.txt`, without which item 4's check is red). Merged as
  `963353d9`; the integrator's full ctest after it: 318 of 318 passed.
- **Counts:** Linux: 318 ctest targets green twice, P0 38 of 38 equal, the compile commands unchanged but for
  `dbg_selftest_nap.cpp`, the four selftests' durations equal to the base's (emu 11.0 s, emu.interp 10.9 s,
  emurun.pauses 2.0 s, .interp 4.0 s). The Windows host: 340 targets green twice (73 platform skips, no open defect;
  emu 16 to 17 s, emu.interp 17 to 21 s, emurun.pauses 3.0 s, .interp 5.0 s, dsp_playground 2 s), the contract gates'
  counts as W64b's, the crash places 551 before and after, the TEMP and npm sentinels empty.
- **Left for later (no package owns them; PLG lists them):** `AsSleepFor` compares the POSIX nap with `sleep_for` at
  five requests only (millisecond-scale and odd requests would pin the rest); the `dsp_playground` skip is registered
  whenever `disasm_winrun` is empty, which could turn a lost winrun line in the tests project into a skip instead of a
  failure; in a build of `tests/debugger/disasm` by itself on Windows without `NULLDC_WINRUN`, `dsptest` runs tsx outside
  a winrun (from W64b). `debugger.disasm.standalone.configure` needs the VS developer environment (the README recipe has
  it). `try\nulldc3.exe` is W64c's Release build of `1d413b3c`.

### I.8 MACa (verified 2026-10-21)

- **Verified and merged:** `skmp/nulldc3-maca` = `ad179a62` (19 commits on `963353d9`; 36 files, +2201 / -304), merged
  with a merge commit (the tree equal to the verified head; the R100 rename of `Info.plist` to `Info.plist.in`,
  `93f0de86`, kept). The first verification failed (the Darwin fault handler kept a pointer to the fault's registers;
  `--version` printed the paging line on stdout; stdout not line-buffered before a fault; `macrun.py` left processes in a
  session of their own; Homebrew's ccache used by default and crashing); the second passed with notes. Accepted: the
  Darwin branch of `libswirl/linux/context.cpp` (`aba0983b`; the cut's 2.2 row gives Darwin defects to MACb, the Owns
  gave MACa its Darwin branches) and the Apple-guarded ccache lines at the top of `tests/CMakeLists.txt` (more than its
  "one line"). The non-ASCII copyright sign in the bundle's copyright string, as RB's.
- **Counts:** Linux: five builds, 318 ctest targets green twice, P0 38 of 38 equal, compile commands of the emulator
  (399) and the tests project (269) unchanged. The Mac (macOS 26.4.1, AppleClang 21): the canary and the probe first
  (window server denied), three bundles (default 29 s, nodbg 22 s, norec 29 s), each `3.0.0-pre+gad179a6280`, codesign
  valid, four or five licences as the embed asks; `--version` exactly one line; headless `app=0 windows=0`; the state
  hashes twice equal (interpreter equal to Linux's; arm64 JIT guest `c9bb6e60601ab606`, 2ndmix `6ab1933550d37163`, not
  gated: MACb); every `macos.*` and `version.*` target green; the tests project 104 passed, 222 skipped, 2 failed (both
  MACb's files); no new crash report in round 2. Merged as `af2acc83`; the integrator's full ctest after it: 318 of
  318 passed.
- **Left for MACb (as "Left by MACa" bullets):** gate the arm64 JIT hashes (a mutation of the fault context's save half
  changes only them); the two failing tests; the ARM7 recompiler off on macOS; the first-run order as separate ctest
  calls; optional gaps in `macrun.py`'s signal path and `macos.build.ccache`.
- **On the owner's Mac outside `~/nulldc3-work`:** five `ccache-*.ips` crash reports from round 1 in
  `~/Library/Logs/DiagnosticReports` (Homebrew's ccache crashed; deleted by the integrator on the owner's word, 2026-10-21); round 2's verifier opened
  a per-user temp database of another program read-only (`sqlite3 -readonly`) while attributing temp entries, which
  touched the mtime of its `-shm` file; nothing else outside the work folder changed.
- **Cut feedback:** on macOS 26.4 applying the same sandbox profile again inside itself succeeds (only a different
  profile is refused), so the brief's mutation is caught by the canary's caller check, not by `sandbox_apply` errors.

### I.9 MACb (verified 2026-10-22), the owner's decisions on the Mac, and the arm64 host

- **Verified and merged:** `skmp/nulldc3-macb` = `47878491` (11 commits on `af2acc83`; 20 files, +1258 / -87), merged
  with a merge commit as `7c87d517`; the integrator's full ctest after it: 319 of 319 passed. The first verification
  failed (S04's arm64 rule could be widened to another field of `dc.timing`; a hung hash run outlived its test); the
  second passed with notes. Accepted outside the literal Owns: the macOS branch of the selftests' nap,
  `libswirl/debugger/dbg_selftest_nap.{h,cpp}` (`6e7f0481`: a kqueue timer, since macOS coalesces short sleeps to about
  1.5 ms; `debugger.selftest.nap` green).
- **Counts:** Linux: 319 targets green twice, P0 38 of 38 equal, compile commands unchanged. The Mac: three bundles,
  the canary first, `macos.hashes.jit` green against MACa's references; the tests project twice: 212 passed, 105 platform
  skips, 13 failed (the arm64 recompiler's debugger: `debugger.selftest.emu`, `.regs`, `.core`, `.hostcycles`,
  `.writesrun.neutral` and eight `debugger.sound.neutral.*.jit`, all green on the interpreter); the contract gates M1,
  M2, M2's guest run, M3 and M3's two DSP runs green on the interpreter and, with S04 recorded (PL-D78, 446 cycles back
  in a step), on the recompiler and the fallback; no new crash report.
- **The owner's decisions (2026-10-22):** (1) the 13 targets are known limits of the arm64 recompiler's debugger for
  3.0.0-pre, registered on every arm64 host (`KNOWN LIMIT (A64)`; given to LA64), and fixed by a later package A64; (2) a
  crash of `nulldc3.app` found on the Mac (`EXC_BREAKPOINT` in `os_DebugBreak()` from the arm64 recompiler's code, after
  "Boot BIOS" with no BIOS copied in: the owner's run) is fixed after the round; (3) an arm64 Linux machine, "the arm64
  host", is available: LA64's brief is rewritten (revision 4) to build and test natively there with the Mac's test
  depth, the cross build under qemu kept as a fallback; its access details are in the private `env.sh` and both pattern
  sets.
- **Left for later:** `debugger.net.test_e2e` flakes on the Mac's loopback (CIe and PLG told); optional test gaps from
  MACa (`macrun.py`'s signal path, its `--detach` rc file) and MACb (S04's went-back in lower case).

### I.10 LA64 (verified 2026-10-22, natively on the arm64 host)

- **Verified and merged:** `skmp/nulldc3-la64` = `59be3df4` (10 commits on `7c87d517`; 12 files, +682 / -59), merged
  with a merge commit as `58273bfa`; the integrator's full ctest after it: 320 of 320 passed. The first verification failed
  on the remote envelope (no core file size limit on the arm64 host, where Ubuntu pipes a crash to apport: 9 nulldc3
  lines in apport's log from the package's early runs, no core file; the outside-the-work-folder listing's blind spots; a
  wrong pid for the kill; a failed copy or feed that did not stop a step); the second passed with notes. The
  `linux_arm64` tests now run with `env -u DISPLAY -u WAYLAND_DISPLAY`, `prlimit --core=1:` and a check of that envelope
  before any emulator starts.
- **Counts:** Linux x86_64 320 targets green twice (`debugger.a64.knownlimits` new on every host), P0 38 of 38 equal,
  compile commands 399 = 399 and 269 = 269. The arm64 host (GCC 13.3, CMake 3.28.3, Ninja 1.11.1): three builds, the
  first run headless with no display; the recompiler's hashes `c9bb6e60601ab606` / `6ab1933550d37163`, equal to the
  Mac's, the interpreter's P0's; the tests project 321 targets twice, 263 passed, 58 skipped (29 sh4bp, 13 known limits,
  9 x64, 3 emcc, 4 vmem tests that skip themselves where `unshare` is refused), 0 failed; the contract gates M1 17/1, M2
  18/1, M3 19/1 on the recompiler and the fallback (S04 recorded by PL-D78's rule), 18, 19, 20 on the interpreter. The
  Mac: 331 targets, 213 passed, 118 skipped, 0 failed; Darwin's assembly equal to the base's but for line numbers.
- **Left for later:** a store-queue write with the MMU off on the arm64 recompiler is reached by no test (mutation o2
  survives; A64); `debugger.contract.m2.guest.interp` failed once in three full runs on the Mac under `ctest -j4` (S16's
  reset not seen within 5000 ms; 3 of 3 alone; CIe and PLG told); what only the `ubuntu-24.04-arm` runner shows (BTI
  and PAC on Neoverse N2, Ubuntu's arm64 SH objdump and its char-signedness, unprivileged user namespaces) is in CIe's
  brief. The owner installed CMake and Ninja on the arm64 host after LA64 started; LA64's fetched copies stay in its
  work folder's `tools`.

### I.11 CIe (verified 2026-10-23)

- **Verified and merged:** `skmp/nulldc3-cie` = `2f936d71` (25 commits on `58273bfa`; 41 files, +3783 / -81), merged
  with a merge commit as `31dafee9`; the integrator's full ctest after it: 327 of 327 passed. The first verification failed
  (`windows.ps1` checked the zip with `tar -t`, which hid Compress-Archive's `\` names; `check_workflows.py` missed an
  environment dump chained after `&&` or `;`; `smoke.py` could leave an emulator running; the Windows tests project's
  build and `macos.sh` off a runner ran built programs outside `winrun` / the sandbox profile; `release_test.py` lacked
  the publish case of a checkout one commit past the tag); each fixed with a test that fails when its fix is reverted
  (`zipcheck.py` and `ci.scripts` new). The second verification passed with notes. The integrator read `ci.yml`'s
  triggers, permissions, `if:` and `needs` against the cut's 6.1.
- **Counts:** Linux 327 targets twice, P0 38 of 38 equal, compile commands unchanged with both options off, `--version`
  one line; the scripts' `ci` / `full` modes: Linux 327/225/102 and 327 passed, the arm64 host `ci` 328/180/148, the
  Windows host 349/190/159 and 349/272/77 (no new crash entry), the Mac 338/141/197 and 338/216/122 (the engineer's
  runs at the head; the verifier's Mac rerun was lost to the Mac sleeping on battery: 13 timeouts in batches of four,
  everything outside ctest green); `appimage.sh` 5 PASS on both Linux systems; the release binary on P0's default rows
  equal.
- **Left for later:** `smoke.py`'s outer `finally` is covered by no case (optional); `linux.sh` passes no SH objdump on
  arm64 (Ubuntu's has the char-signedness bug), so the two `sh4_objdump` targets skip in `linux-arm64`; the v4 actions
  run on Node 20; reported lapses: two local builds at ninja's default parallelism, one local dev-build run of
  `--version` without the headless keys with `DISPLAY` set (dead of SIGPIPE in milliseconds). PLG told (its brief's
  "Left by CIe"), including the Mac's power state.

### I.12 A64 (verified 2026-10-24)

- **Added and ordered** at the owner's word of 2026-10-23 ("before PLG, can you fix the arm64 limitations?"), between
  CIe and PLG (Addendum A64, 2.15, PL-D86). The owner's answers the same day: new arm64 state-hash references accepted;
  both steps before PLG; the Mac on AC power. PLG's first run, stopped for it, is archived (`agents/nulldc3/plg-run1/`).
- **Verified and merged:** `skmp/nulldc3-a64` = `7c025c83` (13 commits on `31dafee9`; 32 files, +1630 / -1000), merged
  with a merge commit as `12069163`. The package workflow (run `wf_a5f51b47-dce`, cut by this PC's restart once and
  resumed) ended after four rounds at `d2ce41d9` with every system green but the Mac: `bm_DiscardAddrPage` discarded the
  block manager's 16 KiB page on Apple silicon against design R4's 4 KiB rule (`debugger.sh4bp.arm` red), in a file
  outside the brief's Owns, and rounds 1 and 2 lost the Mac part to the Mac sleeping on battery. The integrator extended
  the Owns (the brief's addendum of 2026-10-24) to the engineer's two proposed patches (`bm_DiscardAddrPage` keeps its
  host-page index and discards only the blocks overlapping the 4 KiB page; `LoadGuest` steps by 4 KiB, accepted by
  reading); a short workflow (run `wf_adfbe320-6a1`) committed them as `7c025c83`, reran the gates and was verified
  independently with mutants (the 16 KiB discard and a start-only overlap test turn `arm` red on the arm64 host and the
  Mac). The only caller of `bm_DiscardAddrPage` is the debugger's `DebugDiscardPage`; the guest's self-modifying-code
  path (`bm_LockedWrite`) is untouched.
- **Counts at `7c025c83`:** Linux x86_64 327 of 327 (twice in the package, once in the fix, once by its verifier), P0 38
  of 38 equal, compile commands unchanged; the arm64 host 330 / 314 / 16 / 0, the 30 sh4bp targets passed, `linux.sh ci`
  330 / 220 / 110 / 0; the Mac 340 / 259 / 81 / 0 (twice, and once by the verifier), `macos.sh ci` 340 / 180 / 160 / 0;
  the Windows host `full` 349 / 272 / 77 / 0 (the 77 skips CIe's). Contract gates M1 18, M2 19, M3 20 on the recompiler,
  the interpreter and the forced fallback on both arm64 hosts; no `KNOWN LIMIT`, no `S04: recorded` line; `describe`
  says `sh4-debug-native` on the arm64 recompiler. New arm64 recompiler references (PL-D86): guest `6a1e1008459e6ab3`
  (now equal to x86-64's), 2ndmix `56bd7274490f91aa` (were `c9bb6e60601ab606` / `6ab1933550d37163`). No new crash
  entry on any system.
- **Left for later:** the `nodbg` build on the arm64 host measured about 1.3 % slower after A64 (speed_pct medians 663 to
  654; the default build and the Mac unchanged; the owner's "don't mind perf for now" of M3); mutants left green and
  judged by reading (the room a compile keeps for `CheckBlock` and for a pending literal pool, five equivalents); the
  design's 2.3.x f and g wording of the discard (PLG, Appendix D); flakes seen once each and passed on rerun: the arm64
  host's `debugger.selftest.hostcycles` (slow window), the Windows host's `debugger.selftest.emurun.pauses.interp` (its
  `SelfTest:` line spliced into a guest console line); the arm64 host's `ci` mode keeps CIe's `apt-get download` of the
  ARM binutils as the user (no install). PLG told (its brief's "Left by A64").

### I.13 PLG (the round's gate, 2026-10-24; the integrator's checks after item 10 follow below it)

- **The package:** `skmp/nulldc3-plg` on `12069163` (A64's merge), documents only: the design, the plan, the open
  questions and `automated-runs.md` brought to the round (the 16 rows of Appendix D, checked row by row:
  `agents/nulldc3/plg/rows.txt`; rows 1, 3, 8 and 13 as A64 left the tree), the design's 2.3.4 f and g brought to R4's
  4 KiB rule (A64's leftover), this cut copied as `docs/nulldc3/platform-round.md` with its box, PLG's three rows of
  `nulldc3/ci/hygiene-allow.txt` (the quoted patterns of 4.3 and PL-D61 in the copy), the README's line and
  `docs/nulldc3/README.md`, the Windows skip section of `tests/README.md`; the two PR texts under `agents/nulldc3/plg/`
  (`pr-devtools.md`, `pr-nulldc3.md`) and a draft of the release notes' known issues for CIe's template
  (`release-notes-known-issues.md`, not committed). The gate ran on the merged tree `12069163` on the three remote
  systems and on PLG's documents' head here; the targets that read the documents (`rebrand.names`, `ci.hygiene`) ran
  again at the branch's last head.
- **Linux x86_64:** P0 taken again on its commit (`f5d390aa`, from `git archive`, this machine's settings): 38 rows of
  state hashes and 12 sound lines, equal to `rb/p0`; at the documents' head `c4fa9f58` the five builds (default, nodbg,
  norec, hle, generic; 1400 warnings each, 1399 on the generic host, as before), each `--version` one line `nullDC
  3.0.0-pre+gc4fa9f588a`, the 38 rows and the 12 sound lines identical to P0; the UI pin's `dist-native` built again at
  `7b88410` (`bundle-info.json` unchanged); the two emulators that carry the bundle (the default and `-DHAS_TA_LLE=OFF`,
  `-DNULLDC_DEVTOOLS_DIST=<the pin>/dist-native`), whose `debugger.describe` says `ui` `3.0.0-pre+g7b88410915`; the
  tests project with them as `NULLDC_EMULATOR` and `NULLDC_EMULATOR_TA_HLE`, every data path,
  `NULLDC_TESTS_REQUIRE_DATA=ON`: 327 targets green twice (575 s, 560 s), none skipped, `git status` empty behind; in
  them the contract gates M1 18, M2 19, M3 20 on the JIT, the interpreter and the forced fallback (0 partial, 0 not
  applicable), M2's guest run 2 with both notes, M3's run 2 on the guest 3 with `sgc: captured with the arm7 held` and
  `--only S00,S21` 2 with `dsp: stepped with the arm7 held`, on both engines; ThreadSanitizer (397 of 397 compile lines
  with the flag) of the sessions `dsp` and `sgc` and of the contract runs on the guest (interpreter): no report; the UI
  in a fresh clone at `7b88410`: `npm run check` exit 0 (41 files, 793 passed, 1 skipped; the contract runs on the mock
  OK), e2e 401 passed, 1 skipped (two workers); the smoke spec against the headless emulator with the bundle: 1 passed;
  CIe's scripts: `linux.sh build` (the release emulator with the pin's bundle, the prefix map), `appimage.sh` with
  `<ui>` = `wt/ui-pin` (5 PASS: its smoke run's `--version`, the guest at 90 vblanks, its files beside the AppImage,
  `ui`, the five licences of `usr/share/doc/nulldc3/`, each equal to the tree's and the pin's), the AppImage's binary on
  P0's default rows (10 of 10 state hashes equal, the 6 sound lines identical: PL-D83), `linux.sh test ci` 327 tests,
  225 passed, 102 skipped, 0 failed (with Ubuntu's two binutils packages unpacked, as `linux.sh deps` installs them on a
  runner); `git status` empty behind the scripts.
- **The Windows host** (at `12069163`): the crash-report places listed first (CrashDumps, WER's queues and archives: 543
  entries, the owner's and other software's; names only); the tree fed by bundle (the host's clone at `12069163`,
  `src\ui` at `7b88410` with `npm ci`); `nulldc3-winrun` by its bootstrap and the probe as the first program it starts
  (exit 125 on its private desktop); the three builds (Release, 64, 63 and 63 s, `-j8` with 9 GB free on the host, the
  job's peak 1.3 GB), each `--version` `nullDC 3.0.0-pre+g1206916377` through `winrun`; the tests project (85 s),
  `windows.headless.nowindow` first, then ctest twice inside an outer `nulldc3-winrun` (671 s, 657 s): 349 targets, 272
  passed, 77 skipped (the Linux-only harness 67, the four objdump oracles, the three emcc tests, `cfg.log_file`,
  `debugger.sound.runner`, `debugger.sh4bp.b11`), the same set both times; in run 2
  `debugger.selftest.emurun.pauses.interp` failed once (its `SelfTest:` line spliced into a guest console line,
  `DMSelfTest: emurun.pauses: note: ...`: the flake A64 saw) and passed when run again alone, so every target ran green
  twice; the contract gates with Linux's numbers on the JIT (`sh4-debug-native`), the interpreter and the forced
  fallback (M1 18, M2 19, M2's guest run 2, M3 20, M3's DSP runs 3 and 2; 0 partial, 0 not applicable, 0 invariant
  violations; the results of the two runs equal); `hostcycles` with its files kept, 2 of 2; `windows.ps1 -Mode ci` as
  the workflow runs it (`-Jobs 8`): 349 tests, 190 passed, 159 skipped, 0 failed, the zip's entries from its central
  directory (the folder `nulldc3-3.0.0-pre+g1206916377-windows-x86_64/` with `nulldc3.exe`, the five licences and
  `README.txt`, every name with `/`), its smoke run through `winrun` (5 PASS: the zip, `--version`, the guest at 90
  vblanks `6a1e1008459e6ab3`, its files in its `--portable` directory, `debugger.describe`'s `ui`
  `3.0.0-pre+g7b88410915`); the version resource of the unzipped `nulldc3.exe` read back (not run): numeric file and
  product version `3.0.0.0`, `FileVersion` and `ProductVersion` `3.0.0-pre+g1206916377`, `CompanyName` "nullDC Team",
  `LegalCopyright` "Copyright (c) 2026 nullDC Team"; the crash-report places 543 entries before and after, none new (the
  one line `places_diff.py` called new is an entry of the first listing with the next directory's "File Not Found" run
  onto it); no process of the work folder left.
- **The Mac** (at `12069163`, on AC power with `caffeinate -i` around every long step): the DiagnosticReports listed
  first (2 entries, the owner's; names only); the three bundles (Release, 30, 23 and 30 s; each `3.0.0-pre+g1206916377`,
  `3.0.0` / `3.0.0.0` in its plist, codesign valid, the four licences of a bundle without a UI); the tests project (340
  targets); the canary in both shapes (from a ctest that is not sandboxed: the profile applied by `macrun.py`; from a
  ctest under `macrun.py --detach`: under the caller's profile; both sandboxed, the window server's lookup denied and
  unreached); `--version` one line on stdout (the paging line on stderr) and `--help` of the three bundles,
  `macos.refused`, `macos.headless.nowindow` (`app=0 windows=0`), `macos.hashes.jit` on A64's references (guest
  `6a1e1008459e6ab3`, 2ndmix `56bd7274490f91aa`); the state hashes twice on the three bundles (the recompiler's those
  references, the interpreter's P0's: guest `ea9a914608b05f40`, 2ndmix 600 `824841a8a36e3fab`); the sound lines of
  default and nodbg identical (6 entries, the four AicaHash lines equal to P0's Linux x86_64 lines on both engines,
  reported, not gated); describe: `sh4-debug-native`, `sh4-debug-fallback` with `ForceFallback`, 10 running ticks a
  second, none `registersStale`, all with `dc.sh4.cpu`; the tests project twice (438 s, 433 s): 340 targets, 259 passed,
  81 skipped (the Linux-only harness 67, the x64 backend's blocks 9, the three emcc tests, `debugger.sound.runner`,
  `debugger.sh4bp.b11`), the same set, 0 failed; the 30 sh4bp targets 29 passed and `b11` skipped; the contract gates M1
  18, M2 19, M3 20 on the recompiler, the interpreter and the forced fallback, M2's guest run 2 and M3's DSP runs 3 and
  2 on both engines, 0 `S04: recorded`, 0 `KNOWN LIMIT`; `debugger.net.test_e2e` passed in both runs; `macos.sh ci` as
  the workflow runs it: 340 tests, 180 passed, 160 skipped, 0 failed (201 s), its zip unpacked with `ditto -x -k`,
  `codesign --verify --strict --deep` of the built and the unpacked bundle, the unpacked bundle equal to the built one
  (13 entries), the folder `nulldc3-3.0.0-pre+g1206916377-macos-arm64/` with `nulldc3.app`, the five licences and
  `README.txt`, `Contents/Resources` with the five licences, the smoke run through `macrun.py` (`--version`, the guest
  at 90 vblanks `6a1e1008459e6ab3`, its files in its `--portable` directory, `debugger.describe`'s `ui`
  `3.0.0-pre+g7b88410915`); `pmset -g batt` said AC Power before every long step; the DiagnosticReports 2 before and
  after, none new.
- **The arm64 host** (at `12069163`): the crash places listed first (apport's log: the 9 nulldc3 lines of LA64's runs;
  names only); three builds (RelWithDebInfo; 155, 100 and 151 s; every compile with `-ffp-contract=off`); the first runs
  headless with `DISPLAY` and `WAYLAND_DISPLAY` unset and a core limit of 1 byte (the guest at 90 vblanks on both
  engines of the three builds, each `--version` one line `nullDC 3.0.0-pre+g1206916377`); the state hashes twice on the
  three builds (the recompiler's guest `6a1e1008459e6ab3` and 2ndmix `56bd7274490f91aa`, A64's references and the Mac's;
  the interpreter's P0's), the sound lines of default and nodbg identical (6 entries, equal to P0's Linux x86_64 lines,
  reported); describe on the recompiler `sh4-debug-native` (`sh4-debug-fallback` with `ForceFallback`), 11 running ticks
  a second, none `registersStale`; the tests project twice (739 s, 735 s): 330 targets, 314 passed, 16 skipped (the x64
  backend's blocks 9, the three emcc tests, the four `vmem` system tests that skip themselves where unprivileged user
  namespaces are refused), the same set, 0 failed; the 30 sh4bp targets all passed (`b11` counts: `perf_event_paranoid`
  2), `linux_arm64.hashes.jit` and `.pages.refused`, `debugger.selftest.sqw`, `.hostcycles` and `.emurun.pauses` on both
  engines green; the contract gates M1 18, M2 19, M3 20 on the recompiler, the interpreter and the forced fallback, the
  guest's runs whole (2, 3 and 2) on both engines, 0 `S04: recorded`, 0 `KNOWN LIMIT`; CIe's scripts as the
  `linux-arm64` job runs them: `linux.sh build`, `linux.sh test ci` 330 tests, 220 passed, 110 skipped, 0 failed,
  `appimage.sh` and its smoke run (5 PASS: `--version`, the guest at 90 vblanks `6a1e1008459e6ab3`, its files beside the
  AppImage, `ui` `3.0.0-pre+g7b88410915`, the five licences under `usr/share/doc/nulldc3/`), 611 s of the job's 90
  minutes; no new crash entry; outside the work folder only another program's files of the owner changed in the home
  (none of the round's).
- **Hygiene:** `actionlint` 1.7.12 (its binary's SHA-256 checked) on both workflows: clean; the UI's workflow read
  against 6.1 (`agents/nulldc3/plg/logs/ui-workflow-read.txt`: `release`'s `if:` and its `needs` exactly `check`, `e2e`,
  `package`, `release-check`'s `if:`, `publish` in `release` alone, `contents: write` only there, every action by its
  SHA); `ci.workflows`, `ci.hygiene`, `ci.scripts` and `rebrand.names` green in both runs at the documents' head and
  again at the branch's last head; `hygiene.py --extra <the full patterns>` on both repositories (the UI with
  `hygiene-allow-ui.txt`), on every file PLG's commits change, on its commit messages (each with both trailers) and on
  the two PR texts and the release-notes draft: no hit; `agents/nulldc3/plg/` scanned with the remote patterns: clean
  (the counts in PLG's report); every Python file PLG added or changed (19, `logs/py39.txt`) passes
  `ast.parse(feature_version=(3, 9))`, and those that ran on the Mac ran under its Python 3.9.
- **Cleaning up:** the work folders of the Windows host, the Mac and the arm64 host left with `data`, `tools`,
  `npm-cache` and a new `README.txt` (the arm64 host's `try/la64-crash-baseline.txt` kept); `b`, `src`, `tmp`, `run`,
  `in` and the `try` builds of CIe removed after the gate's last run there, no process of the round left; here the build
  trees, the UI clone, the binaries and the AppImage removed, the logs and scripts kept.
- **Departures:** the remote gates ran at the merged tree `12069163` and the Linux part at the documents' head (the
  documents change no code; the brief's "on the merged tree"); rows 1, 3, 8 and 13 of Appendix D were applied as A64
  left the tree, not to their letter (`rows.txt`); 2.3.7's platform items are 31 to 33 (the arm64 recompiler's debugger
  is no known limit any more); the Windows skip section of `tests/README.md` was brought to the counts too (CIe's note
  named the macOS and Linux arm64 ones, which A64 had corrected); two sentences of files PLG does not own are listed,
  not edited (PLAN's backlog, "Left by PLG"); the arm64 host's `try/la64-crash-baseline.txt` stays (the brief's "Left by
  CIe") although `try` goes elsewhere; the first try of the arm64 gate stopped at its feed (the Mac's and the arm64
  host's feeds, started together, shared one bundle file; its own directory, then run again from the start).
- **For the integrator (item 10):** the UI's PR first, with `pr-devtools.md`; after its merge, the emulator's pin moved
  to the UI's `main` commit and `wt/ui-pin` with it, then the emulator's PR with `pr-nulldc3.md`, whose second paragraph
  names the UI's PR (its number goes in when it is opened); both are titled "M3b" (the owner, 2026-10-19). Also: the
  release notes' known issues (`release-notes-known-issues.md`), if the integrator takes them into CIe's template; the
  two sentences outside Appendix D (PLAN's backlog, "Left by PLG").
- **The integrator's checks after item 10:** (to be added: the PRs, their first CI runs, the gitlink, the tags.)

---

## Appendix R. The reviews of rounds 1 to 3 and what became of them

**Round 1.** Three readers attacked revision 0 of this cut and its briefs: "platforms" (R1 to R24), "release" (R25 to
R47) and "integration" (R48 to R71). Each finding was checked against the two trees and the owner's decisions. None was
found wrong; where a finding offered a choice, or where its fix would itself have been unsafe or incomplete, the column
says what was taken instead and why. "As R*n*" marks a finding that another one already settled. A row that round 2
corrected says so ("revised by R*n*").

| # | Reader, weight | Finding | What became of it |
| --- | --- | --- | --- |
| R1 | platforms, must | The first MSVC and AppleClang builds will refuse files no brief owns; section 9 would stop the package at every compile error | Taken: PL-D68; 2.2's row and paragraph; W64a, MACa and LA64 "Owns"; section 9 "Ownership" |
| R2 | platforms, must | Only emulator runs went through `winrun`; the C++ suites, Python and node ran unprotected under MSVC's first build (Windows' `test_guard` traps nothing) | Taken: every program built in the round inside `winrun`, ctest under an outer `winrun` (4.1, 4.3, PL-D69; W64a, W64b, CIe's `windows.ps1`); every new crash-report entry listed, whatever its program (4.3, section 9) |
| R3 | platforms, must | The private desktop does not hide WER, AeDebug, hard-error pop-ups or the firewall; the mutation that turned `winrun`'s error mode off was unsafe and survived | Taken: 4.3's paragraph; PL-D70; the mutation replaced by the `invparam` and `purecall` kinds (PL-D35) and by the report's `wer=` field (R53) (revised by R74: those two mutations are replaced by the report's read-back fields) |
| R4 | platforms, must | The Mac's alert keyed on stderr alone: a socket or `/dev/null` from ssh would raise an alert; the first runs were outside the profile | Taken: PL-D43's alert rule (the argv scan, stderr `/dev/null` or closed, a LaunchServices start); every Mac emulator run through `macrun.py` under the profile, the first ones included (4.3, PL-D69); the alert mutation judged by reading (PL-D70) |
| R5 | platforms, must | `div` cannot trap on arm64; Linux words only three signals; `die` and a breakpoint both end with 133 | Taken: PL-D35 (the line per kind, the codes with `SIGBUS` 138, `die` ends itself with no `fault_handler` line); eight kinds on Windows, four on the Mac (2.8, 2.10). Checked: `fault_signal_name` and the handlers in `linux/common.cpp`; `die` prints through `msgboxf` on stdout |
| R6 | platforms, should | Windows' windows are gone when the child exits; the window station, a failed desktop and a new console were unspecified | Taken: sampling every 100 ms, the caller's station, a unique name, 126, `CREATE_NO_WINDOW`, `--probe` first (4.3, PL-D69, W64a). Added: the probe refuses to make its window unless its desktop is `winrun-<n>`, so a broken `winrun` cannot put the probe's window on the owner's screen |
| R7 | platforms, should | `CoInitialize` on every run makes hidden windows; `windows=0` would fail a correct build | Taken: no `CoInitialize` headless (PL-D34); `windows=<n>` counts visible or `WINDOW_CLASS` windows and the report lists every class (2.8, W64a). Checked: `winmain.cpp`'s `CoInitialize(nullptr)` before `reicast_init` |
| R8 | platforms, should | `output_is_kept` is true for a pipe, `NUL` and a console shared with `winrun`: `windows.helpbox` never reached the box | Taken, with another mechanism: `--own-console` gives the child a pseudo console (`CreatePseudoConsole`), not `CREATE_NEW_CONSOLE`, because Windows 11 can hand a new console to Windows Terminal, whose window opens on the owner's desktop; `windows.helpbox` has the three cases (W64a item 8) |
| R9 | platforms, should | Losing `/EHsc` in `config.cmake` never reaches `debugger.net` (added before `common/libswirl.cmake`) | Taken: the `#error` in `winmain.cpp` and one `tests/common` translation unit (2.8 item 1; W64a). Checked in `tests/CMakeLists.txt` |
| R10 | platforms, should | The objdump oracles fail, not skip, under `NULLDC_TESTS_REQUIRE_DATA` on the Windows host and the Mac | Taken (the first of the two options): a platform skip on hosts that are not Linux, Linux unchanged (W64a; 2.2's row for `tests/debugger/disasm`). Checked: `disasm_oracle_test` |
| R11 | platforms, should | Remote runs did not set `audio:backend=none` | Taken: PL-D71; 4.3; section 9; every remote brief |
| R12 | platforms, should | The Mac had no counterpart to `winrun` | Taken: `tests/macos/macrun.py` (4.2, 4.3, PL-D69; MACa; MACb's `emurun.py` branch; LA64's Mac part) (revised by R72: the profile applied once, nested calls run under the caller's) |
| R13 | platforms, should | A `POST_BUILD` plist edit and signature are lost after a reconfigure without a relink | Taken (with R43, R70): the stamp-file custom command (5.2, PL-D42, MACa); `macos.bundle.codesign` after a bare reconfigure |
| R14 | platforms, should | No Python language floor; the Mac has 3.9 | Taken (with R60): PL-D74; section 9 |
| R15 | platforms, should | `soundlines.py` would run `bash gen.sh` through WSL's launcher on Windows | Taken: W64b item 6. Checked: `shutil.which('bash')` in `soundlines.py` |
| R16 | platforms, should | `GetThreadTimes` is tick-granular | Taken: `QueryThreadCycleTime` (W64b item 5). Checked: `ThreadMicros` falls back to the wall clock off Linux and Darwin |
| R17 | platforms, should | `VersionInfo.FileVersion` is the string, not `3.0.0.0` | Taken (with R29, R51): 5.1's "how each form is read back"; W64a's gate; 7 item 10 |
| R18 | platforms, should | A UI clone named `dreamcast-devtools` beside the checkout is the look-up's second candidate | Taken (with R54): `src\ui`, gates with `-DNULLDC_DEVTOOLS_DIST=OFF` (4.1, 4.2, PL-D72, section 9). Checked: `dbg_webui.cmake` takes `OFF` as "not embedded" |
| R19 | platforms, should | Section 3's rows 5 to 8 named builds the packages had deleted, and gate builds serve no UI | Taken (with R63): `try\` / `try/` with a Release build that embeds the pin's UI (PL-D72; section 3; W64a, W64b, MACa, MACb); R63's `out\` not taken (one name) |
| R20 | platforms, should | Remote output carries the remote home and user name into the logs | Taken: the scrubber (4.3, PL-D73); JUnit files not copied back; each package's directory scanned (revised by R76 and R82: the package directories are scanned with the remote patterns) |
| R21 | platforms, should | Nothing forbade commits in a clone on the Mac | Taken: 4.2, section 9 |
| R22 | platforms, should | S04 on the arm64 JIT is predicted by the design to fail | Taken: PL-D78; 2.11; MACb; Appendix D row 3. Checked: the design's contract notes ("S04 is then not a gate for that backend's fallback"); `rec_arm64.cpp`'s main loop (revised by R73: the forced fallback on arm64 too; the rule in `contract.py`) |
| R23 | platforms, should | A vectored handler for every access violation would end windowed runs on a driver's handled fault | Taken: PL-D34's scope; W64a |
| R24 | platforms, should | `windows.abi.xmm` had no machine and no way to set XMM registers under MSVC | Taken (the first option): an Xbyak thunk and a minimal machine under `tests/windows` modelled on `tests/sh4_dynarec/dynarec_runner.cpp` (W64a). Checked: `libswirl/deps/xbyak` |
| R25 | release, must | The Windows and macOS release builds embed no UI; `release.sh` checks only the `ui` artifact | Taken (with R50): PL-D76; 6.3; CIe; the `try` builds measure the first MSVC and AppleClang embeds (PL-D72). Checked: `dbg_webui.cmake` prints only "not embedded" on a miss, and fails on an explicit path without a bundle |
| R26 | release, must | `cfg/cl.cpp` and `dbg_webui.cpp` are compiled by the tests project and by the standalone `tests/debugger/net` project, which have no generated header; VTe owned none of their CMake files | Taken (with R41, R52): PL-D75; 5.2; VTe's "Owns"; 2.2's rows. Checked: `tests/cfg/CMakeLists.txt`, `tests/debugger/net/CMakeLists.txt` (`project(dbg_net_tests)`) (revised by R78: `nulldc_version.h`, `version_title.cpp`, the net project's own step) |
| R27 | release, must | `rebrand.names` turns red under later packages' and PLG's edits of history, and only RB owned it | Taken (with R49): PL-D79; PL-D17; 2.2's row; section 9 |
| R28 | release, must | This cut, committed by PLG, would fail `ci.hygiene` (it quoted the WSL paths and the generic patterns) | Taken, both ways: 4.1 and 6.6 now describe those paths in words, and PLG owns its rows of `hygiene-allow.txt` (PL-D79; Appendix D) |
| R29 | release, must | `FileVersion` | As R17 |
| R30 | release, should | The string's leading part came from the tag and the numbers from the base | Taken: 5.1 ("derived forms"), PL-D24; VTe's `v3.0.1` case |
| R31 | release, should | `--abbrev` is a minimum; `--depth` with a plain path is ignored; the file transport is blocked for submodules | Taken: `<h>` from `rev-parse HEAD` (5.1, PL-D24); `file://` clones and `protocol.file.allow=always` (VCu, VTe, section 9) |
| R32 | release, should | A dispatch with `dry_run: false` could make a Release of `main` under the tag's name | Taken, more strictly: no `dry_run` input at all, a dispatch never creates anything, `release` only on tag pushes (6.1, PL-D77, `ci.workflows`) |
| R33 | release, should | A flake had only "PR and new tag" as remedy; a partial upload left a published Release; the `ui` artifact expired in a day | Taken: re-runs first (7 item 11, PL-D67); draft, check, publish (6.1, PL-D59, PL-D77); 7 days |
| R34 | release, should | Dry mode was unspecified; `release-files.ts` would refuse every PR | Taken: the check mode (6.1, PL-D77; VCu, CIe) (revised by R79 and R80: explicit invocations; the event condition) |
| R35 | release, should | The release plumbing first ran on `main` after both merges | Taken: `release-check` on pull requests (6.1 to 6.3, 6.5) |
| R36 | release, should | No release file was started in CI; the arm64 AppImage never at all | Taken: a smoke run of every file on its runner (6.3, PL-D76, CIe) |
| R37 | release, should | `appimagetool` downloads a moving runtime | Taken: PL-D81; CIe; the notes say FUSE 3 |
| R38 | release, should | The release files carried neither the UI's MIT licence nor the shell's BSD licence | Taken: PL-D80; 6.4; MACa's bundle; CIe. Checked: `devtools/LICENSE`, `reicast/LICENSE` (revised by R81: `LICENSE-devtools` in the bundle only with an embed; the submodule on the remote systems) |
| R39 | release, should | `reidbg/` was to be deleted as dead, but its server is built | Taken: PL-D14 keeps `reidbg/` and RB relabels it. Checked: `refrend_debug.cpp` (port 8828), `refsw-dbg` registered under `TA_LLE`, the `rend/soft` glob in `CMakeLists.txt` |
| R40 | release, should | `REICAST_CONFIGS` / `REICAST_DATAS` are live identifiers | Taken: `LEGACY_CONFIGS` / `LEGACY_DATAS` (PL-D13, RB) |
| R41 | release, should | `VER_*` in `types.h` would spread the generated header to every translation unit | As R26 (PL-D75). Checked: the two users take a `const char*` |
| R42 | release, should | The cut narrowed the owner's CI rule to "of the owner's" without asking | Taken: PL-Q3; 1.3 item 6 in the owner's words; 6.6; the PR texts |
| R43 | release, should | The plist after a reconfigure | As R13 |
| R44 | release, should | The About panel shows `3.0.0 (3.0.0.N)`; `CFBundleVersion` has four parts | Taken in part: the About panel shows the version (5.3, MACa). Not taken: a three-part `CFBundleVersion`; four parts are kept and recorded (PL-D29): Apple's one-to-three rule is its App Store's, and a three-part form either collides across tags (`X.Y.N`) or sorts wrongly (`N`) |
| R45 | release, should | `execSync` of a string under `cmd.exe` keeps the quotes; the real-path comparison differs on Windows | Taken: 5.1 (no shell, `--show-prefix`, `GIT_OPTIONAL_LOCKS=0`); VCu, VTe. Checked: `vite.config.ts` runs git through `execSync` |
| R46 | release, should | The design's 65, 666-670 and 4044 become false | Taken: Appendix D row 16 |
| R47 | release, should | The Windows host has Windows PowerShell 5.1, the runner `pwsh`; `Compress-Archive` writes `\` | Taken: PL-D82; 6.3; CIe |
| R48 | integration, must | PLG's pushes, PRs and tags cannot run in the package workflow, which forbids pushes | Taken: PL-D84; 2.14; section 3; section 7; PLG's brief. Checked: the workflow's "GIT RULES" line |
| R49 | integration, must | PLG's documents against the two allow lists | As R27 and R28 |
| R50 | integration, must | Release builds without the UI; `ui` checked only after the Releases | As R25; the smoke runs check `ui` before any Release (6.3) |
| R51 | integration, must | `FileVersion` | As R17 |
| R52 | integration, must | `types.h` as a reader; the tests projects | As R26; this finding's design (`libswirl/version.cpp`) is the one taken |
| R53 | integration, must | Equivalent and unsafe mutants on both systems | Taken with a correction: PL-D70; the report prints `errmode=` and `wer=`, but only `wer=` can show a removal under `winrun` (the child inherits `winrun`'s error mode, and a run without it would be unsafe), so `SetErrorMode` is judged by reading; on the Mac the AppKit mutants are judged by reading and `macos.sandbox.canary` proves the profile; extended to mutants that would leave a crash report (the re-raise) |
| R54 | integration, should | The UI clone's name | As R18 |
| R55 | integration, should | Gaps in 2.2 and in the "Owns" of W64a, W64b, MACa, VTe, MACb, PLG | Taken: 2.2's rows (the emulator-driven CMake files, `sh4bp`, `window_checklist.sh`, `tests/version/**`, `nulldc3/README.md`, MACb in the arm64 and Darwin rows, RB's `posix_vmem.cpp` comment) and the matching "Owns"; `README.md` already lists PLG in 2.2, and PLG's brief now owns its part |
| R56 | integration, should | A UI fix round breaks 7 item 3's equality; a UI defect after the UI merge had no path | Taken: 7 items 3 and 4 |
| R57 | integration, should | The submodule's tag under `actions/checkout` was first tested by the emulator's tag | Taken: 7 item 8, `require_ui_tag` (6.1) |
| R58 | integration, should | Audible remote runs | As R11 |
| R59 | integration, should | ccache was cached but never installed | Taken: 6.3, PL-D64 |
| R60 | integration, should | Python floor; PyYAML | As R14; `check_workflows.py` reads the workflow as text |
| R61 | integration, should | The local user name is the GitHub owner name | Taken: PL-D61's pattern definition; 6.6 (revised by R82: the scrubber never replaces a bare name) |
| R62 | integration, should | The UI's hygiene hits had no allow list | Taken: `nulldc3/ci/hygiene-allow-ui.txt` (6.6, PL-D79, CIe). Checked: the UI's `build-dspsim` and provenance tests name `/home/` and `/Users/` |
| R63 | integration, should | Section 3's rows 5 and 7 | As R19 |
| R64 | integration, should | The shipped binaries were never compared with P0 | Taken: PL-D83; CIe's gate; PLG item 5 |
| R65 | integration, should | RB's configure-output check could not be observed (a miss names no searched place) | Taken: RB's gate (`--warn-uninitialized`, the scratch bundle). Checked: `dbg_webui.cmake`'s messages |
| R66 | integration, should | `bWidth` 256 written as 256 is the right byte | Taken: IC's verifier (255, or 0 for the 128 px entry) |
| R67 | integration, should | The approved 16 px PNG's corners are not fully transparent | Taken: `icons.x11` (2.6, IC). Checked: alpha 1 at the top corners and 12 at the bottom ones at 16 px, 0 at 24 and 32 px |
| R68 | integration, should | VTe's gate built the main checkout, which lacks VTe's code and would embed the UI built beside it | Taken: a plain clone under `agents/nulldc3/vte/`, all with `-DNULLDC_DEVTOOLS_DIST=OFF` (2.7, VTe). Checked: the UI checkout beside the emulator has a built `dist-native` |
| R69 | integration, should | CIe's remote runs cannot match the full-data numbers in CI mode; `linux.sh deps` would install packages here | Taken: the data modes (2.13, CIe); `deps` not run here |
| R70 | integration, should | The plist after a reconfigure | As R13 |
| R71 | integration, should | Hours-long remote steps under foreground commands | Taken: `winrun --detach`, `macrun.py --detach` (4.1, 4.2, PL-D69); each remote brief states its expected duration (4.3) |

**Round 2.** The same three readers attacked revision 1 ("platforms" R72 to R77, "release" R78 to R81, "integration" R82
and R83; they numbered their problems per reader, so their "problem 12" is R12, "problem 1" of integration is R48). Each
finding was checked against the two trees, the design and the owner's decisions; none was found wrong. Where a fix was
extended, the column says how.

| # | Reader, weight | Finding | What became of it |
| --- | --- | --- | --- |
| R72 | platforms, must | R12's fix breaks next to itself: `macrun.py` applies `sandbox-exec` to every command, the detached long steps (builds, the tests project's ctest, `macos.sh`) included; inside such a ctest every `tests/macos` target and `emurun.py`'s Darwin branch call `macrun.py` again, and macOS refuses a second profile (`sandbox_apply: Operation not permitted`); the targets "fail, never fall back", so MACa's, MACb's, LA64's, CIe's and PLG's Mac gates go red; the canary never proves the nested shape | Taken: `macrun.py` asks the kernel (`sandbox_check(getpid(), NULL, 0)` through `ctypes`) and applies the profile only for an unsandboxed caller, else runs the command directly and says `macrun: inside the profile already`; it refuses when it cannot ask (4.2, 4.3, PL-D69, section 9; MACa; MACb, LA64, CIe, PLG take it as is). The canary runs in both shapes in every Mac gate. Extended: the canary prints the kernel's view (sandboxed; the window server's `mach-lookup` denied), so neither shape depends on whether a non-GUI ssh session could reach the window server at all, and the mutation "profile skipped for an unsandboxed caller" is caught; an environment mark was rejected (the harnesses give the emulator an environment of their own). Not tested on the Mac (documents only): MACa's first gate is the proof (8.4) |
| R73 | platforms, must | R22 only partly settled: on arm64 `engine.fallback` is true with or without ForceFallback, the `.fallback` targets are `--engine jit --fallback`, so the forced fallback is the JIT's engine behind the arm64 loop, and calling its S04 failure a defect would reject a correct implementation; "recorded, not gated" had no mechanism (`run_runner` fails a target on any failed scenario) and MACb did not own `contract.py`'s verdict | Taken: PL-D78, 2.11, 2.2's row, MACb (What is known, Owns, Gate, Verifier), 8.4, PLG item 3, Appendix D row 3. Checked: `dbg_emu.cpp` `CaptureEngine`; `tests/debugger/contract/CMakeLists.txt` (`FALLBACK` → `--fallback`); `contract.py` `run_runner`; the UI's S04 checks in `scripts/contract/scenarios.ts` (the "went back" message carries both values, the "did not increase" one moves by 0); `ContractSummary.scenarios[].details` in the `--json-file` summary; `SH4_TIMESLICE` 448. Extended: `platform.machine()` `arm64` **or `aarch64`** (the rule follows the backend, Linux arm64 included); every other summary field held at zero; a test of the rule on synthetic summaries on every host, so the verifier's widened mutants are caught by a target, not only by reading |
| R74 | platforms, must | R3's replacement mutations do not hold: without `_set_invalid_parameter_handler` the static UCRT ends in `__fastfail`, past every handler and the job's flag, straight to WER (a report on the owner's host: unsafe); without `_set_purecall_handler` `_purecall` calls `abort()`, which the policy's SIGABRT handler turns into the same line and 134 (equivalent) | Taken: the UI report gains `invparam=`, `purecall=`, `sigabrt=` (read back with `_get_invalid_parameter_handler()`, `_get_purecall_handler()`, `signal(SIGABRT, SIG_GET)`), required by `windows.headless.nowindow`; the two mutations replaced, no crashing mutant (2.8, PL-D70, W64a); `windows.fatal.invparam` / `.purecall` stay as positive tests. Extended: `abort=` (`_set_abort_behavior(0, 0)`, read without change), since removing the abort behaviour is equivalent while the SIGABRT handler stands and unsafe without it |
| R75 | platforms, should | W64b's gate still counted only crash-report entries naming `nulldc3`; W64b first runs the harnesses' Python and node, `sh4bp` and `dbg_net_demo` on Windows | Taken: W64b's gate reports every new entry with its program, whatever it names (as 4.3 and every other remote gate) |
| R76 | platforms, should | The package-directory scan used the owner-specific pattern file, which holds `/home/<u>/`, this machine's own home: every local CMake, ninja, ctest and sanitizer log and P0's recipe carry it (367 files of m3g, 263 of f4d, 485 of e8b), so no package could report clean | Taken: PL-D61 defines the remote patterns (the remote user names in path form and `<u>@<host>`, the Mac's alias, the host names) and the full patterns (plus `/home/<u>/`); package directories and the scrubber use the remote set; committed files, the `docs/nulldc3/` copies, PR texts, CI files and CI logs the full set (4.3, PL-D61, PL-D73, section 9, 6.6, 2.13, 2.14, 7; every remote brief). Checked: the counts above by a read-only `grep -rlF` here. The alternative (scrubbing local output too) was not taken: it would rewrite paths a package needs to rerun its own steps |
| R77 | platforms, should | W64a's and MACa's gates require RB's, IC's and VTe's tests on a system where they first run, but PL-D68 allowed only compile-only fixes, and the earlier packages cannot reach the remote systems | Taken: PL-D68's runtime fixes in `tests/rebrand/**`, `tests/icons/**`, `make_icons.py`'s file input and output and `tests/version/**` (2.2's paragraph and rows, 2.8, 2.10, section 9, W64a, MACa); the 3.9 floor checked in the writer's own turn with `ast.parse(source, feature_version=(3, 9))` (PL-D74, section 9, the briefs of the packages that add Python), noted as best effort |
| R78 | release, must | PL-D75 put the declarations in `types.h`, and `version.cpp` (which needs `types.h` for `VER_EMUNAME`) was to be compiled in the standalone `tests/debugger/net` project, which defines no `TARGET_*`: `build.h` stops with `#error Invalid Target`, and on MSVC `types.h` pulls in `<Windows.h>`; two version steps of one name stop the tests project's configure | Taken: `libswirl/nulldc_version.h` (no includes) declares the four functions; `version.cpp` includes only it and the generated header; `nulldc_title()` moves to `libswirl/version_title.cpp` (includes `types.h`, never the generated header, so one unit still reads it and VTe's `ninja -d explain` gate holds); the net project compiles `version.cpp` alone into the `dbg_webui` object library under a version step of its own name (PL-D75, 2.2, 2.7, 5.2, VTe). Checked: `build.h:200-218`, `types.h:13` and `:405-418`, `tests/debugger/net/CMakeLists.txt` (`project(dbg_net_tests)`, `dbg_webui` consumed only by `test_webui` and `dbg_net_demo`), `dbg_webui.cpp` includes no `types.h`. Extended: VTe's gate builds the net project standalone; two verifier mutations |
| R79 | release, must | The UI's `package` runs `release-files.ts` on every event, and the script chose tag mode by `GITHUB_REF_TYPE`: on the tag push `package` would run `gh release create` with a read token before `check` and `e2e`, fail, skip `release`, and leave the UI's tag without a Release, whose fix would move the tag off the emulator's gitlink; `release` was described as zipping build directories it never receives | Taken: explicit invocations `pack` (never `gh`), `check`, `publish` (from `package`'s artifact; refuses unless the event is a tag push) (6.1, 6.2, 2.4, PL-D77, VCu); the stub test runs `pack` under a tag push's environment; verifier mutation "package calls publish". Extended to the emulator's `release.sh` (`check`, `publish`), so one rule holds for both repositories (6.1, 6.3, 2.13, CIe) |
| R80 | release, must | The emulator's `release` needed all jobs, `release-check` included, which does not run on a tag push, so GitHub would skip `release` (its `if:` carries no status function); `if: github.ref_type == 'tag'` and a mode chosen by `GITHUB_REF_TYPE` are also true for a dispatch on a tag ref, against "a dispatch never creates anything" | Taken: `release` needs exactly `ui` and the four platform jobs (the UI's exactly `check`, `e2e`, `package`), never `release-check`; `if: github.event_name == 'push' && github.ref_type == 'tag'`; `release-check` on `github.event_name != 'push'`; `publish` only when `GITHUB_EVENT_NAME` is `push` and `GITHUB_REF_TYPE` is `tag`; `ci.workflows` and VCu's read check `needs` and both conditions; verifier mutations (6.1, 6.3, 6.5, 2.4, 2.13, PL-D60, PL-D77, section 9, VCu, CIe) |
| R81 | release, must | MACa copied `devtools/LICENSE` into the bundle on every macOS configure, but the Mac's clone never initialised the submodule (only 4.1 did, on Windows): MACa's first configure, the `try` bundle, `macos.sh` and PLG's zip would stop, and a plain clone without `--recursive` could no longer be configured on macOS alone | Taken: `LICENSE-devtools` only when the bundle embeds a UI (`DBG_WEBUI_BUNDLE_DIR`), the configure stopping with a message naming the file then; `macos.bundle.codesign` expects the set that matches the embed (PL-D80, 2.10, 6.4, MACa); the submodule `devtools` initialised from the UI clone on both remote systems, with `-c protocol.file.allow=always` (4.1 made explicit, 4.2, section 9; W64a, W64b, MACa, MACb, LA64, CIe, PLG). Checked: `dbg_webui.cmake` sets `DBG_WEBUI_BUNDLE_DIR` empty when nothing is embedded |
| R82 | integration, must | The same as R76 (m3g 367 of 779 files, e8a 414 of 654, f4a 352 of 455; PLG's item 6 could never be green); and the scrubber still replaced bare user names, which PL-D61 had dropped because the name can be the GitHub owner's (`<owner>/nulldc3` in every remote log) | As R76 for the scan (PLG item 6 now scans `agents/nulldc3/plg/` with the remote patterns; every brief's report line); taken for the scrubber: user names in path form and `<u>@<host>` only, never the bare name (4.3, PL-D61, PL-D73). Extended: remote listings are made without owner columns (`ls -1`, `dir /b`), so a bare remote user name is never printed into a log here |
| R83 | integration, should | PLG's Gate (2.14 and the brief) still listed item 10 (the PRs, the tags); the workflow's verifier reruns every line of the Gate, cannot push, and would spend its fix rounds on it | Taken: both Gates end at items 1 to 9; item 10 is under "After the package (the integrator, outside the workflow; PL-D84; not part of the package's gate)". Checked: `resume/pkg-brief.workflow.js` (VERIFY part B; GIT RULES) |

**Round 3.** Two readers attacked revision 2 ("platforms" R84, "release" R85). Both findings were checked against the two
trees, this machine's files (read-only listings: `/etc/wsl.conf`, `strings` of the gate ELFs, `grep -rl` of
`agents/nulldc3/`, `git grep` of both repositories, `git submodule status` of the worktrees) and the owner's decisions;
both hold. Where a fix was extended, the column says how.

| # | Reader, weight | Finding | What became of it |
| --- | --- | --- | --- |
| R84 | platforms, must | R76/R82 still not settled: the remote patterns hold the Windows host's host name and `<u>@<host>`; this machine is WSL2 without a hostname in `/etc/wsl.conf`, so its host name is the Windows computer name; 2ndmix.elf and hello.elf (KallistiOS 2.2.2, built with `sh-elf-gcc.exe`) print a build banner `<u>@<host>:/opt/toolchains/dc/kos`, so every local log of their runs matches a remote pattern (3833 files of earlier package directories, none in m3g, whose recipe throws the output away, none in either repository); LA64's qemu runs, PLG items 4 to 6 and every package keeping a local harness or guest log could never report clean | Taken: the fact in 4.3, PL-D61 and section 9; the package-directory scan sorts its hits and reports `KOS banner: N lines in M files`, not as a failure, and is clean when nothing else matches; any other hit is reported with its source (4.3, PL-D61, PL-D73, section 9, 2.14 item 6, 6.6; the scan lines of W64a, W64b, MACa, MACb, LA64, CIe, PLG and RB). The full patterns keep the host names; CI runs no gate ELF (PL-D58) and nothing that leaves the machine may quote the banner. Checked: `/etc/wsl.conf` (no `[network] hostname`); `strings` of both ELFs (one banner line each, its user this machine's); `git grep -i` of both repositories (none). Extended, from the same grep: (1) the banner's second form `<u>@<host>.localdomain:/opt/toolchains/dc/kos` (KallistiOS guests built here, 323 lines of earlier directories) and banner lines behind a harness's prefix (`    \|   `), so a banner line is defined by the form of each host-name occurrence, not by the whole line; (2) third-party code under a UI clone's `node_modules` (12 files of the UI's own) holds the host name inside a longer word, which PLG's fresh UI clone of item 1 would bring into its directory: such hits are reported as `inside a word: N lines`, and the scrubber replaces a host name only as a whole word; (3) earlier packages' wine prefixes hold the Windows computer name in capitals (`ComputerName`, `USERDOMAIN`, `LOGONSERVER`): every pattern set and the scrubber match without regard to letter case; (4) 4.3's scrubber replaced "the host names (from the variables above)", but no variable held one: `PL_MAC_HOST` and `PL_WIN_HOST` added, both applied to every system's output, since 2ndmix and hello print the Windows host's name on the Mac too (W64a and MACa name them); (5) PL-Q1 gains a note: the ELFs as they are would put the banner into public CI logs. The alternative (keeping no emulator output of the gate ELFs locally, as P0's recipe does) was not taken: LA64, the contract gates and PLG need their logs |
| R85 | release, must | R81 made `devtools/LICENSE` available only in the remote clones, but the x86_64 AppImage is made here (CIe's gate, its verifier's detached worktree, PLG item 5), where every worktree's `devtools/` is an empty, uninitialised submodule (`git submodule status`: `-`; the package workflow initialises only the two vector submodules); `appimage.sh` would stop, or with an optional copy ship without the UI's licence; `linux.sh test`'s `npm ci` in `devtools/` fails the same way; the gitlink (VCu's verified head) is not on GitHub before section 7 item 2 | Taken: PL-D85 — the scripts take the UI checkout `<ui>` as an argument; `LICENSE-devtools` is copied from `<ui>/LICENSE` and `npm ci` runs in `<ui>`; a script stops when `<ui>/LICENSE` is missing or `<ui>` is not at the tree's gitlink; the value is `devtools` on a runner, `agents/nulldc3/wt/ui-pin` here (section 3 keeps it at the gitlink; every Linux gate already gives it as `NULLDC_DEVTOOLS_DIR`) (PL-D80, PL-D85, 2.13, 2.14 item 5, section 3, 6.3, 6.4, section 9; CIe, PLG). CIe's Linux gate and PLG item 5 list the AppImage's `usr/share/doc/nulldc3/` (the five licences); CIe's verifier mutation: the copy made optional or dropped, and `<ui>` without `LICENSE`. Checked: `git submodule status` of `wt/m3g` and `wt/g`; `resume/pkg-brief.workflow.js`; `tests/CMakeLists.txt` (`NULLDC_DEVTOOLS_DIR`, the look-up that passes over an empty submodule); `tests/devtools` (`devtools.commit`); the shared config's `submodule.devtools.url` (the local UI repository). Departure from the finding's fix: on the Windows host and the Mac `<ui>` is the UI clone `src\ui` / `src/ui`, not the submodule, since the clone already has `node_modules` and is where the contract runner runs (a second `npm ci` avoided); the remote submodule initialisation of R81 stays (the Mac's bundle configure reads `devtools/LICENSE` when it embeds a UI; one rule on both systems), and 4.1's and 4.2's reasons for it are corrected. The alternative of initialising each worktree's submodule here was not taken (a second UI checkout and `node_modules` per worktree, a step every package and verifier must remember) |
