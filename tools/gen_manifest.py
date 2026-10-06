#!/usr/bin/env python3
"""Write the asset manifests for a directory tree (wgf_asset.h, wgf_asset_set_manifest; wgrender's
tools/gen_manifest.py, its version key renamed "wgf_manifest"): a manifest.json in DIR
and in every directory under it, each giving the sha256 of every file beside it and of
each subdirectory's own manifest.json.

    tools/gen_manifest.py DIR [--quiet]

A program that calls wgf_asset_set_manifest("manifest.json") with DIR as its asset
host then fetches only the files whose hash changed, and asks the host about nothing
else but the root. Run it on what is deployed, after the last file is in place (a
file added later is not listed, and is cached as the cache mode says). Idempotent:
the same tree writes the same bytes, and a manifest.json already there is replaced,
never listed. Standard library only; any build tool can write the same JSON instead.
"""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

NAME = 'manifest.json'


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return 'sha256:' + digest.hexdigest()


def write_tree(directory):
    """Write `directory`'s manifest after its subdirectories'; return (its hash, the
    number of manifests written, the number of files listed)."""
    files, dirs = {}, {}
    manifests = listed = 0
    for entry in sorted(os.scandir(directory), key=lambda e: e.name):
        if entry.is_dir():
            dirs[entry.name], m, n = write_tree(Path(entry.path))
            manifests, listed = manifests + m, listed + n
        elif entry.is_file() and entry.name != NAME:
            files[entry.name] = sha256_of(entry.path)
            listed += 1
    text = json.dumps({'wgf_manifest': 1, 'files': files, 'dirs': dirs}, indent=1, sort_keys=True,
                      ensure_ascii=False) + '\n'
    data = text.encode('utf-8')
    target = directory / NAME
    if not target.exists() or target.read_bytes() != data:
        target.write_bytes(data)
    return 'sha256:' + hashlib.sha256(data).hexdigest(), manifests + 1, listed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dir')
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args()
    root = Path(args.dir)
    if not root.is_dir():
        sys.exit(f'gen_manifest: {root} is not a directory')
    _, manifests, listed = write_tree(root)
    if not args.quiet:
        print(f'gen_manifest: {listed} file(s) in {manifests} manifest(s) under {root}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
