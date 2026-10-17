# nullDC 3.0.0 working documents

- `PLAN.md` — the work list: tracks, milestones, gates, status, decisions, open questions.
- `devtools-integration-design.md` — track 1: the debugger server inside the emulator and protocol v3 of the web UI
  (implementation contract, revision 3; the working document `TRACK1-DESIGN.md`), brought to the cut of the M2 round at
  its end (milestone M2 complete). Its appendix has the dispositions of the three reviews, what revision 3 changed, and
  what the M2 round left for the text ("M2 as built"). Revision 2 is the earlier text of the file, in its history.
- `devtools-integration-open.md` — the companion of the design (working name `DESIGN-V3-OPEN.md`): the questions that
  are still open, the ones that were answered on 2026-10-09, header revision 4, the comment-only changes of the
  frozen debugger headers (applied; its list is kept as the record), and header revision 5 (applied with J1); header
  revision 6 is listed in the cut of the M2 round.
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
  changes the round makes to the design text, the briefs and the reviews. Where it and the design differ it is the
  contract until the end of the round, when the design is brought to it; the frozen headers cite it from header
  revision 7 on.
- `sh4-breakpoints-spec.md` — the SH4 breakpoint / single-step mechanism in the active backend, with the prototype's results.
- `automated-runs.md` — headless mode, the `--config` keys that end a run, the exit line and its state hash, the pinned
  RTC, what a fatal fault and a signal that is sent do. Reference for `tests/system` and `tests/perf`.

These are snapshots of working documents. They name each other by their working names (the design has the table at its
top); paths of the form `agents/nulldc3/...` point into the working area outside this repository (reader reports,
reviews, scratch).
