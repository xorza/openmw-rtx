"""`omw review survey`: which open items are due a read, and which wait and why. The choice is `select`, a pure
function of what GitHub holds; the budget is counted from the workflow's own run history, so a failed read counts
as much as a posted one and nothing is stored between runs."""

import json
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Any

from omw.review.github import PAGE, GitHub, when, write_output, write_summary
from omw.review.item import (
    BASE,
    NO_HEAD,
    OPT_OUT,
    Attempt,
    Item,
    Key,
    Marker,
    attempt_of,
    leg_name,
    markers,
)
from omw.system import Refusal


@dataclass(frozen=True)
class Limits:
    """Every bound on the agent's runs, against a flood and against a loop."""

    # An author's reads in any `window`, the sliding window of GitHub's agentic workflows' `user-rate-limit`.
    author: int = 3
    # The repository's reads in any `window`.
    repository: int = 20
    # Items one run reads.
    run: int = 8
    # Reads of one pull request; one more when it turns ready.
    reviews: int = 5
    # Reads of one head in any `window` that posted nothing.
    tries: int = 2
    # How long a later head stands before it is read, so a day of pushes is one read.
    debounce: timedelta = timedelta(hours=5)
    window: timedelta = timedelta(hours=24)


@dataclass(frozen=True)
class Chosen:
    item: Item

    def leg(self) -> dict[str, Any]:
        """The matrix leg of the item workflow that reads it."""
        key = self.item.key
        # `slug` names the leg's artifacts, whose names take no colon.
        return {"name": leg_name(key, self.item.head, self.item.author), "item": str(key),
                "slug": f"{key.kind}-{key.number}"}


@dataclass(frozen=True)
class Passed:
    """An item not read this run, and why: `waits` where a later run reads it."""

    key: Key
    why: str
    waits: bool


@dataclass(frozen=True)
class Survey:
    chosen: list[Chosen]
    passed: list[Passed]


@dataclass(frozen=True)
class _Due:
    """An item due a read, and the rank it is read in when the budget cannot take every one: a pull request turned
    ready first, then an item never read, then a later head."""

    item: Item
    rank: int


def _due(item: Item, answers: list[Marker], attempts: list[Attempt], arrived: datetime | None, now: datetime,
         again: bool, limits: Limits) -> _Due | Passed:
    key = item.key
    if any(attempt.conclusion is None for attempt in attempts if attempt.key == key):
        return Passed(key, "being read", waits=True)
    if again:
        return _Due(item, 0)
    if item.bot:
        return Passed(key, "a bot's", waits=False)
    if item.association == "OWNER":
        return Passed(key, "the owner's", waits=False)
    if OPT_OUT in item.labels:
        return Passed(key, f"labelled {OPT_OUT}", waits=False)
    if key.kind == "pr" and item.base != BASE:
        return Passed(key, f"based on {item.base}, not {BASE}", waits=False)
    tried = [attempt for attempt in attempts if attempt.key == key and attempt.head == item.head]
    if len(tried) >= limits.tries and not any(marker.head == item.head for marker in answers):
        return Passed(key, f"read {len(tried)} times at this head in a day, and nothing posted", waits=True)
    if not answers:
        return _Due(item, 1)
    if key.kind == "issue":
        return Passed(key, "answered", waits=False)
    turned_ready = not item.draft and all(marker.draft for marker in answers)
    if turned_ready:
        return _Due(item, 0)
    if any(marker.head == item.head for marker in answers):
        return Passed(key, "answered at its head", waits=False)
    if len(answers) >= limits.reviews:
        return Passed(key, f"reviewed {len(answers)} times", waits=False)
    assert arrived is not None, key
    if now - arrived < limits.debounce:
        return Passed(key, f"its head stands {limits.debounce} from {arrived:%Y-%m-%d %H:%M} UTC", waits=True)
    return _Due(item, 2)


