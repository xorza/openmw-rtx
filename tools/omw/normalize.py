"""**The tree's one spelling, made by rewriting and not by hand**: every integer type as `<cstdint>`'s fixed widths,
`std::`-qualified, and every include of another folder spelled from the root, of the file's own folder quoted.
Deterministic and idempotent, so `omw format` applies it, `omw format --check` fails a file it would change, and
`omw merge-file` lays out the base and both sides with it before merging: an upstream edit arrives in the tree's
spelling and conflicts only where it overlaps the fork's, as git's `merge.renormalize` does for line endings.

**Outside strings, comments and the headers GLSL reads**, which spell their own: a rewrite that reached a string would
change what the program prints, and one that reached a comment what it says."""

import itertools
import re
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from omw.system import ROOT

# The rules the tree is held to, each switched on by the commit that first runs it over the tree, so that
# `omw format --check` never fails a file no commit has rewritten yet.
ENABLED: tuple[str, ...] = ("includes", "integers")
RULES = ("includes", "integers")

# The headers GLSL reads as well, which spell C++ and GLSL alike (`shaders/portable.h`).
GLSL_HEADERS = ("components/rtx/shaders/", "components/rtxvulkan/shaders/shared/")

# A raw string's prefix and delimiter; a string or character literal with any encoding prefix; a comment.
_LEXEME = re.compile(
    r"""(?P<raw>\b(?:u8|u|U|L)?R"(?P<delim>[^()\\\s]{0,16})\()"""
    r"""|(?P<string>(?:\b(?:u8|u|U|L))?"(?:[^"\\\n]|\\.)*")"""
    r"""|(?P<char>(?:\b(?:u8|u|U|L))?'(?:[^'\\\n]|\\.)+')"""
    r"""|(?P<line>//[^\n]*)"""
    r"""|(?P<block>/\*.*?\*/)""",
    re.DOTALL)


@dataclass(frozen=True)
class Span:
    """A stretch of a file: `code` where a rewrite may reach, else a literal or a comment, kept as it stands."""

    text: str
    code: bool


def spans(text: str) -> list[Span]:
    """`text` cut into code and what is not code, in order; joined, the pieces are `text`."""
    found: list[Span] = []
    at = 0
    while True:
        match = _LEXEME.search(text, at)
        if match is None:
            break
        start = match.start()
        if match.group("raw") is not None:
            close = text.find(f"){match.group('delim')}\"", match.end())
            end = len(text) if close < 0 else close + len(match.group("delim")) + 2
        elif match.group("char") is not None and start > 0 and (text[start - 1].isalnum() or text[start - 1] == "_"):
            # A digit separator (`1'000`) is no character literal: past the separator and on.
            if start > at:
                found.append(Span(text[at:start + 1], True))
            at = start + 1
            continue
        else:
            end = match.end()
        if start > at:
            found.append(Span(text[at:start], True))
        found.append(Span(text[start:end], False))
        at = end
    if at < len(text):
        found.append(Span(text[at:], True))
    return found


# A run of the keywords an integer type is spelled with, a space or more apart, and one at either end of a stretch of
# code, which a comment may part from the rest of its run.
_KEYWORD = r"\b(?:signed|unsigned|short|long|int|char)\b"
_INTEGER_RUN = re.compile(rf"{_KEYWORD}(?:\s+{_KEYWORD})*")
_ENDS_IN_KEYWORD = re.compile(rf"{_KEYWORD}\s*$")
_STARTS_WITH_KEYWORD = re.compile(rf"^\s*{_KEYWORD}")

# Each spelling a run of keywords comes to, as a sorted tuple of its words; a run absent here is left as it stands:
# `char`, which is text, and anything with `long`, whose width differs between Linux and Windows and is chosen by hand.
_INTEGERS = {
    ("int",): "std::int32_t",
    ("signed",): "std::int32_t",
    ("int", "signed"): "std::int32_t",
    ("unsigned",): "std::uint32_t",
    ("int", "unsigned"): "std::uint32_t",
    ("short",): "std::int16_t",
    ("int", "short"): "std::int16_t",
    ("short", "signed"): "std::int16_t",
    ("int", "short", "signed"): "std::int16_t",
    ("short", "unsigned"): "std::uint16_t",
    ("int", "short", "unsigned"): "std::uint16_t",
    ("char", "unsigned"): "std::uint8_t",
    ("char", "signed"): "std::int8_t",
}

