"""`omw tidy [--fix] [--changed] [file...]`: the pinned clang-tidy under the tree's `.clang-tidy`, over the sources
of `apps/` and `components/` the build's compile database holds: every one, those the working tree changed and
those that include a changed header (`--changed`), or those the files named are or are included by. `--fix` applies
the checks' fixes, then formats the working tree's changed files as `build` does. A source whose last run was
clean and whose run's every read stands as it was then is not linted again (`tidycache`).

**A finding is printed once**: a header's is met by every source that includes it, 12,681 times for 477 headers in
one run."""

import json
import os
import re
import subprocess
import tempfile
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

from omw import deps, formatting, tidycache
from omw.build import Build
from omw.system import ROOT, Refusal, jobs, output

# The folders whose sources the tree's `.clang-tidy` holds to it; `extern/` is vendored, as `out/` is generated.
LINTED = (ROOT / "apps", ROOT / "components")

# How often a run writes the clean sources it has so far to the store, in seconds: a run cancelled or killed keeps what
# it linted up to its last write, as a whole tree's lint on CI is an hour and more, and a newer push cancels it.
CHECKPOINT_S = 30.0

# Where a diagnostic begins: `path:line:column: warning: text [check]`. Its notes and the source it quotes follow.
_DIAGNOSTIC = re.compile(r"^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): (?:warning|error): .*\[(?P<checks>[^\]]+)\]$")


@dataclass(frozen=True)
class Source:
    """A source of the compile database, the object it compiles to, by which Ninja's deps log names it, and how."""

    file: Path
    object: Path
    compile: tidycache.Compile


def linted_sources(database: list[dict]) -> list[Source]:
    """The database's sources in `LINTED`, each once, in path order."""
    found: dict[Path, Source] = {}
    for entry in database:
        file = Path(entry["file"])
        if any(file.is_relative_to(folder) for folder in LINTED):
            found.setdefault(file, Source(file, Path(entry["directory"]) / entry["output"],
                                          tidycache.Compile(entry["directory"], entry["command"])))
    return sorted(found.values(), key=lambda source: source.file)


def headers_by_object(listing: str, directory: Path) -> dict[Path, set[Path]]:
    """What `ninja -t deps` says each object read: an unindented `<object>: #deps …` line, then its inputs indented.
    A relative path is from the build `directory`."""
    read: dict[Path, set[Path]] = {}
    current: set[Path] | None = None
    for line in listing.splitlines():
        if line.startswith("    ") and current is not None:
            current.add((directory / line.strip()).resolve())
        elif ": #deps" in line:
            current = read.setdefault((directory / line.split(": #deps", 1)[0]).resolve(), set())
    return read


def sources_of(named: list[Path], sources: list[Source], read: dict[Path, set[Path]]) -> list[Source]:
    """The sources `named` are, and those that read one of them, in path order. **A source the deps log has no line
    for yet is taken whole**, as it was never built and what it reads is unknown."""
    wanted = {path.resolve() for path in named}
    return [source for source in sources
            if source.file.resolve() in wanted or source.object.resolve() not in read
            or not wanted.isdisjoint(read[source.object.resolve()])]


@dataclass(frozen=True)
class Finding:
    """A diagnostic as clang-tidy prints it, its notes and quoted source with it. Keyed by its first line with the
    path made plain: a header is named as each includer reached it, `mwrender/../mwworld/store.hpp` and
    `mwworld/store.hpp` alike."""

    first: str
    text: str
    checks: tuple[str, ...]

    @property
    def key(self) -> str:
        match = _DIAGNOSTIC.match(self.first)
        assert match is not None
        return os.path.normpath(match["path"]) + self.first[match.end("path"):]


def findings(said: str) -> list[Finding]:
    """The diagnostics in one run's output, each with the lines that follow it up to the next."""
    found: list[Finding] = []
    lines: list[str] = []
    checks: tuple[str, ...] = ()
    for line in said.splitlines():
        match = _DIAGNOSTIC.match(line)
        if match is not None:
            if lines:
                found.append(Finding(lines[0], "\n".join(lines), checks))
            lines = [line]
            checks = tuple(check for check in match["checks"].split(",") if check != "-warnings-as-errors")
        elif lines:
            lines.append(line)
    if lines:
        found.append(Finding(lines[0], "\n".join(lines), checks))
    return found


# A path in exported fixes, which YAML quotes in single quotes, a quote inside doubled.
_FIX_PATH = re.compile(r"(?P<key>FilePath:\s*)'(?P<path>(?:[^']|'')*)'")


def plain_paths(fixes: str) -> str:
    """Exported fixes with each path made plain. **A header's fix is one fix however its includers reached it**:
    `clang-apply-replacements` merges a fix two sources made alike only where their paths are alike, and applied
    `doc/../doc/manager.hpp`'s and `doc/manager.hpp`'s both, `override` twice on one line."""
    return _FIX_PATH.sub(lambda match: f"{match['key']}'{os.path.normpath(match['path'])}'", fixes)


def _run(clang_tidy: Path, build: Build, source: Source, fixes: Path | None,
         dependencies: Path) -> subprocess.CompletedProcess:
    """clang-tidy over `source`, its fixes written to `fixes` where it is named, and what it read to `dependencies`."""
    line: list[str | Path] = [clang_tidy, "--quiet", f"-p={build.dir}", source.compile.dependency_file(dependencies),
                              source.file]
    if fixes is not None:
        line.append(f"--export-fixes={fixes}")
    return subprocess.run(line, cwd=ROOT, capture_output=True, text=True, check=False)


