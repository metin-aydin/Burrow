#!/usr/bin/env python3
"""Turns a pull request description into release notes (same rules as JumpDesk/UPaste):
drops HTML comments, empty bullets and headings with nothing under them.

Usage: release-notes.py < body.md   (prints the cleaned notes; empty if nothing is left)
"""
import re
import sys

HEADING = re.compile(r"^#{1,6}\s")


def clean_notes(text: str) -> str:
    text = re.sub(r"<!--.*?-->", "", text.replace("\r\n", "\n"), flags=re.S)
    lines = [l for l in text.split("\n") if not re.fullmatch(r"\s*[-*]\s*", l)]
    kept = []
    for i, line in enumerate(lines):
        if HEADING.match(line):
            nxt = next((l for l in lines[i + 1:] if l.strip()), None)
            if nxt is None or HEADING.match(nxt):
                continue
        kept.append(line)
    return re.sub(r"\n{3,}", "\n\n", "\n".join(kept)).strip()


if __name__ == "__main__":
    sys.stdout.write(clean_notes(sys.stdin.read()))
