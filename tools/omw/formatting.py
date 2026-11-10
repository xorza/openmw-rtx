"""`omw format [--check] [file...]`: the C++ sources of the working tree through the pinned
clang-format (`deps.clang_format`), rewritten in place — or, with `--check`, left alone and every
difference reported, which is what the gate asks. And `omw merge-file`, git's merge driver for those
sources, which `upstream.yml` configures."""

import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from omw import deps
from omw.system import CXX_SOURCES, ROOT, Refusal, jobs, working_tree_files


def format_tree(args: list[str]) -> int:
    """**Rewrites unless asked to check**, as `cargo fmt` does, so the verb does the work its name
    says; the check is the gate's. **Every C++ source of the tree, or those named**: a named file
    that is no such source, a vendored one or another language's, is left alone, so a caller can
    hand over every file it changed.

    **Every source in the working tree, and not every one git tracks.** `git ls-files` names a file
    a move has deleted and misses one it has created, so a check that walks it fails a tree with an
    uncommitted move on the file that was gone and passes it on the file that was new. **One
    clang-format on every system**, the pinned one: a desk's own, of another version, lays the same
    file out otherwise. **In batches**, because one process for every file leaves nothing to run
    beside it: 8.5 s on one core, and 1 s in batches of 64."""
    check = "--check" in args
    named = [arg for arg in args if arg != "--check"]
    if any(arg.startswith("-") for arg in named):
        raise Refusal("format takes `--check` and the files to format, or nothing")
    mode = ["--dry-run", "-Werror"] if check else ["-i"]
    clang_format = str(deps.clang_format())

    sources = working_tree_files(*CXX_SOURCES)
    if named:
        wanted = {Path(name).as_posix().removeprefix("./") for name in named}
        sources = [source for source in sources if source in wanted]
    batches = [sources[at:at + 64] for at in range(0, len(sources), 64)]

    def formatted(batch: list[str]) -> int:
        return subprocess.run([clang_format, *mode, *batch], cwd=ROOT, check=False).returncode

    with ThreadPoolExecutor(max_workers=jobs()) as pool:
        failed = sum(1 for code in pool.map(formatted, batches) if code != 0)
    return 1 if failed else 0


def formatted_text(clang_format: Path, text: bytes, path: str) -> bytes:
    """`text` laid out as the tree's `path` would be, by `.clang-format` from that path's folder up;
    the text as it was where clang-format refuses it, which `git merge-file` then merges as it is."""
    done = subprocess.run([clang_format, f"--assume-filename={path}"], input=text, cwd=ROOT,
                          capture_output=True, check=False)
    return done.stdout if done.returncode == 0 else text


def merge_file(args: list[str]) -> int:
    """**Git's merge driver for C++** (`merge=clang-format` in `.gitattributes`), as `upstream.yml`
    configures it: `omw merge-file %O %A %B %L %P`. The base and both sides are each laid out by the
    pinned clang-format before `git merge-file` merges them into `%A`, so an upstream edit to a file
    the fork reformatted conflicts only where the edit itself does, as `merge.renormalize` does for
    line endings. The exit status is `git merge-file`'s: nought for a clean merge."""
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

