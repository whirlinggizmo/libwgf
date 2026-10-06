# libwgf

A game framework for the web first and the desktop second: a C core on sokol, and games written in Haxe (JavaScript on the web against a prebuilt wasm host; hxcpp natively).

It is done when it ships three games ([SPEC.md](SPEC.md)): Asteroids, a chase-camera racing game, and an ARPG vertical slice with co-op. [docs/ROADMAP.md](docs/ROADMAP.md) says where it is.

## Docs

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how libwgf works: its layers and modules, handles, resources and tasks, the runtime.
- [BUILDING.md](BUILDING.md): building and testing on each platform, what to install, and every tool.
- [docs/CONVENTIONS.md](docs/CONVENTIONS.md): the rules for changing libwgf.
- [games/asteroids/](games/asteroids/README.md): the first game, played at <https://whirlinggizmo.github.io/libwgf/asteroids/>.
- [BUILDING.md](BUILDING.md#games-the-wgf-tool): making a game with the `wgf` tool: `./wgf new`, then `wgf serve`.
- [docs/BINDINGS.md](docs/BINDINGS.md): how a binding maps libwgf's calls; the Haxe binding is [bindings/haxe/](bindings/haxe/README.md).
- [docs/benchmarks.md](docs/benchmarks.md): every program's web size, beside libwgt's and wgrender-c's, which CI holds libwgf to.
- [docs/HISTORY.md](docs/HISTORY.md): what was built and decided, and why.
- [docs/ROADMAP.md](docs/ROADMAP.md): what is left, in order.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [deps/README.md](deps/README.md): the vendored code, its licenses, and what a binary ships.

## Build

```
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug
python3 tools/stage_variant.py linux-x64-debug
```

Every preset, platform, and tool: [BUILDING.md](BUILDING.md).

## License

MIT ([LICENSE](LICENSE)). The vendored code's licenses, and what a program built with libwgf must ship, are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
