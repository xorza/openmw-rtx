"""`omw crash <dump> [symbols]`: a player's crash dump, or the package it came in, read against the
symbols of the build that wrote it — every thread, with the function and the line of each frame. No
build is configured for it: reading a dump needs the symbols and Breakpad's walker, and nothing this
tree compiles."""

import json
import subprocess
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from omw import deps
from omw.package import DIST
from omw.system import Refusal, output, run

# What every minidump begins with, and no package does.
MINIDUMP = b"MDMP"


@dataclass(frozen=True)
class Module:
    """The executable a dump was written of: its file, and where a symbols store files it."""

    file: str
    debug_file: str
    debug_id: str

    def stored(self) -> str:
        """The store's folder of this build of it, as `dump_syms --store` lays it out."""
        return f"{self.debug_file}/{self.debug_id}/"

    def __str__(self) -> str:
        return f"{self.file} {self.debug_id}"


def parse_module(read: str) -> Module:
    """The executable `minidump-stackwalk --json` says the dump was written of."""
    walked = json.loads(read)
    index = walked.get("main_module")
    if index is None:
        raise Refusal("the dump names no executable it was written of")
    module = walked["modules"][index]
    # A Windows dump's path read on Linux, and the other way round.
    file = module["filename"].replace("\\", "/").rsplit("/", 1)[-1]
    return Module(file=file, debug_file=module["debug_file"], debug_id=module["debug_id"])


@dataclass(frozen=True)
class Dump:
    """A dump to read: the name it came under, and where it lies now."""

    name: str
    path: Path


def dumps_in(path: Path, folder: Path) -> list[Dump]:
    """**The dumps `path` holds**: itself where it is one, or each dump of the package a player sends,
    written into `folder` under a name of the driver's own. A player's package is untrusted, and on
    Windows a name of its own could still leave the folder (`C:x.dmp`) or open a device (`CON.dmp`)."""
    with path.open("rb") as file:
        if file.read(len(MINIDUMP)) == MINIDUMP:
            return [Dump(path.name, path)]
    if not zipfile.is_zipfile(path):
        raise Refusal(f"{path} is neither a dump nor a crash package")

    dumps: list[Dump] = []
    with zipfile.ZipFile(path) as package:
        for entry in package.infolist():
            name = PurePosixPath(entry.filename.replace("\\", "/")).name
            if entry.is_dir() or not name.endswith(".dmp"):
                continue
            dump = folder / f"dump-{len(dumps)}.dmp"
            dump.write_bytes(package.read(entry))
            dumps.append(Dump(name, dump))
    if not dumps:
        raise Refusal(f"the package {path} holds no dump")
    return dumps


def store_of(module: Module, source: Path, folder: Path) -> Path | None:
    """**A symbols store that holds `module`'s, or none**, from a release's `-symbols.zip`, a folder
    of one, or the build folder that made the executable, whose symbols are made into `folder`. A
    store of another build is none: the walker reads a dump against it and names no frame."""
    if source.is_file():
        with zipfile.ZipFile(source) as zipped:
            if not any(name.startswith(module.stored()) for name in zipped.namelist()):
                return None
            store = folder / source.name
            if not store.is_dir():
                zipped.extractall(store)
            return store
    if (source / module.stored()).is_dir():
        return source
    executable = source / module.file
    if not executable.is_file():
        return None
    # Made once for every dump of a package that one build wrote.
    store = folder / "made"
    if not (store / module.stored()).is_dir():
        run([deps.crash_tool("dump_syms"), "--store", store, executable], stdout=subprocess.DEVNULL)
    return store if (store / module.stored()).is_dir() else None


def symbols_for(module: Module, named: Path | None, folder: Path) -> Path:
    """The store `named` gives for `module`, or the newest of `dist/`'s that holds it, or a refusal
    that says whose symbols the dump needs."""
    if named is not None:
        store = store_of(module, named, folder)
        if store is None:
            raise Refusal(f"{named} holds no symbols of {module}, the build that wrote the dump")
        return store

    made = sorted(DIST.glob("*-symbols.zip"), key=lambda zipped: zipped.stat().st_mtime, reverse=True)
    for symbols in made:
        if (store := store_of(module, symbols, folder)) is not None:
            return store
    raise Refusal(f"no -symbols.zip in dist/ holds {module}, the build that wrote the dump: name its release's "
                  "-symbols.zip, or the build folder that made it (`omw crash <dump> build-release`)")


def read_crash(args: list[str]) -> int:
    if not 1 <= len(args) <= 2:
        raise Refusal("name the dump to read: `omw crash <dump> [symbols]`")
    given = Path(args[0])
    if not given.is_file():
        raise Refusal(f"there is no dump at {given}")
    named = Path(args[1]) if len(args) == 2 else None
    if named is not None and not named.exists():
        raise Refusal(f"there are no symbols at {named}")

    walker = deps.crash_tool("minidump-stackwalk")
    with tempfile.TemporaryDirectory() as scratch:
        folder = Path(scratch)
        dumps = dumps_in(given, folder)
        for dump in dumps:
            store = symbols_for(parse_module(output([walker, "--json", dump.path])), named, folder)
            if len(dumps) > 1:
                print(f"{dump.name}:")
            run([walker, "--human", dump.path, store])
    return 0
