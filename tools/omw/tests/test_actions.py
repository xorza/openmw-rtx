"""The shell of the composite actions the agents' workflows share, run as the runner runs it. It stays shell, and
not the driver's Python: an action is read from the workflow's own commit, where the driver is the working tree's,
which the agent the action cages may have edited."""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from omw.system import ROOT, on_path, read_text

ACTIONS = ROOT / ".github" / "actions"
# What the scripts start: a desk without one of them, Windows' mostly, runs none of this, and CI's checks job all.
NEEDED = ("bash", "git", "jq", "python3")


def script(action: str, step: str | None = None) -> str:
    """The `run` of an action's step, the first one or the one whose `id` is `step`, as the runner hands it to
    bash: the block under `run: |`, its indentation taken off."""
    lines = read_text(ACTIONS / action / "action.yml").splitlines()
    start = 0 if step is None else lines.index(f"      id: {step}")
    at = next(index for index in range(start, len(lines)) if lines[index].strip() == "run: |")
    indent = len(lines[at]) - len(lines[at].lstrip()) + 2
    block = []
    for line in lines[at + 1:]:
        if line.strip() and len(line) - len(line.lstrip()) < indent:
            break
        block.append(line[indent:])
    return "\n".join(block)


def ran(text: str, cwd: Path, **env: str) -> subprocess.CompletedProcess:
    """`text` as a composite action's `shell: bash` runs it."""
    return subprocess.run(["bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c", text], cwd=cwd,
                          env={**os.environ, **env}, capture_output=True, text=True, check=False)


