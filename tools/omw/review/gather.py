"""`omw review gather`: what a read of one item reads, written as plain files into a folder the agent's job lays
beside the instructions. It runs with no secret and no write, as it opens what a stranger sent: a crash package, a
log, a picture. Every text a person wrote is cut to what a reader of the page sees (`text.visible`), and what was
cut is listed in `hidden.txt` for the agent to report."""

import json
import re
import subprocess
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from omw import crash, deps, fetch
from omw.review import diff as diffs
from omw.review.github import GitHub, write_output
from omw.review.item import BOT, BOT_GRAPHQL, Key, markers
from omw.review.text import visible
from omw.system import ROOT, Refusal, output, run

# The comments of a discussion a read takes, the latest.
COMMENTS = 50
# Attachments a read takes, and the bytes of each.
ATTACHMENTS = 5
ATTACHMENT_BYTES = 25 << 20
# What of a text attachment, or of a text inside a zip, the agent is handed.
TEXT_BYTES = 2 << 20
# What the files inside a stranger's zip may say they hold, together: the zip itself is bounded, and what it
# inflates to is not, but a member is never read past the size it states.
INFLATED_BYTES = 512 << 20
# What a malformed archive or dump raises as it is read: each is the stranger's fault, and is reported as unread.
_UNREADABLE = (Refusal, OSError, zipfile.BadZipFile, zlib.error, EOFError, NotImplementedError,
               subprocess.CalledProcessError)
# Releases searched for the symbols of a crash, newest first, and the bytes of a symbols archive.
RELEASES = 3
SYMBOLS_BYTES = 400 << 20
# Files of a pull request written as it has them.
FILES = 3000
# The agent's turns: a floor, and one for each so many changed lines, to a ceiling.
TURNS_FLOOR = 40
TURNS_LINES = 50
TURNS_CEILING = 200


@dataclass
class Folder:
    """The folder a read writes, and what it cut from the texts it wrote."""

    root: Path
    hidden: list[str] = field(default_factory=list)

    def text(self, where: str, text: str) -> str:
        seen = visible(text)
        self.hidden += [f"{where}: {cut}" for cut in seen.hidden]
        return seen.text

    def write(self, name: str, content: str | bytes) -> None:
        path = self.root / name
        assert path.resolve().is_relative_to(self.root.resolve()), name
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, bytes):
            path.write_bytes(content)
        else:
            path.write_text(content, encoding="utf-8")


def _post(folder: Folder, where: str, post: dict[str, Any]) -> str:
    when = post.get("submitted_at") or post.get("created_at") or ""
    return f"### {post['user']['login']}, {when}, {where}\n\n{folder.text(where, post.get('body') or '')}\n"


def _discussion(github: GitHub, folder: Folder, key: Key) -> list[dict[str, Any]]:
    """What people said on the item, the latest `COMMENTS` of each kind, the workflow's own answers left out; the
    comments themselves, for their attachments."""
    comments = [comment for comment in github.pages(f"issues/{key.number}/comments", 10)
                if comment["user"]["login"] != BOT][-COMMENTS:]
    parts = [_post(folder, f"comment {comment['id']}", comment) for comment in comments]
    if key.kind == "pr":
        reviews = [review for review in github.pages(f"pulls/{key.number}/reviews", 5)
                   if review["user"]["login"] != BOT and review.get("body")][-COMMENTS:]
        inline = [comment for comment in github.pages(f"pulls/{key.number}/comments", 10)
                  if comment["user"]["login"] != BOT][-COMMENTS:]
        parts += [_post(folder, f"review {review['id']}", review) for review in reviews]
        parts += [_post(folder, f"inline comment on {comment['path']}:{comment.get('line') or '?'}", comment)
                  for comment in inline]
    folder.write("discussion.md", "\n".join(parts) or "Nobody has commented.\n")
    return comments


