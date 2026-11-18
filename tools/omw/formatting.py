"""`omw format [--check] [file...]`: the working tree's C++ through the tree's spellings (`normalize`) and the pinned
clang-format, and its CMake through the pinned gersemi, rewritten, or with `--check` every difference reported; and
`omw merge-file`, git's merge driver for the C++, which lays out each side the same way."""

import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from omw import deps, normalize
from omw.system import CMAKE_SOURCES, CXX_SOURCES, ROOT, Refusal, jobs, output, working_tree_files


def format_tree(args: list[str]) -> int:
    """**Rewrites unless asked to check**, as `cargo fmt` does. Every C++ source and CMake file of the working tree,
    or those of them named, so a caller can hand over every file it changed. **In batches of 64** on every core:
    8.5 s a file at a time, 1 s so."""
    check = "--check" in args
    named = [arg for arg in args if arg != "--check"]
    if any(arg.startswith("-") for arg in named):
        raise Refusal("format takes `--check` and the files to format, or nothing")
    mode = ["--dry-run", "-Werror"] if check else ["-i"]
    clang_format = str(deps.clang_format())

    sources = working_tree_files(*CXX_SOURCES)
    cmake = working_tree_files(*CMAKE_SOURCES)
    if named:
        wanted = {Path(name).as_posix().removeprefix("./") for name in named}
        sources = [source for source in sources if source in wanted]
        cmake = [file for file in cmake if file in wanted]
    failed = _normalize(sources, check)
    batches = [sources[at:at + 64] for at in range(0, len(sources), 64)]

    def formatted(batch: list[str]) -> int:
        return subprocess.run([clang_format, *mode, *batch], cwd=ROOT, check=False).returncode

    with ThreadPoolExecutor(max_workers=jobs()) as pool:
        failed += sum(1 for code in pool.map(formatted, batches) if code != 0)
    # gersemi spreads every file over its own workers; without its cache, which is state outside the tree.
    if cmake:
        cmake_mode = ["--check", "--diff"] if check else ["--in-place"]
        failed += subprocess.run([*deps.gersemi(), *cmake_mode, "--no-cache", "--quiet", *cmake], cwd=ROOT,
                                 check=False).returncode != 0
    return 1 if failed else 0


def _read(path: Path) -> str:
    """A source's text, byte for byte whatever its encoding, so that writing it back changes nothing the rules did
    not."""
    return path.read_bytes().decode("utf-8", errors="surrogateescape")


def _normalize(sources: list[str], check: bool) -> int:
    """**The tree's spellings ahead of clang-format**, which orders the includes they add: each source rewritten, or
    with `check` each one they would change named. The count of sources that fail."""
    if not normalize.ENABLED:
        return 0
    exists = normalize.Exists()
    failed = 0
    for name in filter(normalize.applies, sources):
        path = ROOT / name
        text = _read(path)
        try:
            rewritten = normalize.normalized(name, text, exists=exists)
        except normalize.Unnormalizable as refusal:
            print(refusal)
            failed += 1
            continue
        if rewritten == text:
            continue
        if check:
            print(f"{name}: not in the tree's spellings; run `omw format`")
            failed += 1
        else:
            path.write_bytes(rewritten.encode("utf-8", errors="surrogateescape"))
    return failed


def changed_files(root: Path = ROOT) -> list[str]:
    """**The files the working tree changed against `HEAD`**, staged or not, and the new ones git does not ignore,
    from the root, `/`-separated; a deleted file is none."""
    named = {*output(["git", "-C", root, "diff", "--name-only", "HEAD"]).splitlines(),
             *output(["git", "-C", root, "ls-files", "--others", "--exclude-standard"]).splitlines()}
    return sorted(name for name in named if (root / name).is_file())


def formatted_text(clang_format: Path, text: bytes, path: str) -> bytes:
    """`text` laid out as the tree's `path` would be, its spellings and then clang-format's; as it was where either
    refuses it, to be merged as is."""
    decoded = text.decode("utf-8", errors="surrogateescape")
    try:
        text = normalize.normalized(path, decoded).encode("utf-8", errors="surrogateescape")
    except normalize.Unnormalizable:
        return text
    done = subprocess.run([clang_format, f"--assume-filename={path}"], input=text, cwd=ROOT,
                          capture_output=True, check=False)
    return done.stdout if done.returncode == 0 else text


def merge_file(args: list[str]) -> int:
    """**Git's merge driver for C++** (`merge=clang-format`), `omw merge-file %O %A %B %L %P`: the base and both
    sides laid out by the tree's spellings and the pinned clang-format before `git merge-file` merges them into `%A`,
    so an upstream edit to a
    file the fork reformatted conflicts only where the edit does. The status is `git merge-file`'s."""
    if len(args) != 5:
        raise Refusal("merge-file takes the base, ours, theirs, the marker size and the path: %O %A %B %L %P")
    base, ours, theirs, marker_size, path = args
    clang_format = deps.clang_format()
    for side in (base, ours, theirs):
        file = Path(side)
        file.write_bytes(formatted_text(clang_format, file.read_bytes(), path))
    done = subprocess.run(["git", "merge-file", f"--marker-size={marker_size}", "-L", f"{path} (ours)", "-L",
                           f"{path} (base)", "-L", f"{path} (theirs)", ours, base, theirs], cwd=ROOT, check=False)
    return done.returncode

