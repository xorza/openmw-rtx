"""`omw [flavour] remote <host> <verb> [args]` on a Windows host: the tree's own clone there takes this commit, builds
it there with MSVC, and runs the verb in the logged-on user's session, the one place a run on Windows has a surface a
swapchain takes (`docs/research/2026-11-01-windows-tests-over-ssh.md`). The files the line names are brought back.

**Started by an interactive scheduled task**, since a process SSH starts has no desktop; the task's script writes the
run's output and its exit status to files this side waits on, a bounded while, as the task has no console to read."""

import base64
import subprocess
import time
import uuid
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath

from omw.system import ROOT, Refusal, output, run, status

SSH = ("-o", "BatchMode=yes")

# Where the trees' clones stand on the host, from the user's profile: **a clone a tree of this desk, under its folder's
# name**, so two trees' commits each build in a folder of their own and an A/B of them builds neither again.
CLONES = PureWindowsPath("Projects")

# Where a run's script, output and files stand on the host, from the user's profile.
RUNS = PureWindowsPath(".cache", "omw-remote")

# The interactive task every run is started by, made for each and deleted once it ends.
TASK = "omw-remote"

# How long a run may take before this side stops waiting: the longest tour flown there took about an hour.
RUN_LIMIT_S = 4 * 60 * 60

# How often the host is asked whether the run ended.
POLL_S = 20


def is_windows(probe: str) -> bool:
    """Whether a host answered `echo %OS%` as cmd does on Windows, where a POSIX shell echoes it back."""
    return probe.strip() == "Windows_NT"


def cmd_quote(word: str) -> str:
    """`word` as one argument of a cmd line: quoted where it holds a space, a quote or a cmd operator, its quotes
    doubled as the C runtime reads them."""
    if word and not any(c in word for c in ' \t"&|<>^%'):
        return word
    return '"' + word.replace('"', '""').replace("%", "%%") + '"'


@dataclass(frozen=True)
class WindowsRun:
    """A run's folder on the host and the paths it maps there from this machine's."""
    folder: PureWindowsPath

    @property
    def script(self) -> PureWindowsPath:
        return self.folder / "run.cmd"

    @property
    def log(self) -> PureWindowsPath:
        return self.folder / "log.txt"

    @property
    def done(self) -> PureWindowsPath:
        return self.folder / "done.txt"

    def mapped(self, path: Path) -> PureWindowsPath:
        """Where `path`, one of this machine's, stands on the host: under the run's folder by its own name, which is
        all a run reads it by."""
        return self.folder / "files" / path.name


def windows_script(build: PureWindowsPath, run_: WindowsRun, verb: str, args: list[str],
                   env: list[tuple[str, str]]) -> str:
    """The batch file the task runs: the environment the line names, then the harness's `verb` in `build`, its
    output into the run's log and its status into the file this side waits on, last, so its presence is the end."""
    lines = ["@echo off", f"cd /d {cmd_quote(str(build))}"]
    lines += [f'set "{name}={value}"' for name, value in env]
    command = " ".join(cmd_quote(word) for word in ["openmw-rtxtool.exe", verb, *args])
    lines.append(f"{command} > {cmd_quote(str(run_.log))} 2>&1")
    lines.append(f"echo %ERRORLEVEL% > {cmd_quote(str(run_.done))}")
    return "\r\n".join(lines) + "\r\n"


def scp_path(host: str, path: PureWindowsPath) -> str:
    """`path` on `host` as scp names it: forward slashes, which Windows' OpenSSH reads, the drive kept."""
    return f"{host}:{path.as_posix()}"


class WindowsHost:
    """A Windows host reached over SSH, every command run by its cmd."""

    def __init__(self, ssh: str, scp: str, host: str):
        self.ssh, self.scp, self.host = ssh, scp, host
        self.profile = PureWindowsPath(self.cmd("echo %USERPROFILE%").strip())

    def cmd(self, line: str) -> str:
        return output([self.ssh, *SSH, self.host, line])

    def call(self, line: str) -> int:
        return subprocess.run([self.ssh, *SSH, self.host, line], check=False).returncode

    def exists(self, path: PureWindowsPath) -> bool:
        return self.call(f"if exist {cmd_quote(str(path))} (exit 0) else (exit 1)") == 0

    def put(self, local: Path, remote: PureWindowsPath) -> None:
        run([self.scp, "-q", *SSH, "-r", str(local), scp_path(self.host, remote)])

    def fetch(self, remote: PureWindowsPath, local: Path) -> int:
        return subprocess.run([self.scp, "-q", *SSH, "-r", scp_path(self.host, remote), str(local)],
                              check=False, stderr=subprocess.DEVNULL).returncode

    def write(self, remote: PureWindowsPath, text: str) -> None:
        """`text` into `remote`, through PowerShell, which decodes it whole where cmd's echo splits lines and quotes."""
        encoded = base64.b64encode(text.encode()).decode()
        self.cmd(f"powershell -NoProfile -Command \"[IO.File]::WriteAllBytes('{remote}', "
                 f"[Convert]::FromBase64String('{encoded}'))\"")