@unittest.skipUnless(all(on_path(tool) for tool in NEEDED), f"the actions' shell needs {', '.join(NEEDED)}")
class ActionsTest(unittest.TestCase):
    def folder(self) -> Path:
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        return folder

    def caged(self, edits: str, web: str) -> dict[str, str]:
        folder = self.folder()
        done = ran(script("agent", "cage"), folder, EDITS=edits, WEB=web, GITHUB_OUTPUT=str(folder / "output"))
        self.assertEqual(done.returncode, 0, done.stderr)
        return dict(line.split("=", 1) for line in read_text(folder / "output").splitlines())

    def test_the_cage_denies_and_allows_by_whether_the_agent_edits_and_reads_the_web(self):
        always = ["Read(//proc/**)", "Read(~/.claude/**)", "Read(~/.claude.json)", "Read(./.git/**)", "Bash",
                  "NotebookEdit", "Skill"]
        kept_out = ["Edit(./.git/**)", "Edit(./.github/**)", "Edit(./.claude/**)", "Edit(./.mcp.json)"]
        cases = {
            ("true", "false"): ([*always, *kept_out, "WebFetch", "WebSearch"], "Read(./**),Grep,Glob,Edit(./**)"),
            ("false", "true"): ([*always, "Edit", "Write"], "Read(./**),Grep,Glob,WebSearch,WebFetch"),
            ("true", "true"): ([*always, *kept_out], "Read(./**),Grep,Glob,Edit(./**),WebSearch,WebFetch"),
            ("false", "false"): ([*always, "Edit", "Write", "WebFetch", "WebSearch"], "Read(./**),Grep,Glob"),
        }
        for (edits, web), (denied, tools) in cases.items():
            with self.subTest(edits=edits, web=web):
                cage = self.caged(edits, web)
                settings = json.loads(cage["settings"])
                self.assertEqual(settings["permissions"], {"deny": denied})
                self.assertEqual(cage["tools"], tools)
                # No tool the cage allows is one it denies by its bare name.
                self.assertFalse({tool.split("(")[0] for tool in tools.split(",")} & set(denied))
                self.assertIs(settings["disableAllHooks"], True)
                self.assertEqual(settings["sandbox"], {
                    "enabled": True, "failIfUnavailable": True, "allowUnsandboxedCommands": False,
                    "network": {"allowedDomains": []},
                    "credentials": {"envVars": [{"name": name, "mode": "deny"} for name in (
                        "GITHUB_TOKEN", "GH_TOKEN", "CLAUDE_CODE_OAUTH_TOKEN", "ACTIONS_RUNTIME_TOKEN",
                        "ACTIONS_ID_TOKEN_REQUEST_TOKEN")]},
                })

    def test_the_cage_refuses_a_switch_that_is_neither_true_nor_false(self):
        folder = self.folder()
        for edits, web in (("yes", "false"), ("true", ""), ("True", "false")):
            with self.subTest(edits=edits, web=web):
                done = ran(script("agent", "cage"), folder, EDITS=edits, WEB=web, GITHUB_OUTPUT=str(folder / "output"))
                self.assertEqual(done.returncode, 1)
                self.assertIn("true or false", done.stdout)
                self.assertFalse((folder / "output").exists(), "a refused cage wrote settings")

    def tree(self) -> Path:
        """A repository mid-merge as an agent leaves one: two files committed, one of them edited since, a file the
        merge adds staged, one the agent wrote, one with a conflict's markers, and a driver that says what it was
        asked to format."""
        tree = self.folder()

        def git(*args: str) -> None:
            subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.org", *args], cwd=tree,
                           check=True, capture_output=True)

        git("init", "--quiet", ".")
        (tree / "kept.txt").write_text("kept\n")
        (tree / "edited.cpp").write_text("int a;\n")
        (tree / "omw").write_text("import sys\nopen('formatted.log', 'w').write('\\n'.join(sys.argv[1:]))\n")
        git("add", "--all")
        git("commit", "--quiet", "--message", "base")
        (tree / ".git" / "info" / "exclude").write_text("/.upstream-merge/\n/formatted.log\n")
        (tree / "edited.cpp").write_text("int a = 1;\n")
        (tree / "added.cpp").write_text("int b;\n")
        git("add", "added.cpp")
        (tree / "written.cpp").write_text("int c;\n")
        (tree / "marked.cpp").write_text("<<<<<<< ours\nint d;\n=======\nint e;\n>>>>>>> theirs\n")
        lists = tree / ".upstream-merge"
        lists.mkdir()
        (lists / "added.txt").write_text("added.cpp\n")
        (lists / "conflicts.txt").write_text("edited.cpp\n")
        return tree

    def status(self, tree: Path) -> list[str]:
        return subprocess.run(["git", "status", "--porcelain"], cwd=tree, check=True, capture_output=True,
                              text=True).stdout.splitlines()

    def test_a_removal_goes_only_where_a_list_names_it_though_the_file_is_staged_or_edited(self):
        tree = self.tree()
        # One the merge adds, staged, and one the agent edited, each of which git removes only when forced; one no
        # list names; and one outside the tree.
        (tree / ".upstream-merge" / "removed.txt").write_text("added.cpp\n\nedited.cpp\nkept.txt\n../outside\n")
        done = ran(script("agent-edits"), tree, REMOVABLE="conflicts.txt added.txt")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.status(tree), ["D  edited.cpp", "A  marked.cpp", "A  written.cpp"])
        # What the agent changed and left, but the file whose markers a format would reflow.
        self.assertEqual(read_text(tree / "formatted.log").splitlines(), ["format", "written.cpp"])

    def test_a_list_that_does_not_name_a_removal_keeps_it_and_no_list_keeps_every_one(self):
        for removable, status in (("added.txt", ["M  edited.cpp", "A  marked.cpp", "A  written.cpp"]),
                                  ("", ["A  added.cpp", "M  edited.cpp", "A  marked.cpp", "A  written.cpp"])):
            with self.subTest(removable=removable):
                tree = self.tree()
                # A path named twice, which a search of the list of removals itself would find.
                (tree / ".upstream-merge" / "removed.txt").write_text("edited.cpp\nadded.cpp\nedited.cpp\n")
                done = ran(script("agent-edits"), tree, REMOVABLE=removable)
                self.assertEqual(done.returncode, 0, done.stderr)
                self.assertEqual(self.status(tree), status)

    def test_a_tree_the_agent_never_touched_formats_nothing_and_stages_what_stands(self):
        tree = self.tree()
        for name in ("written.cpp", "marked.cpp"):
            (tree / name).unlink()
        subprocess.run(["git", "checkout", "--quiet", "--", "edited.cpp"], cwd=tree, check=True)
        done = ran(script("agent-edits"), tree, REMOVABLE="added.txt")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.status(tree), ["A  added.cpp"])
        self.assertFalse((tree / "formatted.log").exists(), "the driver was started with nothing to format")

    def stub_curl(self, folder: Path, answers: list[tuple[str, str | None]]) -> dict[str, str]:
        """A `curl` in `folder` that keeps the line of each call in `calls` and answers each in turn with a status
        and a body, or with no body and a failure, as for a server never reached; the environment that finds it."""
        (folder / "bin").mkdir()
        curl = folder / "bin" / "curl"
        curl.write_text("""#!/usr/bin/env bash
echo "$*" >> "$STUB/calls"
at=$(wc -l < "$STUB/calls")
cat "$STUB/status$at"
if [ ! -f "$STUB/body$at" ]; then exit 7; fi
while [ $# -gt 0 ]; do
  if [ "$1" = -o ]; then cp "$STUB/body$at" "$2"; fi
  shift
done
""")
        curl.chmod(0o755)
        for at, (status, body) in enumerate(answers, 1):
            (folder / f"status{at}").write_text(status)
            if body is not None:
                (folder / f"body{at}").write_text(body)
        return {"STUB": str(folder), "PATH": f"{folder / 'bin'}{os.pathsep}{os.environ['PATH']}"}

    def exchange(self, answers: list[tuple[str, str | None]],
                 identity: bool = True) -> tuple[subprocess.CompletedProcess, Path]:
        """`app-token`'s script against a `curl` that answers each call in turn (`stub_curl`); the folder holds its
        `calls` and the step's `output`."""
        folder = self.folder()
        env = {**self.stub_curl(folder, answers), "RUNNER_TEMP": str(folder), "GITHUB_OUTPUT": str(folder / "output"),
               "ACTIONS_ID_TOKEN_REQUEST_URL": "", "ACTIONS_ID_TOKEN_REQUEST_TOKEN": ""}
        if identity:
            env |= {"ACTIONS_ID_TOKEN_REQUEST_URL": "https://id.example/token?v=1",
                    "ACTIONS_ID_TOKEN_REQUEST_TOKEN": "request-secret"}
        done = ran(script("app-token"), folder, **env)
        self.assertFalse((folder / "app-token-answer").exists(), "an answer was left on the disk")
        return done, folder

    def test_the_exchange_hands_the_run_s_identity_to_the_app_s_service_and_masks_both_tokens(self):
        # Under either name the action has read it by, and with any status of success.
        for field, status in (("token", "200"), ("app_token", "200"), ("token", "201")):
            with self.subTest(field=field, status=status):
                done, folder = self.exchange([("200", '{"value": "the-identity"}'),
                                              (status, f'{{"{field}": "the-app"}}')])
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                self.assertEqual(done.stdout.splitlines(), ["::add-mask::the-identity", "::add-mask::the-app"])
                self.assertEqual(read_text(folder / "output"), "token=the-app\n")
                asked, exchanged = read_text(folder / "calls").splitlines()
                self.assertIn("Authorization: bearer request-secret", asked)
                self.assertTrue(asked.endswith("https://id.example/token?v=1&audience=claude-code-github-action"))
                self.assertIn("-X POST -H Authorization: Bearer the-identity", exchanged)
                self.assertTrue(exchanged.endswith("https://api.anthropic.com/api/github/github-app-token-exchange"))

    def test_an_exchange_that_fails_says_which_half_refused_with_what_status_and_in_whose_words(self):
        identity = ("200", '{"value": "the-identity"}')
        told = "compare this step with src/github/token.ts as the action has it now"
        cases = [
            # No identity to ask with: nothing is asked.
            ([], False, 0, "::error::the run has no identity to exchange for the app's token: its job lacks "
                           "id-token: write"),
            ([("403", '{"message": "denied"}\n')], True, 1,
             '::error::GitHub gave the run no identity token to exchange (HTTP 403): {"message": "denied"} '),
            ([identity, ("401", "Invalid\r\naudience")], True, 2,
             "::error::the Claude GitHub App's exchange gave no token (HTTP 401): Invalid  audience. The exchange is "
             f"the pinned claude-code-action's own and no documented interface: {told}"),
            # A service never reached answers nothing: not the identity's answer, left from the call before.
            ([identity, ("000", None)], True, 2,
             "::error::the Claude GitHub App's exchange gave no token (HTTP 000): . The exchange is the pinned "
             f"claude-code-action's own and no documented interface: {told}"),
            # An answer that may hold the token under another name: its fields are named, and no value is printed.
            ([identity, ("200", '{"renamed": "a-secret", "expires": 1}')], True, 2,
             "::error::the Claude GitHub App's exchange gave no token (HTTP 200): an answer that holds none, of the "
             "fields expires, renamed. The exchange is the pinned claude-code-action's own and no documented "
             f"interface: {told}"),
            ([identity, ("200", "<html>")], True, 2,
             "::error::the Claude GitHub App's exchange gave no token (HTTP 200): an answer that holds none, of the "
             "fields of no JSON object. The exchange is the pinned claude-code-action's own and no documented "
             f"interface: {told}"),
        ]
        for answers, has_identity, calls, said in cases:
            with self.subTest(said=said[:60]):
                done, folder = self.exchange(answers, has_identity)
                self.assertEqual(done.returncode, 1)
                self.assertEqual(done.stdout.splitlines()[-1], said)
                self.assertNotIn("a-secret", done.stdout + done.stderr)
                self.assertNotIn("the-identity", done.stdout.splitlines()[-1])
                self.assertFalse((folder / "output").exists(), "a failed exchange gave a token")
                held = folder / "calls"
                self.assertEqual(len(read_text(held).splitlines()) if held.exists() else 0, calls)

    def test_a_token_is_given_back_to_github_and_a_failure_to_does_not_fail_the_step(self):
        for answer in (("204", ""), ("000", None)):
            with self.subTest(status=answer[0]):
                folder = self.folder()
                done = ran(script("app-token-revoke"), folder, **self.stub_curl(folder, [answer]),
                           APP_TOKEN="the-app", GITHUB_API_URL="https://api.example")
                self.assertEqual(done.returncode, 0, done.stderr)
                called = read_text(folder / "calls").splitlines()
                self.assertEqual(len(called), 1)
                self.assertIn("-X DELETE", called[0])
                self.assertIn("Authorization: Bearer the-app", called[0])
                self.assertTrue(called[0].endswith("https://api.example/installation/token"))
