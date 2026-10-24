# rebrand.names

The tree was reicast's. Its platform shell `reicast/` is `nulldc3/`, and every live name that said reicast has a
nulldc3 name (the save-state macros `NULLDC_S` ... `NULLDC_USA`, the entry points `nulldc_init`, `nulldc_ui_loop`,
`nulldc_term`, the CMake variables `nulldc_*`, `NULLDC_VERSION`, the Lua module and the User-Agent `nulldc3`, ...).
What still says reicast is history: the licences and copyright headers, credits, provenance notes, the markers of
local edits in a vendored library, the records under `docs/nulldc3/`, and the directories of reicast that the
emulator does not read and the system tests prove it does not. The test holds that.

`rebrand_names.py` lists every line of a tracked text file that says `reicast` in any case (`git -C <tree> grep -I
-i -n reicast`, binary files left out) and every tracked file whose name says it, and compares them with
`allowed.txt`:

- a line that no entry allows fails the test (`not allowed: <path>:<line>: <text>`), and so does a file name
  (`not allowed (file name): <path>`);
- a row that finds no line any more fails it (`gone: allowed.txt:<n>: <path> | <pattern>`), and so does a row that
  finds more lines than the list has rows for it (`count: ...`);
- an entry that is not in the format below fails it (`malformed: allowed.txt:<n>: ...`).

Submodules (`tests/vendor/*`, `devtools/`) are other repositories and are not searched, whatever the git config says:
the script calls git with `-c submodule.recurse=false`, and the ctest target runs it with `submodule.recurse=true` in
git's environment so that a host whose git config sets it is covered.

A tree that is not a git checkout (an archive, a copy; `git rev-parse --show-prefix` at the tree's top) has nothing
to compare: the test is skipped (exit 77), and with `NULLDC_TESTS_REQUIRE_DATA` it fails. It needs python3 (3.9 or
later, the standard library) and git. It reads the working tree, so an uncommitted change counts.

## allowed.txt

One entry per line, four fields separated by ` | ` (space, bar, space):

    <path> | <pattern> | <class> | <reason>

- `<path>`: a tracked file as git names it, from the top of the tree, with `/`. `<dir>/**` is every file below
  `<dir>`, and only goes with the pattern `*`.
- `<pattern>`: `*` allows the whole file (a whole path: the licence files, `docs/nulldc3/**`, whose records are
  edited at will, and `tests/rebrand/**`, this test, which names the word it looks for; they have no rows and are
  never "gone"). Anything else makes the entry a row: a piece of the one line it allows, copied from that line
  (case and spaces count), which has to say reicast itself. A row allows the lines of its file that contain its
  pattern, and n rows with the same path and pattern need exactly n such lines. A pattern may contain ` | `: the
  first ` | ` ends the path and the last two fields are the class and the reason.
- `<class>`: one word, the kind of history: `licence`, `record`, `origin`, `credit`, `provenance`, `header`,
  `marker`, `legacy-dirs`, and `self` for this test's own files.
- `<reason>`: why the line keeps the name.

Rows are sorted by path, and within a file by line. A package that adds, moves, rewraps or deletes a line of history
changes its row in the same commit: find it by the file's path and a piece of the line's text (`grep -n
'<piece>' tests/rebrand/allowed.txt`), and copy the new piece of the line into the pattern. A file that moves takes
its rows with it (the path changes); a line that goes takes its row. A new live name that says reicast gets no row:
it gets a new name.

Run it alone: `python3 tests/rebrand/rebrand_names.py --tree . --allowed tests/rebrand/allowed.txt`.