def select(items: list[Item], answers: dict[Key, list[Marker]], attempts: list[Attempt],
           arrivals: dict[Key, datetime], now: datetime, again: bool, limits: Limits) -> Survey:
    """**What a run reads**: each item due by its own rules (`_due`), then as many of them as the budget takes,
    in rank and then age. `attempts` are the reads of the last `limits.window`; `arrivals` when each pull request's
    head arrived, for those read before at another head. `again` reads the items whoever wrote them and whatever
    answered them, as the maintainer asks it of one item by hand, and only waits out a read already running."""
    assert all(now - attempt.started <= limits.window for attempt in attempts)
    due: list[_Due] = []
    passed: list[Passed] = []
    for item in items:
        verdict = _due(item, answers.get(item.key, []), attempts, arrivals.get(item.key), now, again, limits)
        if isinstance(verdict, _Due):
            due.append(verdict)
        else:
            passed.append(verdict)
    due.sort(key=lambda found: (found.rank, found.item.created))

    by_author = Counter(attempt.author for attempt in attempts)
    total = len(attempts)
    chosen: list[Chosen] = []
    for found in due:
        item = found.item
        if again:
            chosen.append(Chosen(item))
        elif len(chosen) >= limits.run:
            passed.append(Passed(item.key, f"past the {limits.run} items of a run", waits=True))
        elif total >= limits.repository:
            passed.append(Passed(item.key, f"past the repository's {limits.repository} reads a day", waits=True))
        elif by_author[item.author] >= limits.author:
            passed.append(Passed(item.key, f"past {item.author}'s {limits.author} reads a day", waits=True))
        else:
            chosen.append(Chosen(item))
            total += 1
            by_author[item.author] += 1
    return Survey(chosen, passed)


def _item(raw: dict[str, Any], kind: str) -> Item:
    pull = kind == "pr"
    return Item(
        key=Key(kind, raw["number"]),
        author=raw["user"]["login"],
        association=raw["author_association"],
        bot=raw["user"]["type"] == "Bot",
        labels=frozenset(label["name"] for label in raw["labels"]),
        draft=bool(raw.get("draft")) if pull else False,
        head=raw["head"]["sha"] if pull else NO_HEAD,
        base=raw["base"]["ref"] if pull else "",
        created=when(raw["created_at"]),
        updated=when(raw["updated_at"]),
    )


def _items(github: GitHub, key: Key | None) -> list[Item]:
    """The open items, or the one named, whichever its state."""
    if key is not None:
        raw = github.get(f"pulls/{key.number}" if key.kind == "pr" else f"issues/{key.number}")
        # GitHub's issues are its pull requests too, and one named an issue would be read as one.
        if key.kind == "issue" and "pull_request" in raw:
            raise Refusal(f"#{key.number} is a pull request: name it pr:{key.number}")
        return [_item(raw, key.kind)]
    pulls = [_item(raw, "pr") for raw in github.pages("pulls", 5, state="open")]
    issues = [_item(raw, "issue") for raw in github.pages("issues", 5, state="open") if "pull_request" not in raw]
    return pulls + issues


def answers_of(github: GitHub, key: Key) -> list[Marker]:
    """The markers of every answer the workflow posted on `key`: a review's body on a pull request, a comment's on
    an issue."""
    path = f"pulls/{key.number}/reviews" if key.kind == "pr" else f"issues/{key.number}/comments"
    return markers(((post["user"]["login"], post["body"]) for post in github.pages(path, 5)), key)


# The events a run surveys and reads under: `notice`'s runs, under an item's own event, hold no read.
_READING_EVENTS = frozenset({"schedule", "workflow_dispatch"})
# The window's runs as one listing holds them. A listing that fills them may have left the oldest out, and a read
# left out would be a read the budget does not count.
_RUN_PAGES = 5
# The most runs one GraphQL `nodes` takes.
_RUNS_ASKED = 100
# A run's jobs as GitHub lists them: a job is its run's check run, under the same name. One page holds a run's
# survey, its notice and three for each of its items, which `Limits.run` bounds far under it.
_JOBS = """query($ids: [ID!]!) {
  nodes(ids: $ids) {
    ... on WorkflowRun {
      checkSuite { checkRuns(first: 100) { pageInfo { hasNextPage } nodes { name conclusion startedAt } } }
    }
  }
}"""


