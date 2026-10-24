# nullDC 3.0.0 working documents

- `PLAN.md` — the work list: tracks, milestones, gates, status, decisions, open questions.
- `devtools-integration-design.md` — track 1: the debugger server inside the emulator and protocol v3 of the web UI
  (implementation contract, revision 3; the working document `TRACK1-DESIGN.md`), brought to the cuts of the M2 and M3
  rounds and of the platform round at their ends (milestones M2 and M3 complete; the platform round complete). Its
  appendix has the dispositions of the three reviews, what revision 3 changed, and what the M2 and M3 rounds left for
  the text ("M2 as built", "M3 as built"). Revision 2 is the earlier text of the file, in its history.
- `devtools-integration-open.md` — the companion of the design (working name `DESIGN-V3-OPEN.md`): the questions that
  are still open, the ones that were answered on 2026-10-09, header revision 4, the comment-only changes of the
  frozen debugger headers (applied; its list is kept as the record), and header revision 5 (applied with J1); header
  revision 6 is listed in the cut of the M2 round, header revision 7 in the cut of the M3 round, header revision 8 in
  the cut of the platform round.
- `devtools-integration-decisions.md` — the 45 decisions that revision 3 of the design folds in: the 29 of 2026-10-09
  and the 16 that followed the devtools round on 2026-10-10 (working name `DESIGN-V3-DECISIONS.md`).
- `devtools-integration-m2-round.md` — the cut of the M2 round of the debugger (working name `M2-ROUND.md`): its
  scope, header revision 6, the packages with what each owns and its gate, their order, the sounding guest, the
  decisions M2-D1 to M2-D68 and the project owner's answers, the changes the round makes to the design text, the
  briefs, the reviews and the integrator's notes on every package (its Appendix I). It was the contract where it and
  the design differed until the end of the round, when the design was brought to it (package M2G); the design, the
  decisions and the frozen headers cite it for header revision 6, the M2 tree and the guest's contract.
- `devtools-integration-m3-round.md` — the cut of the M3 round of the debugger (working name `M3-ROUND.md`): its
  scope, header revision 7, the packages with what each owns and its gate, their order, the sound lines and the speed
  through the round, the guest's DSP program, the decisions M3-D1 to M3-D44 and the project owner's answers, the
  changes the round makes to the design text, the briefs, the reviews and the integrator's notes on the packages (its
  Appendix I). It was the contract where it and the design differed until the end of the round, when the design was
  brought to it (package M3G); the design, the decisions and the frozen headers cite it for header revision 7, the M3
  tree and the guest's DSP program.
- `platform-round.md` — the cut of the platform round (working name `PLATFORM-ROUND.md`; its PRs call it M3b): the
  rebrand (`reicast/` became `nulldc3/`), the icons, the versions from the tag `v3.0.0-pre`, Windows, macOS, Linux
  arm64, CI and releases, and the arm64 recompiler's native debugger (its addendum A64); the owner's decisions, header
  revision 8, the packages with what each owns and its gate, the remote systems' rules, the decisions PL-D1 to PL-D86,
  the questions PL-Q1 to PL-Q3, the changes the round makes to the design text and the plan (its Appendix D), the
  integrator's notes on every package (its Appendix I) and the reviews of the cut. It was the contract where it and the
  design or the plan differed until the end of the round, when they were brought to it (package PLG).
- `sh4-breakpoints-spec.md` — the SH4 breakpoint / single-step mechanism in the active backend, with the prototype's results.
- `automated-runs.md` — headless mode on Linux, Windows and macOS, the `--config` keys that end a run, the exit line and
  its state hash, the pinned RTC, what a fatal fault and a signal that is sent do, `nulldc3-winrun` and
  `tests/macos/macrun.py`, `--help` and `--version`. Reference for `tests/system`, `tests/perf` and every automated run.

These are snapshots of working documents. They name each other by their working names (the design has the table at its
top); paths of the form `agents/nulldc3/...` point into the working area outside this repository (reader reports,
reviews, scratch).
