"""libwgf's optional parts a game links on purpose: what each is, the native build's define
that links it (bindings/haxe/project/Build.xml, bindings/haxe/src/wgf/Runtime.hx), the web
host's export that does, and the C call a program makes instead.

A part a game's data needs, though its code never names it (a scene's model path=, its emitter
lines), is a feature it declares in wgf.json ("features"): a fact about the game, the same in
every build. The autopilot's runner is the build's instead: the tool's own builds have it, and
an export doesn't unless --keep-autopilot. Nothing is read from a game's files to guess: a
program that reaches a part it didn't declare says so, naming the feature, and an export's
smoke run and playthrough fly the trimmed build, so a miss fails there, before it ships.
Standard library only."""

from collections import namedtuple

Feature = namedtuple('Feature', 'define export call what')

FEATURES = {
    'gltf': Feature('wgf_gltf', '_wgf_mesh_create', 'wgf_mesh_create',
                    "glTF files: a scene's model path= lines"),
    'emitters': Feature('wgf_emitters', '_wgf_emitter2d_create', 'wgf_emitter2d_create',
                        "particles: a scene's emitter2d and emitter3d lines"),
    'characters': Feature('wgf_characters', '_wgf_physics_enable_characters', 'wgf_physics_enable_characters',
                          "characters: a body of type character (Jolt's CharacterVirtual), in code or a scene"),
    'autopilot': Feature('wgf_autopilot', '_wgf_app_enable_autopilot', 'wgf_app_enable_autopilot',
                         "the autopilot's runner: the build's, not the game's (--keep-autopilot)"),
}

# Features a game declares; the rest are a build's.
DECLARED = ('gltf', 'emitters', 'characters')


def check(names, where):
    """A game's declared features, checked: a list of known names; an error's text, or None."""
    if not isinstance(names, list) or not all(isinstance(n, str) for n in names):
        return f'{where}: "features" is a list of names'
    for name in names:
        if name not in DECLARED:
            known = ', '.join(DECLARED)
            if name in FEATURES:
                return f'{where}: "{name}" is a build\'s, not a feature a game declares (known: {known})'
            return f'{where}: no feature "{name}" (known: {known})'
    return None


def defines(names):
    """The native build's defines for `names`."""
    return [FEATURES[n].define for n in names]


def exports(names):
    """The web host's exports for `names`."""
    return {FEATURES[n].export for n in names}


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the wgf tool imports: there is nothing to run.').parse_args()
    raise SystemExit('features.py: a module the wgf tool imports: there is nothing to run')
