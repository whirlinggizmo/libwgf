# Agent instructions

Read [docs/CONVENTIONS.md](docs/CONVENTIONS.md) before changing code, layout, build files, or docs, and follow it; [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) is how libwgf works now, and [docs/ROADMAP.md](docs/ROADMAP.md) what is left. Every rule lives in a developer doc, once; this file only links to them and adds what an agent needs.

## Rules that are easy to break

Each is CONVENTIONS.md's; the words here only find it.

- The layer table, and that dependencies point down ("Layers and dependencies").
- An optional part reached through its hook, never by name ("Layers and dependencies").
- The hard rule: what a public call may take and return; handles typed by kind; math values returned, never taken ("Public API").
- No backend identifier in a public header ("Public API", "Other rules").
- `out/`, never `build/`, for anything built against the library ("Build").
- Third-party source in `deps/`, never fetched ("Dependencies").
- Tools in Python, standard library only; nothing in shell; no regular expressions over C source ("Tooling").
- Which doc is true, and what moves to HISTORY.md ("Docs").
- A guard loosened only in a commit of its own, and every guard change reported ("Guards").
- Which verify tier runs when: quick before every commit, full at a step's close, a hand-off, or a change to the build, toolchain, deps, platform code, or a guard ("Verifying").

## Agent practice

- Before a feature, read how the references did it (SPEC.md lists them; read-only) and their HISTORY entries for it; record what was carried, what was redesigned, and why, in docs/HISTORY.md.
- Before every commit: review the diff against docs/CONVENTIONS.md and run the quick tier, `python3 tools/verify_builds.py --quick`; the full tier, `python3 tools/verify_builds.py --web --windows sightblinder`, when CONVENTIONS' "Verifying" says. Report which tier ran, and each skip as a skip.
- Commit small; push each verified commit to `main`, check that CI passes, and fix a CI failure before new work.
- The skills in `.claude/skills/` are the steps for repeated work: adding a component, a binding call, or an example.
