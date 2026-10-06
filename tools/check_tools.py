#!/usr/bin/env python3
"""Check the tools: named for what they do, never importing a script, and answering
--help with their usage and doing nothing else.

    tools/check_tools.py

A tool is every .py in the tool folders (FOLDERS: tools/, tools/bench/, and each
binding's tools/). A module is one the others import (MODULES); every other is a
script. Checks, as wgrender's check_rules.py does with its own:

  names     a script is <verb>_<noun>, its verb from VERBS: what it does, and to what;
            a module is one word. A MODULES entry with no file fails, so the list
            can't go stale
  imports   no file imports a script, read with Python's own parser (ast): what tools
            share goes in a module
  --help    each tool, run with --help, exits 0 with a usage line within SECONDS and
            leaves nothing in the per-user cache (tools/usercache.py), which it points
            at an empty folder of the tool's own; run with an argument it doesn't
            take, it exits non-zero. A tool that takes --help as a name or a ref, or
            runs anyway, shows here before it downloads or changes something

The tools run in parallel. ctest runs this. Standard library only.
"""
import argparse
import ast
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SECONDS = 30
FOLDERS = ['tools/*.py', 'tools/bench/*.py', 'tools/wgf/*.py', 'bindings/*/tools/*.py']
MODULES = {'tools/binding.py', 'tools/browser.py', 'tools/examples.py', 'tools/headers.py', 'tools/server.py',
           'tools/usercache.py', 'tools/variants.py', 'tools/webhost.py', 'tools/wine.py',
           'tools/wgf/cli.py', 'tools/wgf/devserver.py', 'tools/wgf/game.py'}
# What a script's name may start with: what it does. Then what it does it to. (wgrender's, and stage)
VERBS = ('build', 'check', 'compare', 'compress', 'create', 'drive', 'fetch', 'finish', 'gen', 'list', 'measure',
         'pack', 'run', 'serve', 'setup', 'show', 'stage', 'update', 'verify', 'watch')


def tool_files():
    return sorted({p.relative_to(ROOT).as_posix() for pattern in FOLDERS for p in ROOT.glob(pattern)})


def check_names(files):
    problems = [f'{m}: listed in MODULES, but there is no such file' for m in sorted(MODULES - set(files))]
    for tool in files:
        stem = Path(tool).stem
        if tool in MODULES:
            if '_' in stem:
                problems.append(f'{tool}: a module is one word')
            continue
        verb, sep, noun = stem.partition('_')
        if verb not in VERBS or not sep or not noun:
            problems.append(f'{tool}: a script is <verb>_<noun> ({", ".join(VERBS)}): what it does, and to what')
    return problems


def check_imports(files):
    scripts = {Path(t).stem: t for t in files if t not in MODULES}
    problems = set()
    for tool in files:
        for node in ast.walk(ast.parse((ROOT / tool).read_text(encoding='utf-8'), tool)):
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom):
                names = [node.module or '']
            else:
                continue
            problems |= {f'{tool} imports {scripts[n]}: move what it shares into a module'
                         for n in names if n in scripts and scripts[n] != tool}
    return sorted(problems)


def check_answers(tool):
    """--help: exit 0, a usage line, in time, nothing cached; an unknown argument: refused."""
    problems = []
    with tempfile.TemporaryDirectory(prefix='libwgf-check_tools-') as cache:
        env = dict(os.environ, LIBWGF_CACHE_DIR=cache)

        def run(arg):
            return subprocess.run([sys.executable, str(ROOT / tool), arg], cwd=ROOT, env=env, text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=SECONDS,
                                  stdin=subprocess.DEVNULL)
        try:
            done = run('--help')
            if done.returncode != 0:
                problems.append(f'{tool}: --help exited {done.returncode}')
            elif not done.stdout.lstrip().startswith('usage:'):
                problems.append(f'{tool}: --help printed no usage line: {done.stdout.strip()[:80]!r}')
            if run('--no-such-argument').returncode == 0:
                problems.append(f'{tool}: an argument it does not take is accepted')
        except subprocess.TimeoutExpired as expired:
            problems.append(f'{tool}: still running after {SECONDS} s with {expired.cmd[-1]}')
        left = sorted(p.name for p in Path(cache).iterdir())
        if left:
            problems.append(f'{tool}: left {", ".join(left)} in the cache')
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args()
    files = tool_files()
    failures = check_names(files) + check_imports(files)
    with ThreadPoolExecutor(8) as pool:
        failures += [p for problems in pool.map(check_answers, files) for p in problems]
    for failure in failures:
        print(f'check_tools: {failure}')
    if failures:
        print(f'check_tools: FAIL, {len(failures)} problems in {len(files)} tools')
        return 1
    scripts = len(files) - len(MODULES)
    print(f'check_tools: {scripts} scripts named <verb>_<noun> and {len(MODULES)} modules one word, none importing '
          'a script; each answers --help, does nothing else, and refuses an argument it does not take')
    return 0


if __name__ == '__main__':
    sys.exit(main())
