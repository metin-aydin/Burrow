#!/usr/bin/env python3
import importlib.util, pathlib
spec = importlib.util.spec_from_file_location("rn", pathlib.Path(__file__).with_name("release-notes.py"))
rn = importlib.util.module_from_spec(spec); spec.loader.exec_module(rn)

body = """<!-- template help -->
## New

- Card gallery

## Changed

-

## Fixed

- Vault backup after password change
"""
out = rn.clean_notes(body)
assert out == "## New\n\n- Card gallery\n\n## Fixed\n\n- Vault backup after password change", repr(out)
assert rn.clean_notes("<!-- x -->\n## New\n\n-\n") == ""
print("release-notes ok")
