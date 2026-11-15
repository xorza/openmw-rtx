"""Text across the review's two boundaries. **In**, what a person wrote is cut to what a reader of the page sees
(`visible`), as the action does to what it fetches itself, so an instruction hidden from the maintainer is not one
only the agent reads. **Out**, what the agent wrote is made inert before it is posted (`neutral`), in the order
GitHub's agentic workflows' safe outputs sanitize: characters, protocols, mentions and commands, markup, length. A
link may name any host over HTTPS: the review cites papers, specifications and other engines wherever they are, and
the agent holds nothing a link could carry away."""

import re
import unicodedata
from collections.abc import Callable
from dataclasses import dataclass
from urllib.parse import urlsplit

# Characters a page does not show: zero-width ones, bidirectional controls, which reorder what is shown, Unicode's
# tag characters, which spell text no font draws, and the soft hyphen.
_INVISIBLE = re.compile("[­؜᠎​-‏‪-‮⁠-⁤⁦-⁯﻿"
                        "\U000e0000-\U000e007f]")
# Control characters but the tab, the line feed and the carriage return.
_CONTROL = re.compile("[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")
# An HTML comment, which GitHub does not show; one never closed hides the rest of the text.
_COMMENT = re.compile(r"<!--.*?(?:-->|\Z)", re.DOTALL)
# What a hidden comment is kept to in the list of what was taken out.
HIDDEN_SHOWN = 300


@dataclass(frozen=True)
class Visible:
    """A text as a reader sees it, and what was taken out of it to make it so."""

    text: str
    hidden: list[str]


def visible(text: str) -> Visible:
    hidden = [f"an HTML comment: {comment[:HIDDEN_SHOWN]!r}" for comment in _COMMENT.findall(text)]
    text = _COMMENT.sub("", text)
    if invisible := len(_INVISIBLE.findall(text)) + len(_CONTROL.findall(text)):
        hidden.append(f"{invisible} invisible or control characters")
    return Visible(_CONTROL.sub("", _INVISIBLE.sub("", text)), hidden)


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
REMOVED = "[URL removed: not HTTPS]"
TRUNCATED = "\n\n[Text cut at its length limit]"


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
    text = _MENTION.sub(lambda match: _code(match[0]), text)
    text = _CROSS_REFERENCE.sub(lambda match: _code(match[0]), text)
    text = _COMMAND.sub(lambda match: match[1] + _code(match[2]), text)
    return text.replace("<", "&lt;")


def neutral(text: str, repository: str, limit: int) -> str:
    """`text`, which the agent wrote, made safe to post in `repository`: invisible and control characters out, links
    over HTTPS alone, no mention that notifies anyone, no cross-reference written into another repository, no slash
    command, no markup a page would render as HTML, and at most `limit` characters."""
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
