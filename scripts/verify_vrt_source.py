#!/usr/bin/env python3
"""Verify pinned headers and explicit local patches; --apply prepares a clean pin."""
import argparse
import hashlib
import json
import pathlib
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--apply', action='store_true')
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
vendor = root / 'third_party/vrt_framework'
pin = json.loads((root / 'container/vrt-source.json').read_text())

def tree_hash():
    digest = hashlib.sha256()
    for file in sorted((vendor / 'include').rglob('*')):
        if file.is_file():
            digest.update(str(file.relative_to(vendor)).encode() + b'\0' + file.read_bytes())
    return digest.hexdigest()

for patch in pin.get('patches', []):
    assert hashlib.sha256((root / patch['path']).read_bytes()).hexdigest() == patch['sha256'], 'VRT patch mismatch'
if args.apply and tree_hash() != pin['include_tree_sha256']:
    assert tree_hash() == pin['base_include_tree_sha256'], 'Refusing to patch modified or incorrect VRT headers'
    for patch in pin['patches']:
        subprocess.run(['git', 'apply', '--check', str(root / patch['path'])], cwd=vendor, check=True)
        subprocess.run(['git', 'apply', str(root / patch['path'])], cwd=vendor, check=True)
assert tree_hash() == pin['include_tree_sha256'], 'VRT source mismatch; for clean pinned headers run this script with --apply'
assert 'VRT_REVISION=' + pin['revision'] in (root / 'container/dependencies.env').read_text(), 'VRT label mismatch'
print('Verified VRT base:', pin['revision'], 'patched include tree:', pin['include_tree_sha256'])
