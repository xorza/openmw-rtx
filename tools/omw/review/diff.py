"""A pull request's diff, parsed once: what GitHub takes an inline comment on (`commentable`), and the diff as the
agent reads it, each line under the number it has in the pull request's file (`numbered`). An agent left to count
from a hunk's header, or to read the number off a file, cites the line of whichever copy of the file it read, and
the tree it stands in is `master`'s; numbering the lines for it is what PR-Agent does for the same reason."""

import re
from dataclasses import dataclass, field

_HUNK = re.compile(r"@@ -\d+(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


@dataclass(frozen=True)
class Line:
    """One line of a hunk: its kind, ` `, `+` or `-`, or `\\` for git's note that the line before ends its file
    without a newline; its number on the right side, none where it was removed or is the note; and its text."""

    kind: str
    number: int | None
    text: str


@dataclass(frozen=True)
class Hunk:
    header: str
    lines: list[Line]


@dataclass
class File:
    """One file's part of a diff. `path` is its name on the right side, none where no line of it takes an inline
    comment: the change deletes it, renames it whole, changes its mode alone, it is binary, or git had to quote its
    name."""

    header: str
    path: str | None
    hunks: list[Hunk] = field(default_factory=list)


def parse(diff: str) -> list[File]:
    """**The files of a unified diff**. A hunk is read by the counts its header gives, so a line it holds is never
    taken for a header, as `+++ i` would be, the line `++ i` added. Git ends a name with a space in it with a tab,
    which is not part of it."""
    files: list[File] = []
    lines: list[Line] = []
    old = new = at = 0
    for text in diff.splitlines():
        if old > 0 or new > 0:
            kind = text[:1]
            if kind in (" ", "-"):
                old -= 1
            if kind in (" ", "+"):
                new -= 1
            lines.append(Line(kind, at if kind in (" ", "+") else None, text[1:]))
            at += kind in (" ", "+")
        elif text.startswith("diff --git "):
            files.append(File(text, None))
        elif text.startswith("+++ ") and files:
            name = text[4:].removesuffix("\t")
            files[-1].path = name[2:] if name.startswith("b/") else None
        elif (found := _HUNK.match(text)) and files:
            old, at, new = int(found[1] or 1), int(found[2]), int(found[3] or 1)
            lines = []
            files[-1].hunks.append(Hunk(text, lines))
    return files


def commentable(files: list[File]) -> dict[str, frozenset[int]]:
    """The lines of each file GitHub takes an inline comment on: those of the right side a hunk shows, added or
    kept as context."""
    return {file.path: frozenset(line.number for hunk in file.hunks for line in hunk.lines if line.number is not None)
            for file in files if file.path is not None}


def numbered(files: list[File]) -> str:
    """The diff as the agent reads it: each file under its name, and each line of a hunk after the number it has in
    the pull request's file, blank where the change removes it."""
    out = []
    for file in files:
        out.append(f"## {file.path if file.path is not None else file.header.removeprefix('diff --git ')}")
        if file.path is None:
            out.append("(no line of it takes a finding: deleted, renamed whole, its mode alone changed, binary, or a "
                       "name git quoted)")
        for hunk in file.hunks:
            out.append(hunk.header)
            out += [f"{'' if line.number is None else line.number:>6} {line.kind}{line.text}" for line in hunk.lines]
        out.append("")
    return "\n".join(out)