# The C library's names `<cstdint>` and `<cstddef>` declare in `std`, which a file spells unqualified.
_STD_NAMES = re.compile(r"(?<![\w:.>])\b(u?int(?:_least|_fast)?(?:8|16|32|64)_t|u?intptr_t|u?intmax_t|size_t"
                        r"|ptrdiff_t|max_align_t)\b")

# `int main(`, which the language spells so.
_MAIN = re.compile(r"\bint(\s+main\s*\()")

# The lint exception that marks a keyword spelled on purpose, which the rewrite honours as clang-tidy does.
_KEPT_HERE = "NOLINT(google-runtime-int)"
_KEPT_NEXT = "NOLINTNEXTLINE(google-runtime-int)"

# A directive whose words are a path or a message, not code.
_WORDLESS = re.compile(r"^[ \t]*#[ \t]*(?:include|error|warning|pragma|line)\b")

# The headers a name of each kind needs.
_CSTDINT = re.compile(r"\bstd::u?int(?:_least|_fast)?(?:8|16|32|64)_t\b|\bstd::u?int(?:ptr|max)_t\b")
_CSTDDEF = re.compile(r"\bstd::(?:size_t|ptrdiff_t|max_align_t)\b")
_INCLUDE = re.compile(r'^(?P<lead>[ \t]*#[ \t]*include[ \t]*)(?P<open>[<"])(?P<path>[^>"]+)[>"]', re.MULTILINE)

# Where the language or a library spells a type as a string the rewrite would part from its declaration.
_STRINGIFIED = re.compile(r"\b(?:Q_ARG|Q_RETURN_ARG|SIGNAL|SLOT)\s*\(")


class Unnormalizable(Exception):
    """A file the rules cannot rewrite safely, with the reason; nothing of it is changed."""


def _spelled(match: re.Match[str]) -> str:
    return _INTEGERS.get(tuple(sorted(match.group(0).split())), match.group(0))


def _rewrite_integers(code: str, first_line: int = 0, kept: frozenset[int] = frozenset()) -> str:
    """`code`, whose first line is the file's `first_line`, rewritten but on the lines of `kept` and a wordless
    directive's. **Whole and not a line at a time**, so a type spelled over two lines is one run; a line left as it
    stands is held aside behind a marker no keyword matches, which also ends any run beside it."""
    held: list[str] = []
    masked = []
    for at, line in enumerate(code.splitlines(keepends=True)):
        if first_line + at in kept or _WORDLESS.match(line):
            body = line.rstrip("\r\n")
            masked.append(f"\x01{len(held)}\x01{line[len(body):]}")
            held.append(body)
        else:
            masked.append(line)
    rewritten = _MAIN.sub(lambda match: "\x02" + match.group(1), "".join(masked))
    rewritten = _INTEGER_RUN.sub(_spelled, rewritten)
    rewritten = _STD_NAMES.sub(lambda match: "std::" + match.group(1), rewritten).replace("\x02", "int")
    return re.sub(r"\x01(\d+)\x01", lambda match: held[int(match.group(1))], rewritten)


def _kept_lines(text: str) -> frozenset[int]:
    """The lines, from nought, a `google-runtime-int` exception keeps as they are spelled."""
    kept = set()
    for number, line in enumerate(text.splitlines()):
        if _KEPT_HERE in line:
            kept.add(number)
        if _KEPT_NEXT in line:
            kept.add(number + 1)
    return frozenset(kept)


def _argument_of(text: str, open_paren: int) -> str:
    """The text between the parenthesis at `open_paren` and the one that closes it."""
    depth = 0
    for at in range(open_paren, len(text)):
        depth += text[at] == "("
        depth -= text[at] == ")"
        if depth == 0:
            return text[open_paren + 1:at]
    return text[open_paren + 1:]


def _needs_header(text: str, pattern: re.Pattern[str], header: str) -> bool:
    if not any(span.code and pattern.search(span.text) for span in spans(text)):
        return False
    return re.search(rf"^[ \t]*#[ \t]*include[ \t]*<{re.escape(header)}>", text, re.MULTILINE) is None