def _item(folder: Folder, raw: dict[str, Any], key: Key, extra: dict[str, Any]) -> None:
    folder.write("item.json", json.dumps({
        "item": str(key),
        "title": folder.text("the title", raw["title"]),
        "author": raw["user"]["login"],
        "association": raw["author_association"],
        "url": raw["html_url"],
        "created": raw["created_at"],
        **extra,
    }, indent=2))
    folder.write("body.md", folder.text("the body", raw.get("body") or ""))


def _git(*args: str) -> str:
    return output(["git", "-c", "core.quotePath=false", *args], cwd=ROOT)


@dataclass(frozen=True)
class Pulled:
    head: str
    lines: int


def _files(folder: Folder, base: str, head: str) -> int:
    """The files the pull request adds or changes, as it has them, under `pr-head/`: blobs only, as a link would
    lead a reader out of the folder and a submodule holds nothing to read. The links and submodules are listed."""
    names = [name for name in _git("diff", "--name-only", "-z", "--diff-filter=d", "-M", base, head).split("\0")
             if name]
    skipped = []
    for name in names[:FILES]:
        mode = _git("ls-tree", "-z", head, "--", name).split(" ", 1)[0]
        if mode not in ("100644", "100755"):
            skipped.append(f"{name} (mode {mode})")
            continue
        blob = run(["git", "cat-file", "blob", f"{head}:{name}"], cwd=ROOT, capture_output=True).stdout
        folder.write(f"pr-head/{name}", blob)
    if len(names) > FILES:
        skipped.append(f"{len(names) - FILES} files past the first {FILES}")
    folder.write("skipped.txt", "".join(f"{line}\n" for line in skipped))
    return len(names)


def _previous(github: GitHub, folder: Folder, key: Key) -> None:
    """The workflow's last review of the pull request, and its threads still open, which the agent may name as
    answered."""
    reviews = [review for review in github.pages(f"pulls/{key.number}/reviews", 5)
               if markers([(review["user"]["login"], review.get("body") or "")], key)]
    owner, name = github.name.split("/")
    threads = github.graphql(
        """query($owner: String!, $name: String!, $number: Int!) {
          repository(owner: $owner, name: $name) { pullRequest(number: $number) {
            reviewThreads(first: 100) { nodes { id isResolved path line
              comments(first: 1) { nodes { author { login } body } } } } } } }""",
        owner=owner, name=name, number=key.number)["repository"]["pullRequest"]["reviewThreads"]["nodes"]
    open_threads = [{"id": thread["id"], "path": thread["path"], "line": thread["line"],
                     "body": thread["comments"]["nodes"][0]["body"]}
                    for thread in threads if not thread["isResolved"] and thread["comments"]["nodes"]
                    and (thread["comments"]["nodes"][0]["author"] or {}).get("login") == BOT_GRAPHQL]
    folder.write("previous.json", json.dumps({
        "summary": reviews[-1]["body"] if reviews else "",
        "threads": open_threads,
    }, indent=2))


def gather_pull(github: GitHub, folder: Folder, key: Key) -> Pulled:
    raw = github.get(f"pulls/{key.number}")
    run(["git", "fetch", "--no-tags", "--quiet", "origin", f"+refs/pull/{key.number}/head:refs/review/head"],
        cwd=ROOT)
    head = _git("rev-parse", "refs/review/head").strip()
    base = _git("merge-base", "origin/master", head).strip()
    patch = _git("diff", "--no-color", "--no-ext-diff", "--no-textconv", "-M", base, head)
    parsed = diffs.parse(patch)
    folder.write("diff.patch", patch)
    folder.write("changes.txt", diffs.numbered(parsed))
    lines = sum(line.kind in ("+", "-") for file in parsed for hunk in file.hunks for line in hunk.lines)
    files = _files(folder, base, head)
    _item(folder, raw, key, {"draft": bool(raw["draft"]), "base": raw["base"]["ref"], "head": head,
                             "merge_base": base, "files": files, "lines": lines})
    _discussion(github, folder, key)
    _previous(github, folder, key)
    checks = github.pages(f"commits/{head}/check-runs", 2, key="check_runs")
    folder.write("checks.txt", "".join(f"{check['name']}: {check['conclusion'] or check['status']}\n"
                                       for check in checks) or "No check has run at this head.\n")
    return Pulled(head, lines)


