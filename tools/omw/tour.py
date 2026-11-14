"""`omw [flavour] tour --each=<label>:<args> [--each=...] [--out=<dir>] [tour args]`: a tour of each content, one
after another and never two at once, since they share the card, and one table at the end with the worst status.
Each `--each` is a label and the line that loads its content (`--replace=config --config=<profile>`, or the player's
own with `--content=Tribunal.esm --content=Bloodmoon.esm`); the rest of the line goes to every tour, and `--out` is
the driver's: each tour writes into a folder of its label under it, its log beside it."""

import contextlib
import re
import shlex
import sys
from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.exitstatus import ExitStatus
from omw.system import Refusal, Switches, read_text, temporary_folder

# What a tour names the findings it writes after every leg (`SessionRequest::Watching::mFindings`).
FINDINGS = "findings.txt"

# The worst first: a run that could not read its line, one that failed, one that found a difference, one that passed.
SEVERITY = (ExitStatus.REFUSED, ExitStatus.FAILED, ExitStatus.DIFFERED, ExitStatus.PASSED)

# The codes a harness ends with; any other, a signal's among them, is a run that broke.
KNOWN = frozenset(status.value for status in ExitStatus)


@dataclass(frozen=True)
class Content:
    label: str
    args: tuple[str, ...]


@dataclass(frozen=True)
class Plan:
    contents: tuple[Content, ...]
    out: Path | None
    rest: tuple[str, ...]


@dataclass(frozen=True)
class Outcome:
    label: str
    status: int
    faults: int | None
    costs: int | None


def wants_each(args: list[str]) -> bool:
    return any(arg.startswith("--each=") for arg in args)


def plan(args: list[str]) -> Plan:
    switches = Switches("tour", "a tour of each content, one after another; the rest of the line goes to every tour")
    switches.add_argument("--each", action="append", required=True,
                          help="<label>:<args>, the line that loads one content")
    switches.add_argument("--out", type=Path, help="where the tours go, a directory of their own if not given")
    asked, rest = switches.parse_known_args(args)
    contents: list[Content] = []
    for each in asked.each:
        label, colon, line = each.partition(":")
        if not colon or not re.fullmatch(r"[\w.-]+", label):
            raise Refusal(f"--each={each} names no label: `--each=vanilla:--content=Tribunal.esm`")
        if any(content.label == label for content in contents):
            raise Refusal(f"--each names {label} twice")
        contents.append(Content(label, tuple(shlex.split(line))))
    # Absolute, as each tour's `--out` names it and the harness runs from the build directory.
    return Plan(tuple(contents), asked.out.resolve() if asked.out else None, tuple(rest))


def read_counts(findings: str) -> tuple[int, int] | None:
    """The faults and the costs a tour's findings file counts, or nothing where it holds no count."""
    found = re.search(r"tour findings: (\d+) faults, (\d+) costs and marks", findings)
    return (int(found[1]), int(found[2])) if found else None


def worst(statuses: list[int]) -> int:
    if any(status not in KNOWN for status in statuses):
        return ExitStatus.FAILED
    return min(statuses, key=lambda status: SEVERITY.index(ExitStatus(status)), default=ExitStatus.PASSED)


def table(outcomes: list[Outcome]) -> str:
    lines = [f"  {'content':<16} {'status':<10} {'faults':>7} {'costs':>7}"]
    for outcome in outcomes:
        name = ExitStatus(outcome.status).name.lower() if outcome.status in KNOWN else str(outcome.status)
        faults = "-" if outcome.faults is None else str(outcome.faults)
        costs = "-" if outcome.costs is None else str(outcome.costs)
        lines.append(f"  {outcome.label:<16} {name:<10} {faults:>7} {costs:>7}")
    return "\n".join(lines)


def run(build: Build, args: list[str]) -> int:
    asked = plan(args)
    build.build_harness()
    stated = contextlib.nullcontext(asked.out) if asked.out else temporary_folder("omw-tour-")
    with stated as out:
        out.mkdir(parents=True, exist_ok=True)
        outcomes = []
        for content in asked.contents:
            folder = out / content.label
            log = out / f"{content.label}.log"
            print(f"tour: {content.label}", flush=True)
            with log.open("w", encoding="utf-8") as written:
                ended = build.harness("tour", *content.args, *asked.rest, f"--out={folder}", stdout=written,
                                      stderr=written)
            findings = folder / FINDINGS
            counts = read_counts(read_text(findings)) if findings.is_file() else None
            outcomes.append(Outcome(content.label, ended.returncode, *(counts or (None, None))))
            if ended.returncode != ExitStatus.PASSED:
                print(f"  ended {ended.returncode}, see {log}", file=sys.stderr, flush=True)

        print(table(outcomes))
        print(f"\nthe tours are in {out}")
        return worst([outcome.status for outcome in outcomes])
