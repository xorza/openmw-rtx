"""Spellings the fork's own code refuses, each a fault the compiler passes, checked in the gate.

**A path narrowed with `.string()` or `.generic_string()`**, which on Windows goes through the ANSI code page and
throws for a name outside it: `Files::pathToUnicodeString` spells one as UTF-8."""

import re

from omw.system import ROOT, fork_files, read_text, report

NARROWED = re.compile(r"\.(generic_)?string\(\)")


def narrowed(name: str, text: str) -> list[str]:
    """Each line of `text`, the file `name`, that narrows a path, outside a `//` comment."""
    return [f"{name}:{number}: a path narrowed through the code page; spell it with Files::pathToUnicodeString"
            for number, line in enumerate(text.splitlines(), 1)
            if NARROWED.search(line.split("//", 1)[0])]


def check() -> int:
    return report([line for name in sorted(fork_files())
                   if name.endswith((".cpp", ".hpp"))
                   for line in narrowed(name, read_text(ROOT / name))])
