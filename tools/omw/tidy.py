"""`omw tidy [--fix] [--changed] [file...]`: the pinned clang-tidy under the tree's `.clang-tidy`, over the sources
of `apps/` and `components/` the build's compile database holds: every one, those the working tree changed and
those that include a changed header (`--changed`), or those the files named are or are included by. `--fix` applies
the checks' fixes, then formats the working tree's changed files as `build` does.

**A finding is printed once**: a header's is met by every source that includes it, 12,681 times for 477 headers in
one run."""

import json
import os
import re
import subprocess
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from omw import deps, formatting
from omw.build import Build
from omw.system import ROOT, Refusal, jobs, output

# The folders whose sources the tree's `.clang-tidy` holds to it; `extern/` is vendored, as `out/` is generated.
LINTED = (ROOT / "apps", ROOT / "components")

# Where a diagnostic begins: `path:line:column: warning: text [check]`. Its notes and the source it quotes follow.
_DIAGNOSTIC = re.compile(r"^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): (?:warning|error): .*\[(?P<checks>[^\]]+)\]$")


@dataclass(frozen=True)
class Source:
    """A source of the compile database, and the object it compiles to, by which Ninja's deps log names it."""

    file: Path
    object: Path


def linted_sources(database: list[dict]) -> list[Source]:
    """The database's sources in `LINTED`, each once, in path order."""
    found: dict[Path, Source] = {}
    for entry in database:
        file = Path(entry["file"])
        if any(file.is_relative_to(folder) for folder in LINTED):
            found.setdefault(file, Source(file, Path(entry["directory"]) / entry["output"]))
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


def _run(clang_tidy: Path, build: Build, source: Source, fixes: Path | None) -> subprocess.CompletedProcess:
    """clang-tidy over `source`, its fixes written to `fixes` where it is named."""
    line: list[str | Path] = [clang_tidy, "--quiet", f"-p={build.dir}", source.file]
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

    # Built first, as a source reads what the build generates: Qt's forms, the version, the shaders' headers.
    build.build_default()
    database = json.loads((build.dir / "compile_commands.json").read_text(encoding="utf-8"))
    sources = linted_sources(database)
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

    clang_tidy = deps.clang_tidy()
    with tempfile.TemporaryDirectory(prefix="omw-tidy-") as scratch:
        # A file a source, by its place in the list: objects share names (`main.cpp.o`), and a fix written over
        # another source's is lost.
        fixes = [Path(scratch) / f"{index}.yaml" if fix else None for index in range(len(sources))]
        with ThreadPoolExecutor(max_workers=jobs()) as pool:
            runs = list(pool.map(lambda source, into: _run(clang_tidy, build, source, into), sources, fixes))
        if fix:
            for file in fixes:
                if file is not None and file.is_file():
                    file.write_text(plain_paths(file.read_text(encoding="utf-8")), encoding="utf-8")
            subprocess.run([deps.clang_apply_replacements(), scratch], cwd=ROOT, check=True)

    seen: dict[str, Finding] = {}
    failed = 0
    for source, done in zip(sources, runs, strict=True):
        found = findings(done.stdout)
        for finding in found:
            seen.setdefault(finding.key, finding)
        # A run that failed and said no diagnostic broke, and its own words say how.
        if done.returncode != 0 and not found:
            failed += 1
            print(f"clang-tidy failed on {source.file.relative_to(ROOT)}:\n{done.stderr}")

    for _, finding in sorted(seen.items()):
        print(finding.text)
    tally = Counter(check for finding in seen.values() for check in finding.checks)
    for check, count in tally.most_common():
        print(f"{count:6} {check}")
    print(f"tidy: {len(seen)} findings over {len(sources)} sources")

    if fix:
        formatting.format_tree(formatting.changed_files())
    return 1 if seen or failed else 0