_ATTACHMENT = re.compile(r"https://(?:github\.com/user-attachments/(?:files|assets)/|"
                         r"github\.com/[\w.-]+/[\w.-]+/(?:files|assets)/|user-images\.githubusercontent\.com/)"
                         r"[A-Za-z0-9._/%-]+")
_KINDS = ((b"PK\x03\x04", "zip"), (b"MDMP", "dmp"), (b"\x89PNG", "png"), (b"\xff\xd8\xff", "jpg"),
          (b"GIF8", "gif"))


def kind_of(data: bytes) -> str | None:
    """What an attachment is by its first bytes, not by the name a stranger gave it: an archive, a dump, a
    picture the agent can look at, or text; none where it is none of them."""
    for magic, kind in _KINDS:
        if data.startswith(magic):
            return kind
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "webp"
    head = data[:TEXT_BYTES]
    if b"\0" in head:
        return None
    try:
        head.decode("utf-8")
    except UnicodeDecodeError as error:
        # A multibyte character cut at the bound is text still.
        if error.start < len(head) - 4:
            return None
    return "txt"


def attachments_in(texts: list[str]) -> list[str]:
    """The attachment URLs of `texts`, each once, in order, at most `ATTACHMENTS`."""
    found: list[str] = []
    for text in texts:
        for url in _ATTACHMENT.findall(text):
            if url not in found:
                found.append(url)
    return found[:ATTACHMENTS]


@dataclass(frozen=True)
class Symbols:
    tag: str
    store: Path


def _symbols(github: GitHub, module: crash.Module, scratch: Path) -> Symbols | None:
    """The newest of the last `RELEASES` releases whose symbols hold `module`'s."""
    system = "windows" if module.debug_file.lower().endswith(".pdb") else "linux"
    releases = [release for release in github.pages("releases", 1)
                if release["tag_name"].startswith("rtx-v") and not release["draft"]][:RELEASES]
    for release in releases:
        assets = [asset for asset in release["assets"] if asset["name"].endswith(f"-{system}-symbols.zip")]
        if not assets:
            continue
        # Apart from the stores, which `store_of` unpacks into a folder of the archive's own name.
        archive = scratch / "downloads" / assets[0]["name"]
        if not archive.is_file():
            fetch.download(assets[0]["browser_download_url"], archive, limit=SYMBOLS_BYTES)
        store = crash.store_of(module, archive, scratch / "stores")
        if store is not None:
            return Symbols(release["tag_name"], store)
    return None


@dataclass(frozen=True)
class CrashRead:
    text: str
    tag: str


def check_inflated(archive: Path) -> None:
    """Refuses a zip whose members say they hold more than `INFLATED_BYTES` together, before any is read."""
    with zipfile.ZipFile(archive) as zipped:
        stated = sum(member.file_size for member in zipped.infolist())
    if stated > INFLATED_BYTES:
        raise Refusal(f"the zip says it holds {stated} bytes, past {INFLATED_BYTES}")


def _read_crash(github: GitHub, package: Path, scratch: Path) -> CrashRead:
    """Each dump of a crash package, described and walked against its release's symbols."""
    walker = deps.crash_tool("minidump-stackwalk")
    lines: list[str] = []
    tag = ""
    for dump in crash.dumps_in(package, scratch):
        lines += [f"## {dump.name}", ""]
        lines += crash.describe_dump(dump.path.read_bytes())
        module = crash.parse_module(output([walker, "--json", dump.path]))
        symbols = _symbols(github, module, scratch)
        if symbols is None:
            lines.append(f"No release of the last {RELEASES} holds the symbols of {module}; the stack is unnamed.")
            lines.append(output([walker, "--human", dump.path]))
            continue
        tag = symbols.tag
        lines.append(f"Read against the symbols of {symbols.tag}.")
        lines.append(output([walker, "--human", dump.path, symbols.store]))
    return CrashRead("\n".join(lines), tag)