def _attempts(github: GitHub, workflow: str, since: datetime) -> list[Attempt]:
    """Every read of the window, from the jobs of the workflow's runs: a skipped job read nothing, and one still
    queued, behind another read of its item, counts as running from when its run began. The jobs are asked of
    GraphQL a hundred runs a call: REST lists them a call a run, and every item of a burst dispatches a run that
    surveys, so a burst of N items would ask about N² job lists."""
    stamp = since.strftime("%Y-%m-%dT%H:%M:%SZ")
    listed = github.pages(f"actions/workflows/{workflow}/runs", _RUN_PAGES, key="workflow_runs", created=f">={stamp}")
    if len(listed) >= _RUN_PAGES * PAGE:
        raise Refusal(f"{workflow} ran {len(listed)} times or more since {stamp}, past what the survey lists: "
                      f"reads it left out would go uncounted")
    # A run created while the listing is paged moves the rest down a place, and the next page lists one again.
    runs = list({run["node_id"]: run for run in listed if run["event"] in _READING_EVENTS}.values())
    attempts = []
    for first in range(0, len(runs), _RUNS_ASKED):
        batch = runs[first:first + _RUNS_ASKED]
        nodes = github.graphql(_JOBS, ids=[run["node_id"] for run in batch])["nodes"]
        # A run deleted since it was listed is no node, and `gh` refuses the whole answer for it, as REST refused its
        # jobs: the next survey lists the window without it.
        for run, node in zip(batch, nodes, strict=True):
            jobs = node["checkSuite"]["checkRuns"]
            if jobs["pageInfo"]["hasNextPage"]:
                raise Refusal(f"run {run['id']} of {workflow} holds more jobs than a page, and the survey reads one")
            for job in jobs["nodes"]:
                if job["conclusion"] == "SKIPPED":
                    continue
                started = when(job["startedAt"] or run["created_at"])
                attempt = attempt_of(job["name"], started, job["conclusion"])
                if attempt is not None and attempt.started >= since:
                    attempts.append(attempt)
    return attempts


def arrival(github: GitHub, item: Item, ci: str) -> datetime:
    """When the pull request's head arrived: CI's first `pull_request` run at it, which GitHub creates on every
    push, a fork's waiting for approval included. A commit's own date is its author's to write, and GitHub's API
    no longer says when a commit was pushed. Where CI holds no run at the head, the pull request's last update,
    which came no sooner."""
    runs = github.pages(f"actions/workflows/{ci}/runs", 1, key="workflow_runs", event="pull_request",
                        head_sha=item.head)
    return min((when(run["created_at"]) for run in runs), default=item.updated)


def survey(github: GitHub, key: Key | None, again: bool, workflow: str, ci: str, now: datetime,
           limits: Limits) -> Survey:
    if again and key is None:
        raise Refusal("--again reads one item again, and names it with --item")
    items = _items(github, key)
    answers = {item.key: answers_of(github, item.key) for item in items}
    arrivals = {item.key: arrival(github, item, ci) for item in items
                if item.key.kind == "pr" and answers[item.key]
                and not any(marker.head == item.head for marker in answers[item.key])}
    attempts = _attempts(github, workflow, now - limits.window)
    return select(items, answers, attempts, arrivals, now, again, limits)


def report(found: Survey) -> None:
    """The matrix for the item workflow, and the run's summary of what waits."""
    write_output("items", json.dumps([chosen.leg() for chosen in found.chosen], separators=(",", ":")))
    lines = ["## Survey", ""]
    lines += [f"- reads {chosen.item.key}, by {chosen.item.author}" for chosen in found.chosen] or ["- reads nothing"]
    lines += [f"- {'waits' if passed.waits else 'passes'} {passed.key}: {passed.why}" for passed in found.passed]
    write_summary("\n".join(lines))
