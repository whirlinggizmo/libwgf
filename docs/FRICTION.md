# Friction

Each workaround a game had to make, and each use of libwgf that was awkward, as the game's developer found it (SPEC.md, "Games test the framework only if they can't bend it"). A game developer works from the public API, the docs, and `wgf new` alone, and may work around a gap only by logging it here. A milestone closes only when each of its entries is triaged: it became a framework task (in ROADMAP.md), or a decision was recorded that the workaround is fine (in HISTORY.md, with why).

An entry is added by the game's developer, under its game, newest last, and is never rewritten; its triage is added to it. A triaged entry stays until its milestone closes, then moves to HISTORY.md with the milestone's record.

## Format

```
### <game>: <what was missing or awkward, in a few words>

- **Where:** <file and line, or the function, in the game>
- **Missing:** <what the framework lacked, or what made the use awkward>
- **Workaround:** <what the game did instead; "none" if it went without>
- **Cost:** <what it cost: lines, time, a measured number, a behavior the player sees>
- **Found by:** <the session: the game's developer, a clean-room rebuild, an adversarial review, a jam simulation>
- **Triage:** <empty until triaged; then "task: <ROADMAP step or item>" or "fine: <HISTORY entry>">
```

## Entries

None yet.
