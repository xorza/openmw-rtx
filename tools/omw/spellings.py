"""Spellings the fork's own code refuses: each a fault the compiler passes, checked in the gate.

**A path narrowed with `.string()` or `.generic_string()`.** On Windows it goes through the ANSI
code page, which throws for a name outside it, so a checkout or a profile under such a name failed
the harness at start. `Files::pathToUnicodeString` spells a path as UTF-8, and
`Platform::Process::setEnvironmentPath` hands one to the environment whole. The fork's folders
alone: upstream's code is upstream's to change."""

import re

from omw.system import ROOT, output

FORK = (
    "components/rtx/",
    "components/rtxvulkan/",
    "components/myguirtx/",
    "components/crashcatcher/",
    "apps/rtxtool/",
    "apps/openmw/mwrender/rtx/",
    "apps/components_tests/rtx/",
    "apps/components_tests/rtxvulkan/",
    "apps/components_tests/rtxtool/",
    "apps/components_tests/crashcatcher/",
)

NARROWED = re.compile(r"\.(generic_)?string\(\)")


def narrowed(name: str, text: str) -> list[str]:
    """Each line of `text`, the file `name`, that narrows a path, outside a `//` comment."""
    return [f"{name}:{number}: a path narrowed through the code page; spell it with Files::pathToUnicodeString"
            for number, line in enumerate(text.splitlines(), 1)
            if NARROWED.search(line.split("//", 1)[0])]


def check() -> int:
    listed = output(["git", "-C", ROOT, "ls-files", "--cached", "--others", "--exclude-standard", "--",
                     *FORK]).splitlines()
    found = [line for name in listed
             if name.endswith((".cpp", ".hpp")) and (ROOT / name).is_file()
             for line in narrowed(name, (ROOT / name).read_text(encoding="utf-8"))]
    for line in found:
        print(line)
    return 1 if found else 0
