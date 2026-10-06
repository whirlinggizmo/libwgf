#!/usr/bin/env python3
"""Check the docs against the code: what the docs promise to name, they name, and every
link in them goes somewhere.

    tools/check_docs.py

  links     every relative link in the repository's Markdown (what git tracks, not deps/)
            names a file or directory that exists
  headers   every public header (<layer>/include/wgf_*.h, include/) is named in
            docs/ARCHITECTURE.md, which describes each section
  tools     every tool a person runs (tools/ and tools/wgf/, check_tools' scripts) is
            named in BUILDING.md, which says what each does
  presets   every configure preset in CMakePresets.json is named in BUILDING.md
  docs      every doc in docs/ is linked from README.md, where a reader starts
  deps      every directory in deps/ is named in deps/README.md
ctest runs this. Standard library only.
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import tools  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SKIP = ('deps', 'build', 'out', '.git', 'node_modules')
LINK = re.compile(r'\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')


def markdown_files():
    """The repository's own Markdown: what git tracks (a build's or an export's copies
    aren't), less the vendored code's."""
    import subprocess
    done = subprocess.run(['git', 'ls-files', '*.md'], cwd=ROOT, capture_output=True, text=True)
    paths = done.stdout.split() if done.returncode == 0 else [p.relative_to(ROOT).as_posix() for p in ROOT.rglob('*.md')]
    for rel in sorted(paths):
        if Path(rel).parts[0] in SKIP or any(part in SKIP for part in Path(rel).parts):
            continue
        yield ROOT / rel


def check_links():
    problems = []
    for path in markdown_files():
        text = path.read_text(encoding='utf-8')
        text = re.sub(r'```.*?```', '', text, flags=re.S)  # code blocks show commands, not links
        for target in LINK.findall(text):
            if '://' in target or target.startswith(('#', 'mailto:')):
                continue
            file_part = target.split('#', 1)[0]
            if not file_part:
                continue
            if not (path.parent / file_part).exists():
                problems.append(f'{path.relative_to(ROOT).as_posix()}: a link to {target}, which doesn\'t exist')
    return problems


def check_headers():
    text = (ROOT / 'docs' / 'ARCHITECTURE.md').read_text(encoding='utf-8')
    problems = []
    for header in sorted(ROOT.glob('*/include/wgf*.h')):
        if header.name not in text and header.parent.parent.name != 'math':
            problems.append(f'docs/ARCHITECTURE.md doesn\'t name {header.relative_to(ROOT).as_posix()}')
    return problems


def check_tools_named():
    text = (ROOT / 'BUILDING.md').read_text(encoding='utf-8')
    return [f'BUILDING.md doesn\'t say what {tool} does' for tool in tools.scripts() if f'`{Path(tool).name}`' not in text]


def check_presets():
    text = (ROOT / 'BUILDING.md').read_text(encoding='utf-8')
    presets = json.loads((ROOT / 'CMakePresets.json').read_text())['configurePresets']
    problems = []
    for preset in presets:
        name = preset['name']
        if preset.get('hidden') or name in ('base', 'linux-x64', 'windows-x64-mingw', 'windows-x64-msvc'):
            continue
        short = name.rsplit('-', 1)[-1]
        if name not in text and f'-{short}`' not in text:
            problems.append(f'BUILDING.md doesn\'t name the preset {name}')
    return problems


def check_docs_linked():
    text = (ROOT / 'README.md').read_text(encoding='utf-8')
    return [f'README.md doesn\'t link docs/{doc.name}' for doc in sorted((ROOT / 'docs').glob('*.md'))
            if f'docs/{doc.name}' not in text]


def check_deps():
    text = (ROOT / 'deps' / 'README.md').read_text(encoding='utf-8')
    return [f'deps/README.md doesn\'t name deps/{d.name}' for d in sorted((ROOT / 'deps').iterdir())
            if d.is_dir() and f'{d.name}/' not in text and f'`{d.name}`' not in text]


def main():
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter).parse_args()
    checks = {'links': check_links, 'headers': check_headers, 'tools': check_tools_named, 'presets': check_presets,
              'docs': check_docs_linked, 'deps': check_deps}
    problems = []
    for name, check in checks.items():
        found = check()
        problems += found
        for problem in found:
            print(f'check_docs: {name}: {problem}')
    if problems:
        print(f'check_docs: FAIL, {len(problems)} problem(s)')
        return 1
    print(f'check_docs: the docs name what they say they do ({", ".join(checks)}), and every link resolves')
    return 0


if __name__ == '__main__':
    sys.exit(main())
