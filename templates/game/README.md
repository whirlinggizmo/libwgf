# @NAME@

A game on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe.

```sh
wgf serve                   # in a browser, reloading as you save, its state kept
wgf run                     # on the desktop
wgf autopilot autopilot/smoke.autopilot   # an autopilot run, headless (its format: libwgf's BUILDING.md, "Autopilot files")
wgf export                  # export/web (a static folder) and export/desktop, each smoke-tested
```

`wgf.json` is the game's: its name, its main class, where its sources, assets, and autopilot files are, and its web size budget (`web_budget_kb`, gzipped), which `wgf export` holds it to.
