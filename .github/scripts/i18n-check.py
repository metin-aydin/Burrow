#!/usr/bin/env python3
"""Fails if a translation catalog has untranslated or obsolete-free missing entries.

Usage: .github/scripts/i18n-check.py app/i18n/*.ts
Run `cmake --build build --target Burrow_lupdate` first so new source strings are in the catalogs.
"""
import sys
import xml.etree.ElementTree as ET

bad = 0
for path in sys.argv[1:]:
    for msg in ET.parse(path).getroot().iter("message"):
        t = msg.find("translation")
        if t is None or t.get("type") in ("unfinished", "vanished") or not (t.text or "").strip():
            if t is not None and t.get("type") == "vanished":
                continue
            print(f"{path}: untranslated: {msg.find('source').text!r}")
            bad += 1
if bad:
    sys.exit(f"{bad} untranslated string(s)")
print("translations complete")
