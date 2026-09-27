"""`omw format`: every C++ source in the working tree through the clang-format CI pins."""

import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor

from omw import deps
from omw.system import ROOT, WINDOWS, Refusal, jobs, output


def check_format(args: list[str]) -> int:
    """**Every source in the working tree, and not every one git tracks.** CI's own script walks
    `git ls-files`, which names a file a move has deleted and misses one it has created — so a tree
    with an uncommitted move failed it on the file that was gone and passed it on the file that was
    new. CI pins clang-format 14; the desk's later ones disagree with it, and Windows has the one
    `deps.windows_clang_format` takes out of LLVM's package. **In batches**, because one process for
    every file leaves nothing to run beside it: 8.5 s on one core, and 1 s in batches of 64."""
    if args:
        raise Refusal("format takes no arguments")
    found = str(deps.windows_clang_format()) if WINDOWS else shutil.which("clang-format-14")
    if found is None:
        raise Refusal("clang-format-14 is not on the PATH, and CI pins that version")
    clang_format: str = found

    listed = output(["git", "-C", ROOT, "ls-files", "--cached", "--others", "--exclude-standard", "--",
                     ":(exclude)extern/", "*.cpp", "*.hpp", "*.h"]).splitlines()
    files = [name for name in listed if (ROOT / name).is_file()]
    batches = [files[at:at + 64] for at in range(0, len(files), 64)]

    def checked(batch: list[str]) -> int:
        return subprocess.run([clang_format, "--dry-run", "-Werror", *batch], cwd=ROOT).returncode

    with ThreadPoolExecutor(max_workers=jobs()) as pool:
        failed = sum(1 for code in pool.map(checked, batches) if code != 0)
    return 1 if failed else 0
