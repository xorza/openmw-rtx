"""`omw inert <file>`: a text an agent wrote after reading upstream, made safe to post in this repository, as
`upstream.yml` posts a merge's reports, printed. The rules go in the order GitHub's agentic workflows' safe outputs
sanitize: characters, protocols, mentions and commands, markup, length. A link may name any host over HTTPS: a
report cites upstream's commits, issues and the sources it read wherever they are. No build is configured for it."""

import os
import re
import sys
import unicodedata
from collections.abc import Callable
from pathlib import Path
from urllib.parse import urlsplit

from omw.system import Refusal, Switches, read_text

# The most a pull request's body or a comment holds: GitHub refuses one past 65,536 characters.
BODY = 60_000

# Characters a page does not show: the soft hyphen, the Arabic letter mark, the Mongolian vowel separator,
# zero-width ones, bidirectional controls, which reorder what is shown, the invisible operators, the byte order
# mark, and Unicode's tag characters, which spell text no font draws.
_INVISIBLE = re.compile("[­؜᠎​-‏‪-‮⁠-⁤⁦-⁯﻿"
                        "\U000e0000-\U000e007f]")
# Control characters but the tab, the line feed and the carriage return.
_CONTROL = re.compile("[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")
# A fenced block or an inline code span, which GitHub shows as written and links nothing in.
_FENCE = re.compile(r"(?m)^[ \t]*(```|~~~)")
# An inline span's backticks are a whole run, so a fence's three are never read as a span's one and one.
_CODE = re.compile(r"(?ms)^[ \t]*(```|~~~).*?^[ \t]*\1[ \t]*$|(?<!`)(`+)(?!`)[^\n]*?(?<!`)\2(?!`)")
# A URL GitHub links: a scheme and `//`, or a bare `www.`; and a Markdown link's target, which may name any scheme.
_URL = re.compile(r"(?i)\b(?:[a-z][a-z0-9+.-]*://|www\.)[^\s<>()\[\]`]+")
_TARGET = re.compile(r"\]\(\s*([^)\s]+)")
_SCHEMED = re.compile(r"(?i)[a-z][a-z0-9+.-]*:")
_SAFE_TARGET = re.compile(r"(?i)(https://|mailto:)")
# A page of another repository that GitHub would write a cross-reference into, were it linked.
_REFERENCING = re.compile(r"/([^/]+/[^/]+)/(?:issues|pull|discussions|commit)/", re.IGNORECASE)
_MENTION = re.compile(r"(?<![\w`@./+-])@[A-Za-z0-9][A-Za-z0-9-]{0,38}(?:/[A-Za-z0-9_.-]+)?")
_CROSS_REFERENCE = re.compile(r"(?<![\w/`])[A-Za-z0-9][\w.-]*/[\w.-]+#[0-9]+\b")
_COMMAND = re.compile(r"(?m)^([ \t]*)(/[A-Za-z][\w-]*)")
# An item of this repository by its number, as GitHub links one: `#12` or `GH-12`. Not an HTML entity's digits.
_NUMBER = re.compile(r"(?i)(?<![\w/`&#])(?:#|GH-)[0-9]+\b")
REMOVED = "[URL removed: not HTTPS]"
TRUNCATED = "\n\n[Text cut at its length limit]"


def inert(args: list[str]) -> int:
    switches = Switches("inert", "")
    switches.add_argument("text", type=Path)
    parsed = switches.parse_args(args)
    # As bytes of UTF-8, whatever the locale's encoding: the text goes back into a file GitHub is handed.
    sys.stdout.buffer.write(neutral(read_text(parsed.text), _repository(), BODY).encode())
    return 0


def _repository() -> str:
    """The repository the run stands in, which Actions names in `GITHUB_REPOSITORY`."""
    named = os.environ.get("GITHUB_REPOSITORY", "")
    if "/" not in named:
        raise Refusal("GITHUB_REPOSITORY names no repository: `owner/name`, as Actions sets it")
    return named


def _code(text: str) -> str:
    """`text` as one inline code span, fenced by more backticks than it holds."""
    longest = max((len(run) for run in re.findall(r"`+", text)), default=0)
    fence = "`" * (longest + 1)
    return f"{fence} {text} {fence}"


def _outside_code(text: str, change: Callable[[str], str]) -> str:
    """`text` with `change` applied to what stands outside its code, which GitHub shows as written."""
    spans = [match[0] for match in _CODE.finditer(text)]
    # `split` returns the text between matches and then each match's two groups, so the prose is every third piece.
    prose = _CODE.split(text)[::3]
    assert len(prose) == len(spans) + 1
    out = [change(prose[0])]
    for span, piece in zip(spans, prose[1:], strict=True):
        out += [span, change(piece)]
    return "".join(out)


def _url(url: str, repository: str) -> str:
    parts = urlsplit(url if "://" in url else f"https://{url}")
    if parts.scheme.lower() != "https":
        return REMOVED
    referenced = _REFERENCING.match(parts.path) if (parts.hostname or "").lower() == "github.com" else None
    if referenced is not None and referenced[1].lower() != repository.lower():
        return _code(url)
    return url


def _links(text: str, repository: str) -> str:
    text = _TARGET.sub(lambda match: "](" + REMOVED if _SCHEMED.match(match[1]) and not
                       _SAFE_TARGET.match(match[1]) else match[0], text)
    return _URL.sub(lambda match: _url(match[0], repository), text)


def _inert(text: str) -> str:
    """The numbers ahead of the other rules, whose code spans a number inside one would break out of."""
    text = _NUMBER.sub(lambda match: _code(match[0]), text)
    text = _MENTION.sub(lambda match: _code(match[0]), text)
    text = _CROSS_REFERENCE.sub(lambda match: _code(match[0]), text)
    text = _COMMAND.sub(lambda match: match[1] + _code(match[2]), text)
    return text.replace("<", "&lt;")


def neutral(text: str, repository: str, limit: int) -> str:
    """`text`, which an agent wrote about upstream's work, made safe to post in `repository`: invisible and control
    characters out, links over HTTPS alone, no mention that notifies anyone, no cross-reference written into another
    repository, **no `#12` that links this repository's item of that number** — the number is upstream's tracker's,
    and GitHub would close this repository's item at the merge where a closing word stands before it — no slash
    command, no markup a page would render as HTML, and at most `limit` characters. The characters go first, so
    nothing a later rule reads is spelled around them, then the links, then the rest over what is still prose, then
    the length."""
    text = _CONTROL.sub("", _INVISIBLE.sub("", unicodedata.normalize("NFC", text)))
    text = _outside_code(text, lambda prose: _links(prose, repository))
    # A link made a code span above is code now, which the rules below leave as it is.
    return cut(_outside_code(text, _inert), limit)


def cut(text: str, limit: int) -> str:
    """`text` in at most `limit` characters, said where it was cut, with every fence closed, so nothing after it
    in the comment is swallowed into its block."""
    assert limit > len(TRUNCATED) + 4, limit
    if len(text) > limit:
        text = text[:limit - len(TRUNCATED) - 4] + TRUNCATED
    # A closed fence is code, so one found in the prose is a fence that never closes.
    unclosed = _FENCE.search("".join(_CODE.split(text)[::3]))
    if unclosed is not None:
        text += "\n" + unclosed[1]
    return text
