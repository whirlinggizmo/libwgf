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

## Agent practice

- Before a feature, read how the references did it (SPEC.md lists them; read-only) and their HISTORY entries for it; record what was carried, what was redesigned, and why, in docs/HISTORY.md.
- Before every commit: review the diff against docs/CONVENTIONS.md, run `python3 tools/verify_builds.py --web --windows sightblinder` (every preset and check this machine can run), and report each skip as a skip.
- Commit small; push each reviewed, verified commit to `main`, and check that CI passes.
- The skills in `.claude/skills/` are the steps for repeated work: adding a component, a binding call, or an example.
