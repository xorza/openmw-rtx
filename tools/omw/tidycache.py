"""What `omw tidy` remembers between runs: **a source whose run was clean is not linted again until something its
run read changes**. A run is remembered by the pinned clang-tidy, the source's compile command, and the contents of
every file the run read, as ccache's direct mode keys a compile: the dependency file clang-tidy's own run writes, and
not the build's deps log, as an MSVC build's compiler reads other headers. It names what `-H` does not: a forced
include, and a header a `__has_include` found. The `.clang-tidy` that stands, or does not, in each folder above each
file read is one of them, so a configuration added is a change as one edited is.

**A run's reads are hashed after it**, so a file modified or changed no earlier than the run's start keeps its
sources from being remembered, as ccache keeps a compile that read a file too new: the run may have read it as it
was before. The change time as well as the modification time, which a copy that keeps times sets to the past.

**What a run looked for and did not find is no read**: a header added where an include's search would now find it
first, or one a `__has_include` now finds, moves no key, as it moves no object of the build's own. The store holds
at most one entry a source of the compile database; a source the database no longer has is dropped at the next
write.

**Each read is kept with its digest, and the command as its own**, so a run linted again says what changed since
(`Freshness`): a cache that misses on every run names the file it misses on instead of a count alone."""

import hashlib
import json
import os
import re
from collections import Counter
from collections.abc import Iterable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from omw import pins
from omw.system import SYSTEM, jobs

# The store's layout; a store of another is no store.
FORMAT = 2

# The one rule of a dependency file, its continuations joined: the target, which a drive's colon may stand in, up
# to the first colon a space or the end follows.
_RULE = re.compile(r"(?s)^.*?:(?:\s|$)(?P<prerequisites>.*)$")

# A path of a rule, a space in it escaped by a backslash.
_PREREQUISITE = re.compile(r"(?:\\ |\S)+")

# A flag that has the compile read a file its dependency file may not name, as Clang's driver and MSVC's spell it: a
# precompiled header, whose own headers it names only where asked, a response file, a file system overlay, a driver
# configuration, a module map, a sanitizer's ignore list.
_UNLISTED = re.compile(r"(?:^|\s)(?:-include-pch|[-/]Yu|@|-ivfsoverlay|--config|-fmodule-map-file"
                       r"|-fsanitize-ignorelist)")

# The program that compiles, the first word of a command, quoted where its path has a space.
_PROGRAM = re.compile(r'^\s*(?:"(?P<quoted>[^"]*)"|(?P<bare>\S+))')

# MSVC's flags clang-cl does not take, each with what it means in Clang's own spelling. `/arch:SSE4.2`, which MSVC
# 17.10 added and `vcpkg/flags.cmake` builds with, is x86-64-v2 (SSE4.2 and POPCNT); clang-cl ignores it with a
# warning the tree's checks make an error, and the run lints nothing.
_MSVC_IN_CLANG = {"/arch:SSE4.2": "-march=x86-64-v2"}

# The digest of a file that is not there, which no SHA-256 spells.
ABSENT = "-"


def dependencies(text: str, directory: Path) -> list[str]:
    """The files a dependency file as Clang writes it names, plain and absolute: `target: a b \\` and continuation
    lines, a space in a path escaped by a backslash, `#` by one and `$` doubled. A relative path is from the compile's
    `directory`, where clang-tidy runs it. Nothing where the text holds no rule."""
    match = _RULE.match(text.replace("\\\r\n", " ").replace("\\\n", " "))
    if match is None:
        return []
    return [os.path.normpath(directory / path.replace("\\ ", " ").replace("\\#", "#").replace("$$", "$"))
            for path in _PREREQUISITE.findall(match["prerequisites"])]


def configurations(read: Iterable[str]) -> set[str]:
    """The `.clang-tidy` of every folder above each file `read`, up to the root, whether it stands or not."""
    folders: set[str] = set()
    for path in read:
        folder = os.path.dirname(path)
        while folder not in folders:
            folders.add(folder)
            parent = os.path.dirname(folder)
            if parent == folder:
                break
            folder = parent
    return {os.path.join(folder, ".clang-tidy") for folder in folders}


def _digest(path: str) -> str:
    try:
        with Path(path).open("rb") as file:
            return hashlib.file_digest(file, "sha256").hexdigest()
    except FileNotFoundError:
        return ABSENT


class Digests:
    """Each file's SHA-256 by its path, read once for as long as the object lives."""

    def __init__(self) -> None:
        self._known: dict[str, str] = {}

    def fill(self, paths: Iterable[str]) -> None:
        wanted = list({path for path in paths if path not in self._known})
        with ThreadPoolExecutor(max_workers=jobs()) as pool:
            self._known.update(zip(wanted, pool.map(_digest, wanted), strict=True))

    def __getitem__(self, path: str) -> str:
        return self._known[path]


