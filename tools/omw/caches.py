"""`omw caches --ref=<ref> --keep=<n> <prefix>...`: the repository's GitHub Actions caches on a ref whose keys begin
with a prefix named, each family's newest `n` kept and the rest deleted, as CI holds its caches under the repository's
10 GB. A cache's family is its key less what tells one save from the next: a time, a digest, a run's number. Through
GitHub's REST API with the job's token in `GH_TOKEN`, not `gh`, which CI's AlmaLinux container lacks."""

import json
import os
import re
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime

from omw.system import Refusal, Switches

# One page is GitHub's largest; a ref holds a few dozen caches under the repository's limit, so ten pages are past
# any, and a listing that fills them is refused rather than read short.
PAGE = 100
PAGES = 10

# What one save of a family adds to its key, at its end and one after another: an ISO time (the ccache action's), a
# digest of 16 hexadecimal digits or more (a pin's, a set's, a commit's), a run's number.
_VERSION = re.compile(r"(?:-(?:\d{4}-\d\d-\d\dT[\d:.]+Z|[0-9a-f]{16,}|\d+))+$")


@dataclass(frozen=True)
class Cache:
    id: int
    key: str
    created: datetime


def family(key: str) -> str:
    """The family `key` is a save of: the key less its trailing versions."""
    return _VERSION.sub("", key)


def superseded(caches: list[Cache], prefixes: list[str], keep: int) -> list[Cache]:
    """The caches of `caches` whose key begins with one of `prefixes` and that `keep` newer ones of their family
    stand before."""
    assert keep >= 0, keep
    families: dict[str, list[Cache]] = {}
    for cache in caches:
        if cache.key.startswith(tuple(prefixes)):
            families.setdefault(family(cache.key), []).append(cache)
    return [cache for kept in families.values()
            for cache in sorted(kept, key=lambda cache: cache.created, reverse=True)[keep:]]


@dataclass(frozen=True)
class Repository:
    """The repository's caches, through its REST API."""

    api: str
    name: str
    token: str

    @staticmethod
    def from_environment() -> "Repository":
        token = os.environ.get("GH_TOKEN", "")
        name = os.environ.get("GITHUB_REPOSITORY", "")
        if not token or "/" not in name:
            raise Refusal("caches asks GitHub with `GH_TOKEN` for the repository `GITHUB_REPOSITORY` names, as "
                          "Actions sets it")
        return Repository(os.environ.get("GITHUB_API_URL", "https://api.github.com"), name, token)

    def _ask(self, method: str, path: str) -> bytes:
        """GitHub's answer, or a refusal naming what was asked and what GitHub said. A cache another run deleted
        since it was listed is gone, as asked."""
        request = urllib.request.Request(f"{self.api}/repos/{self.name}/{path}", method=method, headers={
            "Accept": "application/vnd.github+json", "Authorization": f"Bearer {self.token}",
            "X-GitHub-Api-Version": "2022-11-28"})
        try:
            with urllib.request.urlopen(request, timeout=60) as answer:
                return answer.read()
        except urllib.error.HTTPError as error:
            if method == "DELETE" and error.code == 404:
                return b""
            raise Refusal(f"GitHub refused {method} {path}: {error.code} {error.reason}") from error
        except urllib.error.URLError as error:
            raise Refusal(f"GitHub did not answer {method} {path}: {error.reason}") from error

    def caches(self, ref: str, prefix: str) -> list[Cache]:
        """Every cache on `ref` whose key begins with `prefix`."""
        found: list[Cache] = []
        for page in range(1, PAGES + 1):
            query = urllib.parse.urlencode({"ref": ref, "key": prefix, "per_page": PAGE, "page": page})
            listed = json.loads(self._ask("GET", f"actions/caches?{query}"))["actions_caches"]
            found += [Cache(entry["id"], entry["key"], datetime.fromisoformat(entry["created_at"]))
                      for entry in listed]
            if len(listed) < PAGE:
                return found
        raise Refusal(f"{ref} holds {PAGES * PAGE} caches or more under {prefix}: past what caches lists")

    def delete(self, cache: Cache) -> None:
        self._ask("DELETE", f"actions/caches/{cache.id}")


def caches(args: list[str]) -> int:
    switches = Switches("caches", __doc__ or "")
    switches.add_argument("--ref", required=True)
    switches.add_argument("--keep", type=int, required=True)
    switches.add_argument("prefixes", nargs="+")
    parsed = switches.parse_args(args)
    if parsed.keep < 0:
        raise Refusal("caches keeps none or more of each family")
    repository = Repository.from_environment()
    listed = {cache.id: cache for prefix in parsed.prefixes for cache in repository.caches(parsed.ref, prefix)}
    for cache in superseded(list(listed.values()), parsed.prefixes, parsed.keep):
        print(f"deleting {cache.key}, made {cache.created:%Y-%m-%d %H:%M} UTC")
        repository.delete(cache)
    return 0
