"""`omw review post` and `omw review check`: the agent's answer turned into the one comment it becomes, and posted;
and whether an answer already stands, which each of the two jobs that could repeat one asks first. Everything the
agent wrote is untrusted, so the guarantees live here: the disclosure is the template's, the text is made inert
(`text.neutral`), an inline finding stands only where GitHub takes one, and the agent's credential is searched for
before anything is public."""

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from omw.review import diff as diffs
from omw.review.answer import LABELS, Finding, IssueAnswer, PullAnswer
from omw.review.github import GitHub, write_output, write_summary
from omw.review.item import NO_HEAD, Key, Marker
from omw.review.survey import answers_of
from omw.review.text import cut, neutral
from omw.system import Refusal

# A comment's body, or a pull request's; GitHub refuses one past 65,536 characters, and the template takes what this
# leaves.
BODY = 60_000
INLINE = 4_000
# A path a link may be built of: no `..`, nothing but what a tree's file names use.
_PATH = re.compile(r"(?!.*(?:^|/)\.\.(?:/|$))[A-Za-z0-9_][A-Za-z0-9_./+-]{0,299}")

PULL_HEADER = ("**Automatic review.** This was written by the repository's automation at `{head}`. A maintainer "
               "reads every pull request and will reply here; nothing below approves, blocks or speaks for them.")
ISSUE_HEADER = ("**Automatic first look.** The repository's automation read this report and the code it points "
                "at. A maintainer will follow up; what is below is a starting point, not an answer.")
HELD = ("The automatic review stopped here: the {what} holds text addressed to the automation, which a maintainer "
        "will read.")
FOOTER = "<sub>React with 👍 or 👎 to say whether this helped; the maintainer reads them to tune the review.</sub>"


@dataclass(frozen=True)
class Comment:
    """One inline comment, where GitHub takes it."""

    path: str
    line: int
    body: str


@dataclass(frozen=True)
class Rendered:
    """What a read posts: the body, the inline comments a pull request's review carries, and the threads of the
    earlier review it resolves."""

    body: str
    inline: list[Comment]
    resolved: list[str]
    held: bool


def _count(count: int, one: str, many: str) -> str:
    return f"{count} {one if count == 1 else many}"


def _order(finding: Finding) -> tuple[bool, int]:
    return not finding.blocking, LABELS.index(finding.label)


def _finding(finding: Finding, repository: str) -> str:
    decoration = "blocking" if finding.blocking else "non-blocking"
    text = f"**{finding.label} ({decoration}):** {neutral(finding.title, repository, 300)}\n\n"
    text += neutral(finding.body, repository, INLINE)
    sources = [neutral(source, repository, 600) for source in finding.sources]
    if sources:
        text += "\n\nSources: " + ", ".join(sources)
    return text


def _place(path: str, line: int) -> str:
    if not _PATH.fullmatch(path):
        return "`(a path the review could not name)`"
    return f"`{path}:{line}`" if line else f"`{path}`"


def _held(answer: PullAnswer | IssueAnswer, what: str, repository: str) -> str:
    places = ", ".join(neutral(injection.where, repository, 200) for injection in answer.injections)
    return HELD.format(what=what) + f" ({places})"


def render_pull(answer: PullAnswer, marker: Marker, diff: str, open_threads: set[str], repository: str) -> Rendered:
    header = PULL_HEADER.format(head=marker.head[:12])
    end = f"\n\n---\n{FOOTER}\n{marker.render()}"
    if answer.injections:
        return Rendered(f"{header}\n\n{_held(answer, 'pull request', repository)}{end}", [], [], held=True)

    commentable = diffs.commentable(diffs.parse(diff))
    inline: list[Comment] = []
    elsewhere: list[Finding] = []
    for finding in sorted(answer.findings, key=_order):
        if finding.line in commentable.get(finding.path, frozenset()) and _PATH.fullmatch(finding.path):
            inline.append(Comment(finding.path, finding.line, _finding(finding, repository)))
        else:
            elsewhere.append(finding)
    verdict = (f"Approach: **{answer.approach}** · Correct: **{answer.correct}** · Tests: **{answer.covered}** · "
               f"General: **{answer.general}**")
    parts = [header, f"**Intent:** {neutral(answer.intent, repository, 600)}", verdict,
             neutral(answer.summary, repository, 5000)]
    if elsewhere:
        parts.append("### Findings off the diff\n\n" + "\n\n".join(
            f"- {_place(finding.path, finding.line) if finding.path else '(the change as a whole)'} "
            + _finding(finding, repository).replace("\n", "\n  ") for finding in elsewhere))
    if inline:
        parts.append(f"{_count(len(inline), 'finding stands', 'findings stand')} on the lines they are about.")
    # Each thread once, and only one of the earlier review's open ones: the agent names them, and is untrusted.
    resolved = [thread for thread in dict.fromkeys(answer.answered) if thread in open_threads]
    if resolved:
        parts.append(f"{_count(len(resolved), 'finding', 'findings')} of the earlier review answered, and "
                     f"{'its thread' if len(resolved) == 1 else 'their threads'} resolved.")
    if answer.coverage:
        parts.append(f"**Read:** {neutral(answer.coverage, repository, 600)}")
    return Rendered(cut("\n\n".join(parts), BODY) + end, inline, resolved, held=False)


