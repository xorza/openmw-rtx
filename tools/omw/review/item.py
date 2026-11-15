"""What the review reads and answers: an item, the marker its answer carries, and the name its jobs go by, which
the survey counts the budget from. Nothing here asks GitHub anything."""

import re
from collections.abc import Iterable
from dataclasses import dataclass
from datetime import datetime

from omw.system import Refusal

# The account `GITHUB_TOKEN` writes as: a marker any other account wrote counts for nothing.
BOT = "github-actions[bot]"
# The login GraphQL gives the same account, which drops the suffix.
BOT_GRAPHQL = "github-actions"
# An issue's head, which it has none of.
NO_HEAD = "-"
# The only base a pull request is read against.
BASE = "master"
# The label that keeps an item from the review.
OPT_OUT = "no-auto-review"


@dataclass(frozen=True)
class Key:
    """An item by its kind and number, spelled `pr:45` or `issue:53` on a command line and in a marker."""

    kind: str
    number: int

    def __post_init__(self) -> None:
        assert self.kind in ("pr", "issue"), self.kind
        assert self.number > 0, self.number

    @staticmethod
    def parse(text: str) -> "Key":
        found = re.fullmatch(r"(pr|issue):([1-9][0-9]*)", text)
        if found is None:
            raise Refusal(f"an item is `pr:<number>` or `issue:<number>`, and {text!r} is neither")
        return Key(found[1], int(found[2]))

    def __str__(self) -> str:
        return f"{self.kind}:{self.number}"


@dataclass(frozen=True)
class Marker:
    """What an answer says it answered, hidden at its end: the item, the head it read, and whether that head was a
    draft's, so a pull request turned ready is read once more."""

    key: Key
    head: str
    draft: bool

    def __post_init__(self) -> None:
        assert self.head == NO_HEAD or re.fullmatch(r"[0-9a-f]{40}", self.head), self.head
        if self.key.kind == "issue":
            assert self.head == NO_HEAD
            assert not self.draft

    def render(self) -> str:
        return f"<!-- auto-review {self.key} head={self.head} draft={'yes' if self.draft else 'no'} -->"


_MARKER = re.compile(r"<!-- auto-review (pr|issue):([1-9][0-9]*) head=([0-9a-f]{40}|-) draft=(yes|no) -->")


def markers(posts: Iterable[tuple[str, str]], key: Key) -> list[Marker]:
    """The markers of `key` among `posts`, each its author's login and its body, that the workflow's own account
    wrote: anyone can paste one, and a pasted one must not stand for an answer."""
    found = []
    for author, body in posts:
        if author != BOT:
            continue
        for match in _MARKER.finditer(body or ""):
            marker = Marker(Key(match[1], int(match[2])), match[3], match[4] == "yes")
            if marker.key == key:
                found.append(marker)
    return found


@dataclass(frozen=True)
class Item:
    """An open item as the survey weighs it."""

    key: Key
    author: str
    association: str
    bot: bool
    labels: frozenset[str]
    draft: bool
    head: str
    base: str
    created: datetime
    updated: datetime


@dataclass(frozen=True)
class Attempt:
    """One read of an item, out of the workflow's run history: what it read, for whom, when, and how it ended,
    `None` while it runs."""

    key: Key
    head: str
    author: str
    started: datetime
    conclusion: str | None


# The job of the item workflow that runs the agent: its name, as GitHub lists it, is the caller's leg's name, a
# slash, and this.
READ_JOB = "read"


def leg_name(key: Key, head: str, author: str) -> str:
    """The name of an item's leg of the matrix, which every job of it carries in the run history."""
    return f"{key.kind} {key.number} at {head} by {author}"


_JOB = re.compile(r"(pr|issue) ([1-9][0-9]*) at ([0-9a-f]{40}|-) by ([A-Za-z0-9-]+(?:\[bot\])?) / " + READ_JOB)


def attempt_of(job_name: str, started: datetime, conclusion: str | None) -> Attempt | None:
    """The read a job of the run history is, or none where the job is another."""
    found = _JOB.fullmatch(job_name)
    if found is None:
        return None
    return Attempt(Key(found[1], int(found[2])), found[3], found[4], started, conclusion)
