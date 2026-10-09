# nullDC 3.0.0 working documents

- `PLAN.md` — the work list: tracks, milestones, gates, status, decisions, open questions.
- `devtools-integration-design.md` — track 1: the debugger server inside the emulator and protocol v3 of the web UI
  (implementation contract, revision 3; the working document `TRACK1-DESIGN.md`). Its appendix has the dispositions of
  the three reviews and what revision 3 changed. Revision 2 is the earlier text of the file, in its history.
- `devtools-integration-open.md` — the companion of the design (working name `DESIGN-V3-OPEN.md`): the questions that
  are still open, the ones that were answered on 2026-10-09, and header revision 4, the comment-only changes of the
  frozen debugger headers.
- `devtools-integration-decisions.md` — the 29 decisions of 2026-10-09 that revision 3 of the design folds in (working
  name `DESIGN-V3-DECISIONS.md`).
- `sh4-breakpoints-spec.md` — the SH4 breakpoint / single-step mechanism in the active backend, with the prototype's results.
- `automated-runs.md` — headless mode, the `-config` keys that end a run, the exit line and its state hash, the pinned
  RTC, what a fatal fault and a signal that is sent do. Reference for `tests/system` and `tests/perf`.

These are snapshots of working documents. They name each other by their working names (the design has the table at its
top); paths of the form `agents/nulldc3/...` point into the working area outside this repository (reader reports,
reviews, scratch).
