"""The tools themselves: where they are, and which are modules the others import, for the
tools that check them (tools/check_tools.py, tools/check_docs.py). Standard library only."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FOLDERS = ['tools/*.py', 'tools/bench/*.py', 'tools/wgf/*.py', 'bindings/*/tools/*.py']
MODULES = {'tools/binding.py', 'tools/browser.py', 'tools/examples.py', 'tools/headers.py', 'tools/server.py',
           'tools/tools.py', 'tools/usercache.py', 'tools/variants.py', 'tools/webhost.py', 'tools/wine.py',
           'tools/wgf/cli.py', 'tools/wgf/devserver.py', 'tools/wgf/game.py'}


def tool_files():
    """Every tool, as a path from the root: tools/run_smoke.py."""
    return sorted({p.relative_to(ROOT).as_posix() for pattern in FOLDERS for p in ROOT.glob(pattern)})


def commands():
    """The tools a person runs: every one but the modules."""
    return [t for t in tool_files() if t not in MODULES]


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('tools.py: a module the other tools import: there is nothing to run')