def tidy(build: Build, args: list[str]) -> int:
    fix = "--fix" in args
    changed = "--changed" in args
    named = [arg for arg in args if arg not in ("--fix", "--changed")]
    if any(arg.startswith("-") for arg in named):
        raise Refusal("tidy takes `--fix`, `--changed` and the files to lint, or nothing")
    if changed and named:
        raise Refusal("tidy lints the changed files or the files named, not both")

    # Built first, as a source reads what the build generates: Qt's forms, the version, the shaders' headers. The whole
    # tree's lint reads nothing else of the build, so it builds what the sources wait on and none of their objects; a
    # lint of the files named or changed reads Ninja's log of what each object included, which only a build writes.
    if named or changed:
        build.build_default()
    else:
        build.build_generated()
    database = json.loads((build.dir / "compile_commands.json").read_text(encoding="utf-8"))
    every = linted_sources(database)
    sources = every
    if changed:
        named = formatting.changed_files()
        # A changed configuration holds every source to it.
        if any(Path(name).name == ".clang-tidy" for name in named):
            named = []
        elif not named:
            return 0
    if named:
        read = headers_by_object(output([build.ninja, "-C", build.dir, "-t", "deps"], env=build.env), build.dir)
        sources = sources_of([ROOT / name for name in named], sources, read)
    if not sources:
        return 0

    store = build.dir / "tidy-clean.json"
    remembered = tidycache.load(store)
    compiles = {str(source.file): source.compile for source in sources}
    unchanged = tidycache.unchanged(compiles, remembered)
    linted = [source for source in sources if str(source.file) not in unchanged]

    # A source of the database not asked of now keeps what it had, and one linted now what came out clean.
    held = {str(source.file) for source in every}
    kept = {source: run for source, run in remembered.items()
            if (source in held and source not in compiles) or source in unchanged}

    clang_tidy = deps.clang_tidy()
    found: list[list[Finding]] = [[] for _ in linted]
    remembered_now: dict[str, tidycache.Remembered] = {}
    fresh: dict[str, list[str]] = {}
    with tempfile.TemporaryDirectory(prefix="omw-tidy-") as scratch:
        # The runs' start by the clock that stamps a file's modification, and not by `time.time_ns`, which that
        # clock, the kernel's coarse one, lags behind: a file written just after would read as written before.
        started = Path(scratch) / "started"
        started.touch()
        started_ns = started.stat().st_mtime_ns
        # A file a source, by its place in the list: objects share names (`main.cpp.o`), and a fix written over
        # another source's is lost.
        fixes = [Path(scratch) / f"{index}.yaml" if fix else None for index in range(len(linted))]
        dependencies = [Path(scratch) / f"{index}.d" for index in range(len(linted))]
        done: list[subprocess.CompletedProcess | None] = [None] * len(linted)
        pool = ThreadPoolExecutor(max_workers=jobs())
        try:
            pending = {pool.submit(_run, clang_tidy, build, source, into, written): index
                       for index, (source, into, written) in enumerate(zip(linted, fixes, dependencies, strict=True))}
            saved = time.monotonic()
            for future in as_completed(pending):
                index = pending[future]
                source = linted[index]
                run = done[index] = future.result()
                found[index] = findings(run.stdout)
                # What the run read, nothing where it wrote no dependency file, as a run that failed early does not.
                listing = dependencies[index]
                named_reads = (tidycache.dependencies(listing.read_text(encoding="utf-8"),
                                                      Path(source.compile.directory))
                               if listing.is_file() else [])
                # A run that names nothing it read is not trusted to have named its reads.
                if run.returncode == 0 and not found[index] and named_reads and not source.compile.reads_unlisted:
                    fresh[str(source.file)] = named_reads
                if time.monotonic() - saved >= CHECKPOINT_S:
                    remembered_now |= tidycache.remember(compiles, fresh, started_ns)
                    fresh.clear()
                    tidycache.save(store, kept | remembered_now)
                    saved = time.monotonic()
        except BaseException:
            # Stopped early, interrupted or failed: what came out clean is kept as at a checkpoint.
            tidycache.save(store, kept | remembered_now | tidycache.remember(compiles, fresh, started_ns))
            raise
        finally:
            # A run that stops early starts no source it had not reached.
            pool.shutdown(wait=True, cancel_futures=True)
        runs = [run for run in done if run is not None]
        assert len(runs) == len(linted)
        if fix:
            for file in fixes:
                if file is not None and file.is_file():
                    file.write_text(plain_paths(file.read_text(encoding="utf-8")), encoding="utf-8")
            subprocess.run([deps.clang_apply_replacements(), scratch], cwd=ROOT, check=True)

    seen: dict[str, Finding] = {}
    failed = 0
    for source, run, said in zip(linted, runs, found, strict=True):
        for finding in said:
            seen.setdefault(finding.key, finding)
        # A run that failed and said no diagnostic broke, and its own words say how.
        if run.returncode != 0 and not said:
            failed += 1
            print(f"clang-tidy failed on {source.file.relative_to(ROOT)}:\n{run.stderr}")

    for _, finding in sorted(seen.items()):
        print(finding.text)
    tally = Counter(check for finding in seen.values() for check in finding.checks)
    for check, count in tally.most_common():
        print(f"{count:6} {check}")
    print(f"tidy: {len(seen)} findings over {len(sources)} sources, "
          f"{len(unchanged)} of them unchanged since a clean run")

    # After the fixes, whose edits keep out a source that read a file they touched.
    tidycache.save(store, kept | remembered_now | tidycache.remember(compiles, fresh, started_ns))

    if fix:
        formatting.format_tree(formatting.changed_files())
    return 1 if seen or failed else 0