def bring_commit(host: WindowsHost, tree: PureWindowsPath, git: Callable[[list[str]], str]) -> str:
    """This commit into the host's clone, checked out detached: what its clone lacks comes as a bundle, which needs
    no Git server on either machine, and a commit it holds already comes as nothing, an ancestor of what it stands at
    among them, whose bundle would be empty. The fetch forces `refs/omw/remote`, which names only the commit last
    brought and so stands on another line whenever two trees run there. Refuses a clone with changes of its own, which
    the checkout would carry along."""
    commit = git(["rev-parse", "HEAD"]).strip()
    if host.cmd(f"cd /d {cmd_quote(str(tree))} && git status --porcelain").strip():
        raise Refusal(f"remote: {host.host}'s clone at {tree} has changes of its own; commit or drop them there")
    theirs = host.cmd(f"cd /d {cmd_quote(str(tree))} && git rev-parse HEAD").strip()
    # The object alone, as cmd reads a caret as its escape and `^{commit}` would reach git without it.
    held = host.call(f"cd /d {cmd_quote(str(tree))} && git cat-file -e {commit}") == 0
    if theirs != commit and not held:
        known = subprocess.run(["git", "-C", str(ROOT), "cat-file", "-e", f"{theirs}^{{commit}}"],
                               check=False, stderr=subprocess.DEVNULL).returncode == 0
        bundle = ROOT / "out" / "omw-remote.bundle"
        span = [f"{theirs}..HEAD"] if known else ["HEAD"]
        run(["git", "-C", str(ROOT), "bundle", "create", str(bundle), *span])
        remote_bundle = host.profile / RUNS / "omw-remote.bundle"
        host.cmd(f"if not exist {cmd_quote(str(remote_bundle.parent))} mkdir {cmd_quote(str(remote_bundle.parent))}")
        host.put(bundle, remote_bundle)
        bundle.unlink()
        host.cmd(f"cd /d {cmd_quote(str(tree))} && git fetch -q {cmd_quote(str(remote_bundle))} +HEAD:refs/omw/remote")
    if theirs != commit:
        host.cmd(f"cd /d {cmd_quote(str(tree))} && git checkout -q --detach {commit}")
    return commit


def windows_args(args: list[str], paths: list[Path], run_: WindowsRun) -> list[str]:
    """`args`, a line `remote_line` made absolute, with each of `paths` it names as `--name=path` the host's."""
    named = {str(path): str(run_.mapped(path)) for path in paths}
    mapped = []
    for word in args:
        name, equals, value = word.partition("=")
        mapped.append(f"{name}={named[value]}" if equals and value in named else word)
    return mapped


def remote_windows(ssh: str, scp: str, host_name: str, flavour: str, verb: str, args: list[str],
                   env: list[tuple[str, str]], outputs: list[Path], inputs: list[Path],
                   git: Callable[[list[str]], str]) -> int:
    """The run on a Windows host: `args` as `remote_line` made them, their paths mapped into the run's folder there."""
    host = WindowsHost(ssh, scp, host_name)
    tree = host.profile / CLONES / ROOT.name
    if not host.exists(tree / ".git"):
        raise Refusal(f"remote: {host_name} has no clone at {tree} for this tree: make one there, a `git clone` of "
                      f"another tree's clone with its out\\deps")
    commit = bring_commit(host, tree, git)
    print(f"remote: {host_name} builds {commit[:10]}", flush=True)
    if host.call(f"cd /d {cmd_quote(str(tree))} && omw.cmd {flavour} build openmw-rtxtool") != 0:
        raise Refusal(f"remote: the build on {host_name} failed, as it says above")

    run_ = WindowsRun(host.profile / RUNS / uuid.uuid4().hex[:12])
    host.cmd(f"mkdir {cmd_quote(str(run_.folder / 'files'))}")
    for path in inputs:
        host.put(path, run_.mapped(path))

    line = windows_args(args, [*outputs, *inputs], run_)
    host.write(run_.script, windows_script(tree / "out" / flavour, run_, verb, line, env))
    host.cmd(f"schtasks /create /tn {TASK} /tr {cmd_quote(str(run_.script))} /sc once /st 23:59 /it /f")
    host.cmd(f"schtasks /run /tn {TASK}")
    print(f"remote: {verb} runs in {host_name}'s session, its output in {run_.log}", flush=True)

    try:
        waited = 0
        while not host.exists(run_.done):
            if waited >= RUN_LIMIT_S:
                raise Refusal(f"remote: {host_name}'s run did not end in {RUN_LIMIT_S} s; its output is {run_.log}")
            time.sleep(POLL_S)
            waited += POLL_S
    finally:
        # Deleted however the wait ends: a task made to run once still fires at the time it names, and would start
        # the run again there.
        host.cmd(f"schtasks /delete /tn {TASK} /f")

    code = int(host.cmd(f"type {cmd_quote(str(run_.done))}").strip() or "1")
    print(host.cmd(f"type {cmd_quote(str(run_.log))}"), end="", flush=True)
    for path in outputs:
        fetched = host.fetch(run_.mapped(path), path.parent)
        print(f"remote: {path}{'' if fetched == 0 else ' was not written there'}", flush=True)
    return status(code)