def _zip_texts(folder: Folder, archive: Path, name: str) -> list[str]:
    """The logs and texts inside a zip, each cut to `TEXT_BYTES`, read without trusting what the zip says of its
    sizes."""
    written = []
    with zipfile.ZipFile(archive) as zipped:
        members = [member for member in zipped.infolist()
                   if not member.is_dir() and member.filename.lower().endswith((".log", ".txt", ".cfg"))]
        for index, member in enumerate(members[:ATTACHMENTS]):
            with zipped.open(member) as opened:
                data = opened.read(TEXT_BYTES)
            if kind_of(data) == "txt":
                written.append(f"{name}-{index}.txt")
                folder.write(f"attachments/{name}-{index}.txt",
                             folder.text(f"{member.filename} in {name}", data.decode("utf-8", "replace")))
    return written


def _attachments(github: GitHub, folder: Folder, texts: list[str], scratch: Path) -> str:
    """Each attachment downloaded, looked at by its bytes, and written where the agent reads it; the tag of the
    release a crash came from, or empty."""
    listed = []
    tag = ""
    crashes = []
    for index, url in enumerate(attachments_in(texts)):
        name = f"{index}"
        download = scratch / f"attachment-{index}"
        try:
            fetch.download(url, download, limit=ATTACHMENT_BYTES)
        except Refusal as refused:
            listed.append(f"{url}: not read, {refused}")
            continue
        data = download.read_bytes()
        kind = kind_of(data)
        if kind in ("png", "jpg", "gif", "webp"):
            folder.write(f"attachments/{name}.{kind}", data)
            listed.append(f"{url}: attachments/{name}.{kind}")
        elif kind == "txt":
            folder.write(f"attachments/{name}.txt",
                         folder.text(f"attachment {url}", data[:TEXT_BYTES].decode("utf-8", "replace")))
            listed.append(f"{url}: attachments/{name}.txt")
        elif kind in ("zip", "dmp"):
            try:
                if kind == "zip":
                    check_inflated(download)
                    listed += [f"{url}: attachments/{written}" for written in _zip_texts(folder, download, name)]
                read = _read_crash(github, download, scratch)
                crashes.append(read.text)
                tag = tag or read.tag
                listed.append(f"{url}: a crash package, read into crash.txt")
            except _UNREADABLE as refused:
                listed.append(f"{url}: not read as a crash package, {refused}")
        else:
            listed.append(f"{url}: not read, neither a picture, text, a zip nor a dump")
    folder.write("attachments.txt", "".join(f"{line}\n" for line in listed) or "No attachment.\n")
    folder.write("crash.txt", "\n\n".join(crashes) or "No crash package.\n")
    return tag


def gather_issue(github: GitHub, folder: Folder, key: Key, scratch: Path) -> str:
    raw = github.get(f"issues/{key.number}")
    _item(folder, raw, key, {})
    comments = _discussion(github, folder, key)
    index = [issue for issue in github.pages("issues", 10, state="all") if "pull_request" not in issue]
    titles = [(issue["number"], issue["state"], folder.text(f"the title of #{issue['number']}", issue["title"]))
              for issue in index if issue["number"] != key.number]
    folder.write("issues.txt", "".join(f"#{number} [{state}] {title}\n" for number, state, title in titles))
    texts = [raw.get("body") or ""] + [comment.get("body") or "" for comment in comments]
    tag = _attachments(github, folder, texts, scratch)
    folder.write("release.txt", f"{tag}\n" if tag else "")
    return tag


def gather(github: GitHub, key: Key, out: Path, scratch: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    folder = Folder(out)
    if key.kind == "pr":
        pulled = gather_pull(github, folder, key)
        write_output("head", pulled.head)
        write_output("turns", str(min(TURNS_CEILING, TURNS_FLOOR + pulled.lines // TURNS_LINES)))
        write_output("release", "")
    else:
        tag = gather_issue(github, folder, key, scratch)
        write_output("head", "-")
        write_output("turns", str(TURNS_FLOOR + 20))
        write_output("release", tag)
    folder.write("hidden.txt", "".join(f"{line}\n" for line in folder.hidden) or "Nothing was hidden.\n")

