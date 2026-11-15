"""`omw review <step>`: the automatic review's steps, as `auto-review.yml` runs them."""

import json
import os
import tempfile
from datetime import UTC, datetime
from pathlib import Path

from omw.review import answer, gather, post, survey
from omw.review.github import GitHub, repository
from omw.review.item import Key
from omw.system import Refusal, Switches

# The workflow whose run history counts the reads, and CI's, whose runs say when a head arrived.
WORKFLOW = "auto-review.yml"
CI_WORKFLOW = "ci.yml"
STEPS = ("survey", "gather", "check", "post", "schema")


def review(args: list[str]) -> int:
    if not args or args[0] not in STEPS:
        raise Refusal(f"review takes a step: {', '.join(STEPS)}")
    step, rest = args[0], args[1:]
    switches = Switches(f"review {step}", "")
    if step == "schema":
        switches.add_argument("kind", choices=sorted(answer.SCHEMAS))
        parsed = switches.parse_args(rest)
        print(json.dumps(answer.wire(answer.SCHEMAS[parsed.kind]), separators=(",", ":")))
        return 0
    switches.add_argument("--item", type=Key.parse, required=step != "survey")
    if step != "gather":
        switches.add_argument("--again", action="store_true")
    if step == "gather":
        switches.add_argument("--out", type=Path, required=True)
    if step == "check":
        switches.add_argument("--head", required=True)
    if step == "post":
        switches.add_argument("--data", type=Path, required=True)
        switches.add_argument("--answer", type=Path, required=True)
        switches.add_argument("--dry-run", action="store_true")
    parsed = switches.parse_args(rest)
    github = GitHub(repository())

    if step == "survey":
        survey.report(survey.survey(github, parsed.item, parsed.again, WORKFLOW, CI_WORKFLOW,
                                    datetime.now(UTC), survey.Limits()))
    elif step == "gather":
        with tempfile.TemporaryDirectory(prefix="omw-review-") as scratch:
            gather.gather(github, parsed.item, parsed.out, Path(scratch))
    elif step == "check":
        post.check(github, parsed.item, parsed.head, parsed.again)
    else:
        given = answer.parse(parsed.answer.read_text(encoding="utf-8"), parsed.item)
        post.post(github, parsed.item, post.Gathered.read(parsed.data), given,
                  os.environ.get("AGENT_SECRET", ""), parsed.again, parsed.dry_run)
    return 0
