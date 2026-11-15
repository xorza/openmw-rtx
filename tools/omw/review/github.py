"""GitHub as the review asks it, through `gh api`, which the runner has and which reads the job's token from
`GH_TOKEN`. Every listing is bounded: a page is GitHub's largest, and a listing stops at the pages its caller
names."""

import json
import os
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Any

from omw.system import Refusal, resolved

PAGE = 100


def repository() -> str:
    """The repository the run stands in, which Actions names in `GITHUB_REPOSITORY`."""
    named = os.environ.get("GITHUB_REPOSITORY", "")
    if "/" not in named:
        raise Refusal("GITHUB_REPOSITORY names no repository: `owner/name`, as Actions sets it")
    return named


def when(text: str) -> datetime:
    """A time GitHub writes, `2026-11-15T10:00:00Z`."""
    return datetime.fromisoformat(text.replace("Z", "+00:00"))


class GitHub:
    """One repository's REST and GraphQL APIs."""

    def __init__(self, name: str):
        self.name = name

    def get(self, path: str, **params: str | int) -> Any:
        fields = [part for key, value in params.items() for part in ("-f", f"{key}={value}")]
        return json.loads(_gh(path, ["--method", "GET", f"repos/{self.name}/{path}", *fields]))

    def pages(self, path: str, pages: int, key: str | None = None, **params: str | int) -> list[Any]:
        """Up to `pages` pages of a listing, oldest page first; `key` names the list inside an object a listing
        such as a workflow's runs wraps it in."""
        assert pages > 0, pages
        found: list[Any] = []
        for page in range(1, pages + 1):
            answer = self.get(path, per_page=PAGE, page=page, **params)
            listed = answer[key] if key is not None else answer
            found += listed
            if len(listed) < PAGE:
                break
        return found

    def post(self, path: str, payload: dict[str, Any]) -> Any:
        return json.loads(_gh(path, ["--method", "POST", f"repos/{self.name}/{path}", "--input", "-"],
                              json.dumps(payload)))

    def graphql(self, query: str, **variables: str | int) -> Any:
        """The answer's data. `gh` exits non-zero for an answer that carries errors, and says them."""
        fields = [part for key, value in variables.items()
                  for part in (("-F" if isinstance(value, int) else "-f"), f"{key}={value}")]
        return json.loads(_gh("graphql", ["graphql", "-f", f"query={query}", *fields]))["data"]


def _gh(what: str, args: list[str], stdin: str | None = None) -> str:
    """`gh api` with `args`, its output, or a refusal naming `what` it asked and what GitHub said: `gh` prints that
    on its error stream, which a failed call would otherwise drop for a bare exit status."""
    done = subprocess.run(resolved(["gh", "api", *args], None), input=stdin, capture_output=True, encoding="utf-8",
                          errors="replace", check=False)
    if done.returncode != 0:
        raise Refusal(f"GitHub refused {what}: {done.stderr.strip() or done.stdout.strip()}")
    return done.stdout


def write_output(name: str, value: str) -> None:
    """A step's output, where Actions reads them, and printed where it does not run."""
    assert "\n" not in value, name
    path = os.environ.get("GITHUB_OUTPUT")
    if path is None:
        print(f"{name}={value}")
        return
    with Path(path).open("a", encoding="utf-8") as out:
        out.write(f"{name}={value}\n")


def write_summary(text: str) -> None:
    """The run's summary page, where Actions shows it, and printed where it does not run."""
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path is None:
        print(text)
        return
    with Path(path).open("a", encoding="utf-8") as out:
        out.write(text + "\n")
