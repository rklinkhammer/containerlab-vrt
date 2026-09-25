#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


PIN = "dbe85d37155145842da60367af1c4beef8801b0c"
ROOTS = ("include", "tests", "examples", "bench", "cmake", "fuzz")
ACTIVE_DOCUMENTATION = ("README.md", "docs")
REPLACEMENTS = ((b"GRAPHX", b"SDR"), (b"GraphX", b"SDR"), (b"Graphx", b"Sdr"), (b"graphx", b"sdr"))


def run(repository: Path, *arguments: str) -> str:
    return subprocess.check_output(["git", "-C", str(repository), *arguments], text=True).strip()


def migrate(repository: Path) -> None:
    if run(repository, "rev-parse", "HEAD") != PIN:
        raise SystemExit(f"VRT source is not pinned to {PIN}")
    if run(repository, "status", "--short"):
        raise SystemExit("VRT source must be clean before migration")

    files = run(repository, "ls-files", *ROOTS, *ACTIVE_DOCUMENTATION).splitlines()
    for relative in files:
        if relative.startswith("docs/implementation/artifacts/") or relative.endswith("graphx-migration-audit.md"):
            continue
        path = repository / relative
        if not path.is_file():
            continue
        original = path.read_bytes()
        updated = original
        for old, new in REPLACEMENTS:
            updated = updated.replace(old, new)
        if updated != original:
            path.write_bytes(updated)

    rename_roots = ROOTS + ("docs",)
    for path in sorted((repository / root for root in rename_roots if (repository / root).exists()), reverse=True):
        candidates = sorted(path.rglob("*"), key=lambda item: len(item.parts), reverse=True)
        for candidate in candidates:
            if "artifacts" not in candidate.parts and candidate.name != "graphx-migration-audit.md" and "graphx" in candidate.name.lower():
                new_name = candidate.name.replace("graphx", "sdr").replace("GraphX", "SDR")
                candidate.rename(candidate.with_name(new_name))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("repository", type=Path)
    arguments = parser.parse_args()
    migrate(arguments.repository.resolve())


if __name__ == "__main__":
    main()