def _add_include(text: str, header: str) -> str:
    """`#include <header>` beside the file's standard headers, or a block of its own after its own header or its
    `#pragma once`; clang-format orders it within its block."""
    line = f"#include <{header}>\n"
    for match in _INCLUDE.finditer(text):
        path = match.group("path")
        if match.group("open") == "<" and re.fullmatch(r"[a-z_0-9]+", path):
            return text[:match.start()] + line + text[match.start():]
    first = _INCLUDE.search(text)
    pragma = re.search(r"^#pragma once[ \t]*\n", text, re.MULTILINE)
    if first is not None and first.group("open") == '"' and pragma is None:
        end = text.index("\n", first.start()) + 1
        return text[:end] + "\n" + line + text[end:]
    if pragma is not None:
        return text[:pragma.end()] + "\n" + line + text[pragma.end():]
    if first is not None:
        return text[:first.start()] + line + "\n" + text[first.start():]
    return line + "\n" + text


def _integers(name: str, text: str) -> str:
    pieces = spans(text)
    for piece in pieces:
        stringified = (_argument_of(piece.text, match.end() - 1) for match in _STRINGIFIED.finditer(piece.text))
        if piece.code and any(_rewrite_integers(argument) != argument for argument in stringified):
            raise Unnormalizable(f"{name}: names a type inside Q_ARG, SIGNAL or SLOT, which compare it as a string; "
                                 "spell the call with a function pointer or a lambda")
    code = [piece.text for piece in pieces if piece.code]
    for before, after in itertools.pairwise(code):
        if _ENDS_IN_KEYWORD.search(before) and _STARTS_WITH_KEYWORD.search(after):
            raise Unnormalizable(f"{name}: a comment parts an integer type's keywords; join them")
    kept = _kept_lines(text)
    parts = []
    line = 0
    for piece in pieces:
        parts.append(_rewrite_integers(piece.text, line, kept) if piece.code else piece.text)
        line += piece.text.count("\n")
    rewritten = "".join(parts)
    if rewritten == text:
        return text
    for pattern, header in ((_CSTDINT, "cstdint"), (_CSTDDEF, "cstddef")):
        if _needs_header(rewritten, pattern, header):
            rewritten = _add_include(rewritten, header)
    return rewritten


def _includes(name: str, text: str, exists: "Exists") -> str:
    folder = PurePosixPath(name).parent

    def target_of(match: re.Match[str]) -> PurePosixPath | None:
        """The tree's file an include names, from the root: a quoted path from the file's folder, else from the root;
        an angled one from the root. Nothing where it names none, as a library's header and a generated one."""
        path = PurePosixPath(match.group("path"))
        if match.group("open") == '"':
            beside = _resolved(folder / path)
            if beside is not None and exists(beside):
                return beside
        if path.parts and path.parts[0] in ("apps", "components") and exists(path):
            return path
        return None

    def spelled(match: re.Match[str]) -> str:
        target = target_of(match)
        if target is None:
            return match.group(0)
        if target.parent == folder:
            return f'{match.group("lead")}"{target.name}"'
        return f"{match.group('lead')}<{target.as_posix()}>"

    return _INCLUDE.sub(spelled, text)


def _resolved(path: PurePosixPath) -> PurePosixPath | None:
    """`path` with its `..` and `.` folded, or nothing where it climbs out of the tree."""
    parts: list[str] = []
    for part in path.parts:
        if part == "..":
            if not parts:
                return None
            parts.pop()
        elif part != ".":
            parts.append(part)
    return PurePosixPath(*parts)


class Exists:
    """Whether a path from the root names a file of the tree, asked of the file system once a path."""

    def __init__(self, root: Path = ROOT):
        self._root = root
        self._known: dict[PurePosixPath, bool] = {}

    def __call__(self, path: PurePosixPath) -> bool:
        if path not in self._known:
            self._known[path] = (self._root / path).is_file()
        return self._known[path]


def applies(name: str) -> bool:
    """Whether the rules reach `name`, a path from the root: the C++ of `apps/` and `components/`, the headers GLSL
    reads aside."""
    return (name.startswith(("apps/", "components/")) and name.endswith((".cpp", ".hpp", ".h"))
            and not name.startswith(GLSL_HEADERS))


def normalized(name: str, text: str, rules: Iterable[str] | None = None, exists: Exists | None = None) -> str:
    """`text`, the file `name`, rewritten by `rules`, the tree's own (`ENABLED`) unless named; `text` itself where they
    change nothing or do not reach it. Raises `Unnormalizable` for a file a rule cannot rewrite safely."""
    if not applies(name):
        return text
    chosen = set(ENABLED if rules is None else rules)
    unknown = chosen - set(RULES)
    assert not unknown, f"no rule is named {sorted(unknown)}"
    if "includes" in chosen:
        text = _includes(name, text, exists or Exists())
    if "integers" in chosen:
        text = _integers(name, text)
    return text
