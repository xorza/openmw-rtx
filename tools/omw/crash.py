"""`omw crash <dump> [symbols]`: a player's crash dump, read against a release's symbols — every
thread, with the function and the line of each frame. No build is configured for it: reading a dump
needs the release's symbols and Breakpad's walker, and nothing this tree compiles."""

import tempfile
import zipfile
from pathlib import Path

from omw import deps
from omw.package import DIST
from omw.system import Refusal, run


def read_crash(args: list[str]) -> int:
    if not 1 <= len(args) <= 2:
        raise Refusal("name the dump to read: `omw crash <dump> [symbols]`")
    dump = Path(args[0])
    if not dump.is_file():
        raise Refusal(f"there is no dump at {dump}")

    # A release's `-symbols.zip` or a folder of one, or else the newest `omw archive` left in dist/.
    if len(args) == 2:
        symbols = Path(args[1])
    else:
        made = sorted(DIST.glob("*-symbols.zip"), key=lambda zipped: zipped.stat().st_mtime)
        if not made:
            raise Refusal("no symbols: name a release's -symbols.zip, or `omw archive` first")
        symbols = made[-1]

    walker = deps.crash_tool("minidump-stackwalk")
    if symbols.is_dir():
        run([walker, "--human", dump, symbols])
        return 0
    with tempfile.TemporaryDirectory() as folder:
        with zipfile.ZipFile(symbols) as zipped:
            zipped.extractall(folder)
        run([walker, "--human", dump, folder])
    return 0