@dataclass(frozen=True)
class Compile:
    """How the build compiles a source, from its compile database entry."""

    directory: str
    command: str

    @property
    def reads_unlisted(self) -> bool:
        """Whether the compile reads a file its dependency file may not name, so no key of its run is whole."""
        return _UNLISTED.search(self.command) is not None

    def extra_args(self, path: Path) -> list[str]:
        """What clang-tidy's run of the compile adds to its command: that it write its dependency file to `path`,
        and under MSVC's driver, clang-cl, Clang's spelling of each flag of MSVC's it does not take
        (`_MSVC_IN_CLANG`). The tooling strips every `-M` flag of a command, which `-Wp,` hides; clang-cl takes a
        flag of Clang's own as `/clang:`, and ignores it bare without a word."""
        # `-Wp,` splits its argument at each comma.
        assert "," not in str(path)
        program = _PROGRAM.match(self.command)
        assert program is not None
        # Clang's own rule for its driver's mode: the program's name, whichever system's separators its path has.
        name = re.split(r"[\\/]", program["quoted"] or program["bare"])[-1].lower().removesuffix(".exe")
        msvc = name == "cl" or name.endswith("-cl") or "--driver-mode=cl" in self.command
        if not msvc:
            return [f"--extra-arg=-Wp,-MD,{path}"]
        flags = self.command.split()
        return [f"--extra-arg=/clang:-Wp,-MD,{path}",
                *(f"--extra-arg=/clang:{clang}" for msvc_flag, clang in _MSVC_IN_CLANG.items() if msvc_flag in flags)]


def command_digest(compile: Compile) -> str:
    """What a remembered run keeps of how it ran, as a digest: the pinned clang-tidy, and the folder and command of
    the source's compile."""
    return hashlib.sha256(f"{pins.CLANG_TIDY[SYSTEM].sha256}\n{compile.directory}\n{compile.command}\n".encode()
                          ).hexdigest()


@dataclass(frozen=True)
class Read:
    """A file a run read, and its SHA-256 as the run read it, `ABSENT` where it did not stand."""

    path: str
    digest: str


@dataclass(frozen=True)
class Remembered:
    """A source's clean run: its compile (`command_digest`) and every file it read, the source and the configurations
    among them, in path order."""

    command: str
    reads: tuple[Read, ...]


@dataclass(frozen=True)
class Freshness:
    """Which remembered runs still stand, and what moved under those that do not: how many ran otherwise
    (`command_digest`), and how many each file moved, the first of a run's reads in path order whose contents did."""

    unchanged: set[str]
    ran_otherwise: int
    read_moved: Counter[str]


def load(store: Path) -> dict[str, Remembered]:
    """The clean runs `store` holds, by source; none where it is missing or of another layout."""
    if not store.is_file():
        return {}
    held = json.loads(store.read_text(encoding="utf-8"))
    if held.get("format") != FORMAT:
        return {}
    files = [Read(path, digest) for path, digest in held["files"]]
    return {source: Remembered(entry["command"], tuple(files[index] for index in entry["reads"]))
            for source, entry in held["sources"].items()}


def save(store: Path, remembered: dict[str, Remembered]) -> None:
    """`remembered` into `store`, each file and digest written once, in place of what it held all at once."""
    index: dict[Read, int] = {}
    sources = {source: {"command": run.command, "reads": [index.setdefault(read, len(index)) for read in run.reads]}
               for source, run in sorted(remembered.items())}
    written = store.with_name(store.name + ".new")
    written.write_text(json.dumps({"format": FORMAT, "files": [[read.path, read.digest] for read in index],
                                   "sources": sources}, separators=(",", ":")), encoding="utf-8")
    os.replace(written, store)


def freshness(compiles: dict[str, Compile], remembered: dict[str, Remembered]) -> Freshness:
    """The sources of `compiles` whose remembered run stands: run as it ran, every file it read as it read it."""
    known = {source: run for source, run in remembered.items() if source in compiles}
    digests = Digests()
    digests.fill(read.path for run in known.values() for read in run.reads)
    unchanged: set[str] = set()
    ran_otherwise = 0
    read_moved: Counter[str] = Counter()
    for source, run in known.items():
        if command_digest(compiles[source]) != run.command:
            ran_otherwise += 1
            continue
        moved = next((read.path for read in run.reads if digests[read.path] != read.digest), None)
        if moved is None:
            unchanged.add(source)
        else:
            read_moved[moved] += 1
    return Freshness(unchanged, ran_otherwise, read_moved)


def _touched_since(path: str, started_ns: int) -> bool:
    """Whether `path` was modified or changed at or after `started_ns`. Windows' change time is the file's creation,
    which a file written anew over another moves too. A file not there was not touched: its digest says it is not."""
    try:
        stat = Path(path).stat()
    except FileNotFoundError:
        return False
    return max(stat.st_mtime_ns, stat.st_ctime_ns) >= started_ns


def remember(compiles: dict[str, Compile], reads: dict[str, list[str]], started_ns: int) -> dict[str, Remembered]:
    """The clean runs of `compiles`, each by the files its run `reads` named: those whose every read, the source and
    the configurations among them, was untouched since `started_ns`, by the file system's clock, and whose every
    file read, a configuration aside, is still there: the run read it, so one gone since is a change."""
    listed = {source: {source, *read} for source, read in reads.items()}
    whole = {source: tuple(sorted(files | configurations(files))) for source, files in listed.items()}
    digests = Digests()
    digests.fill(path for read in whole.values() for path in read)
    touched = {path: _touched_since(path, started_ns) for path in {path for read in whole.values() for path in read}}
    return {source: Remembered(command_digest(compiles[source]), tuple(Read(path, digests[path]) for path in read))
            for source, read in whole.items()
            if not any(touched[path] for path in read) and all(digests[path] != ABSENT for path in listed[source])}
