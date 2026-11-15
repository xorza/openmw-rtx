import contextlib
import io
import json
import unittest
import urllib.error
from datetime import UTC, datetime, timedelta
from email.message import Message
from unittest import mock

from omw import caches
from omw.caches import Cache, Repository, family, superseded
from omw.system import Refusal

T0 = datetime(2026, 11, 15, 12, 0, tzinfo=UTC)
ENVIRONMENT = {"GH_TOKEN": "t", "GITHUB_REPOSITORY": "o/r", "GITHUB_API_URL": "https://api.example"}


def cache(number: int, key: str, minutes: int) -> Cache:
    return Cache(number, key, T0 + timedelta(minutes=minutes))


class FamilyTest(unittest.TestCase):
    def test_a_key_less_its_trailing_versions(self):
        cases = {
            # The ccache action's time.
            "ccache-linux-clang-2026-11-15T20:24:46.788Z": "ccache-linux-clang",
            # A pin's digest, and a commit's.
            "tools-linux-3646ea0e384418b9": "tools-linux",
            "tidy-clean-windows-msvc-86baf3482ea19ce7a0949061f7ea5cb4f1ced76c": "tidy-clean-windows-msvc",
            # A set's tag and a run's number, one after the other, so every set of a system is one family.
            "vcpkg-windows-deps-90fa646facc31ba9-43760548334": "vcpkg-windows-deps",
            # A name's own words stay, a short hexadecimal one among them.
            "fetchcontent-linux-asan-ee2db2e227611273": "fetchcontent-linux-asan",
            "ccache-cafe": "ccache-cafe",
        }
        for key, wanted in cases.items():
            with self.subTest(key=key):
                self.assertEqual(family(key), wanted)


class SupersededTest(unittest.TestCase):
    def test_each_family_keeps_its_newest_and_only_the_prefixes_named(self):
        listed = [
            cache(1, "ccache-linux-clang-2026-11-15T19:44:17.493Z", 0),
            cache(2, "ccache-linux-clang-2026-11-15T20:20:37.548Z", 30),
            cache(3, "ccache-linux-asan-2026-11-15T20:20:45.496Z", 31),
            cache(4, "tools-linux-1111111111111111", 5),
            cache(5, "tools-linux-2222222222222222", 40),
            cache(6, "vcpkg-linux-deps-90fa646facc31ba9-1", 10),
            cache(7, "vcpkg-linux-deps-aaaaaaaaaaaaaaaa-2", 50),
        ]
        ids = lambda found: sorted(each.id for each in found)  # noqa: E731
        # The newest of each family stays: clang's at 30, asan's only one, the tools' at 40; the vcpkg family is
        # named by no prefix.
        self.assertEqual(ids(superseded(listed, ["ccache-", "tools-"], 1)), [1, 4])
        # None kept: every one named goes, and still no vcpkg's.
        self.assertEqual(ids(superseded(listed, ["ccache-linux-clang-"], 0)), [1, 2])
        # Two sets of one system are one family, whichever tag each was built for.
        self.assertEqual(ids(superseded(listed, ["vcpkg-"], 1)), [6])
        self.assertEqual(ids(superseded(listed, ["vcpkg-"], 0)), [6, 7])
        # More kept than a family holds deletes nothing of it.
        self.assertEqual(ids(superseded(listed, ["ccache-", "tools-", "vcpkg-"], 2)), [])
        self.assertEqual(superseded([], ["ccache-"], 0), [])


class VerbTest(unittest.TestCase):
    def run_verb(self, args: list[str], pages: list[list[dict]]) -> list[tuple[str, str]]:
        asked: list[tuple[str, str]] = []
        answers = iter(pages)

        def ask(_repository: Repository, method: str, path: str) -> bytes:
            asked.append((method, path))
            return json.dumps({"actions_caches": next(answers)}).encode() if method == "GET" else b""

        with mock.patch.dict(caches.os.environ, ENVIRONMENT), \
                mock.patch.object(Repository, "_ask", ask), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(caches.caches(args), 0)
        return asked

    def test_lists_each_prefix_on_the_ref_and_deletes_what_its_family_superseded(self):
        old = {"id": 1, "key": "ccache-linux-clang-2026-11-15T19:44:17.493Z", "created_at": "2026-11-15T19:44:18Z"}
        new = {"id": 2, "key": "ccache-linux-clang-2026-11-15T20:20:37.548Z", "created_at": "2026-11-15T20:20:38Z"}
        asked = self.run_verb(["--ref=refs/heads/master", "--keep=1", "ccache-"], [[new, old]])
        self.assertEqual(asked, [
            ("GET", "actions/caches?ref=refs%2Fheads%2Fmaster&key=ccache-&per_page=100&page=1"),
            ("DELETE", "actions/caches/1"),
        ])
        # None kept: both go, the listing read across a full page into the next.
        full = [{"id": n, "key": f"tidy-clean-linux-clang-{n:040x}", "created_at": "2026-11-15T20:00:00Z"}
                for n in range(3, 103)]
        asked = self.run_verb(["--ref=refs/heads/b", "--keep=0", "tidy-clean-"], [full, []])
        self.assertEqual([method for method, _ in asked], ["GET", "GET", *["DELETE"] * 100])
        self.assertTrue(asked[1][1].endswith("&page=2"))

    def test_refuses_what_it_cannot_answer(self):
        with mock.patch.dict(caches.os.environ, {"GH_TOKEN": "", "GITHUB_REPOSITORY": "o/r"}), \
                self.assertRaisesRegex(Refusal, "GH_TOKEN"):
            caches.caches(["--ref=r", "--keep=1", "ccache-"])
        with self.assertRaisesRegex(Refusal, "none or more"):
            caches.caches(["--ref=r", "--keep=-1", "ccache-"])
        with self.assertRaisesRegex(Refusal, "caches:"):
            caches.caches(["--keep=1", "ccache-"])
        # A listing that fills every page it reads is not read short.
        page = [{"id": n, "key": f"ccache-x-{n}", "created_at": "2026-11-15T20:00:00Z"} for n in range(caches.PAGE)]
        with self.assertRaisesRegex(Refusal, "1000 caches or more under ccache-"):
            self.run_verb(["--ref=r", "--keep=1", "ccache-"], [page] * caches.PAGES)


class AskTest(unittest.TestCase):
    def ask(self, method: str, raised: Exception) -> bytes:
        repository = Repository("https://api.example", "o/r", "t")
        with mock.patch.object(caches.urllib.request, "urlopen", side_effect=raised):
            return repository._ask(method, "actions/caches/7")

    def test_a_cache_already_gone_is_deleted_and_any_other_failure_says_what_was_asked(self):
        def http(code: int) -> urllib.error.HTTPError:
            return urllib.error.HTTPError("u", code, "Not Found" if code == 404 else "Forbidden", Message(), None)

        self.assertEqual(self.ask("DELETE", http(404)), b"")
        with self.assertRaisesRegex(Refusal, "^GitHub refused GET actions/caches/7: 404 Not Found$"):
            self.ask("GET", http(404))
        with self.assertRaisesRegex(Refusal, "^GitHub refused DELETE actions/caches/7: 403 Forbidden$"):
            self.ask("DELETE", http(403))
        with self.assertRaisesRegex(Refusal, "^GitHub did not answer GET actions/caches/7: timed out$"):
            self.ask("GET", urllib.error.URLError("timed out"))