def render_issue(answer: IssueAnswer, marker: Marker, release: str, repository: str) -> Rendered:
    end = f"\n\n---\n{FOOTER}\n{marker.render()}"
    if answer.injections:
        return Rendered(f"{ISSUE_HEADER}\n\n{_held(answer, 'issue', repository)}{end}", [], [], held=True)
    parts = [ISSUE_HEADER, f"**Kind:** {answer.kind}", neutral(answer.summary, repository, 5000)]
    if answer.places:
        lines = []
        for place in answer.places:
            ref = release if place.tree == "release" and release else "master"
            where = _place(place.path, place.line)
            if _PATH.fullmatch(place.path):
                anchor = f"#L{place.line}" if place.line else ""
                where = f"[{where}](https://github.com/{repository}/blob/{ref}/{place.path}{anchor})"
            lines.append(f"- {where} at {ref}: {neutral(place.why, repository, 600)}")
        parts.append("**Where it likely lives**\n\n" + "\n".join(lines))
    if answer.missing:
        parts.append("**What would help**\n\n" + "\n".join(f"- {neutral(need, repository, 400)}"
                                                         for need in answer.missing))
    if answer.duplicates:
        parts.append("**Possibly the same as** " + ", ".join(f"#{number}" for number in answer.duplicates))
    return Rendered(cut("\n\n".join(parts), BODY) + end, [], [], held=False)


def leaks(rendered: Rendered, secret: str) -> bool:
    """Whether the agent's credential stands anywhere in what would be posted."""
    return bool(secret) and any(secret in text for text in [rendered.body, *(c.body for c in rendered.inline)])


def answered(github: GitHub, key: Key, head: str) -> bool:
    """Whether an answer to `key` at `head` stands already: a pull request's at that head, an issue's at all."""
    return any(marker.head == head or head == NO_HEAD for marker in answers_of(github, key))


def publish(github: GitHub, key: Key, head: str, rendered: Rendered) -> None:
    if key.kind == "issue":
        github.post(f"issues/{key.number}/comments", {"body": rendered.body})
        return
    github.post(f"pulls/{key.number}/reviews", {
        "commit_id": head,
        "event": "COMMENT",
        "body": rendered.body,
        "comments": [{"path": comment.path, "line": comment.line, "side": "RIGHT", "body": comment.body}
                     for comment in rendered.inline],
    })
    for thread in rendered.resolved:
        github.graphql("mutation($id: ID!) { resolveReviewThread(input: {threadId: $id}) { thread { id } } }",
                       id=thread)


@dataclass(frozen=True)
class Gathered:
    """What `gather` wrote that `post` reads."""

    item: dict[str, Any]
    diff: str
    threads: set[str]
    release: str

    @staticmethod
    def read(folder: Path) -> "Gathered":
        item = json.loads((folder / "item.json").read_text(encoding="utf-8"))
        diff_file = folder / "diff.patch"
        previous_file = folder / "previous.json"
        release_file = folder / "release.txt"
        previous = json.loads(previous_file.read_text(encoding="utf-8")) if previous_file.is_file() else {}
        return Gathered(item, diff_file.read_text(encoding="utf-8") if diff_file.is_file() else "",
                        {thread["id"] for thread in previous.get("threads", [])},
                        release_file.read_text(encoding="utf-8").strip() if release_file.is_file() else "")


def post(github: GitHub, key: Key, gathered: Gathered, answer: PullAnswer | IssueAnswer,
         secret: str, again: bool, dry_run: bool) -> None:
    head = gathered.item.get("head", NO_HEAD)
    marker = Marker(key, head, bool(gathered.item.get("draft", False)))
    if isinstance(answer, PullAnswer):
        rendered = render_pull(answer, marker, gathered.diff, gathered.threads, github.name)
    else:
        rendered = render_issue(answer, marker, gathered.release, github.name)
    if leaks(rendered, secret):
        raise Refusal("the answer carries the agent's credential; nothing was posted. Revoke the token and read the "
                      "read job's log")
    if rendered.held:
        print(f"::warning::{key} holds text addressed to the automation; the answer is held for a maintainer")
    write_summary("\n\n".join([f"## {'Would post' if dry_run else 'Posts'} on {key}", rendered.body,
                               *(f"**Inline, {c.path}:{c.line}**\n\n{c.body}" for c in rendered.inline)]))
    if dry_run:
        return
    if not again and answered(github, key, head):
        print(f"an answer to {key} at {head} stands already; nothing posted")
        return
    publish(github, key, head, rendered)


def check(github: GitHub, key: Key, head: str, again: bool) -> None:
    """`answered=true` where an answer stands, which skips the agent; with `again`, never."""
    write_output("answered", "true" if not again and answered(github, key, head) else "false")
