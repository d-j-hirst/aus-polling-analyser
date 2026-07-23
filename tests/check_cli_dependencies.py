#!/usr/bin/env python3
"""Reject GUI, XML, live-parser and platform dependencies in the CLI closure."""

from __future__ import annotations

import re
import sys
from pathlib import Path


INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
FORBIDDEN_PATTERNS = (
    (re.compile(r'^\s*#\s*include\s*[<"]wx(?:/|\\)', re.MULTILINE),
     "wxWidgets"),
    (re.compile(r'^\s*#\s*include\s*[<"]tinyxml2(?:\.h)?[>"]',
                re.MULTILINE | re.IGNORECASE),
     "TinyXML"),
    (re.compile(r'^\s*#\s*include\s*"LivePreparation\.h"', re.MULTILINE),
     "concrete live preparation"),
    (re.compile(r'^\s*#\s*include\s*"LiveV2\.h"', re.MULTILINE),
     "concrete live analysis"),
    (re.compile(r'^\s*#\s*include\s*"ElectionData\.h"', re.MULTILINE),
     "live election parsing"),
    (re.compile(r'^\s*#\s*include\s*"Beep\.h"', re.MULTILINE),
     "platform sound"),
    (re.compile(r'^\s*#\s*include\s*[<"]windows\.h[>"]',
                re.MULTILINE | re.IGNORECASE),
     "Windows APIs"),
    (re.compile(r"\b(?:MessageBeep|PlaySound)\s*\("),
     "platform sound"),
)
FORBIDDEN_SOURCES = {
    "LivePreparationBridge.cpp",
    "MacroRunner.cpp",
    "PollingProject.cpp",
}
CLI_ENTRY_SOURCE = "PollingCli.cpp"


def fail(message: str) -> None:
    print(f"CLI dependency audit failed: {message}", file=sys.stderr)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    manifest = root / "cli-sources.txt"
    if not manifest.is_file():
        fail(f"missing source manifest: {manifest}")
        return 1

    sources = [
        line.strip()
        for line in manifest.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    errors: list[str] = []
    if len(sources) != len(set(sources)):
        errors.append("cli-sources.txt contains duplicate entries")

    forbidden_present = FORBIDDEN_SOURCES.intersection(sources)
    for source in sorted(forbidden_present):
        errors.append(f"cli-sources.txt includes prohibited source {source}")

    pending = [root / source for source in [*sources, CLI_ENTRY_SOURCE]]
    visited: set[Path] = set()
    while pending:
        path = pending.pop()
        try:
            path = path.resolve(strict=True)
        except FileNotFoundError:
            errors.append(f"listed or included file does not exist: {path}")
            continue
        if path in visited:
            continue
        visited.add(path)
        try:
            path.relative_to(root)
        except ValueError:
            errors.append(f"local include escapes the repository: {path}")
            continue

        text = path.read_text(encoding="utf-8-sig")
        relative = path.relative_to(root)
        for pattern, dependency in FORBIDDEN_PATTERNS:
            if pattern.search(text):
                errors.append(f"{relative} depends on {dependency}")

        for include in INCLUDE_PATTERN.findall(text):
            included_path = path.parent / include
            if included_path.is_file():
                pending.append(included_path)

    for error in errors:
        fail(error)
    if errors:
        return 1

    print(
        f"CLI dependency audit passed: {len(sources)} shared sources plus "
        f"{CLI_ENTRY_SOURCE}, "
        f"{len(visited)} local source/header files"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
