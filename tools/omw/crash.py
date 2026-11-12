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

# The streams the monitor's facts travel in: its summary, `Crash::sSummaryStream`, and Crashpad's own
# info, which holds the annotations (`minidump_extensions.h`).
SUMMARY_STREAM = 0x4F4D5701
CRASHPAD_INFO_STREAM = 0x43500001

# The note table's annotation, `Crash::sNotesAnnotation`, laid out as `crashnote.cpp`'s `Table`, whose
# static asserts hold these numbers: the reason, the count of threads refused, then each slot's
# thread, sequence and text.
NOTES_ANNOTATION = "openmw-notes"
NOTE_CAPACITY = 256
NOTE_SLOTS_AT = 264
NOTE_SLOT_SIZE = 272


class Minidump:
    """**The bytes of a minidump, read where a field says**: a dump is a player's, so every offset is
    checked against its size, and one that leaves it reads as nothing."""

    def __init__(self, data: bytes):
        self.data = data

    def word(self, at: int, size: int = 4) -> int:
        if at < 0 or at + size > len(self.data):
            raise Refusal("the dump is cut short")
        return int.from_bytes(self.data[at:at + size], "little")

    def located(self, at: int) -> bytes:
        """The bytes a `MINIDUMP_LOCATION_DESCRIPTOR` at `at` names."""
        size, rva = self.word(at), self.word(at + 4)
        if rva + size > len(self.data):
            raise Refusal("the dump is cut short")
        return self.data[rva:rva + size]

    def text(self, rva: int) -> str:
        """A `MinidumpUTF8String`."""
        length = self.word(rva)
        return self.data[rva + 4:rva + 4 + length].decode("utf-8", "replace")

    def byte_array(self, rva: int) -> bytes:
        """A `MinidumpByteArray`."""
        length = self.word(rva)
        return self.data[rva + 4:rva + 4 + length]

    def streams(self) -> dict[int, int]:
        """Where each stream's location descriptor lies, by its type."""
        count, directory = self.word(8), self.word(12)
        return {self.word(directory + i * 12): directory + i * 12 + 4 for i in range(count)}

    def summary(self) -> list[str]:
        """The monitor's summary, a line each, or nothing where the dump carries none."""
        at = self.streams().get(SUMMARY_STREAM)
        return [] if at is None else self.located(at).decode("utf-8", "replace").splitlines()

    def annotations(self) -> tuple[list[tuple[str, str]], dict[str, bytes]]:
        """The simple annotations of the process and of each module, and each module's annotation
        objects by name."""
        simple: list[tuple[str, str]] = []
        objects: dict[str, bytes] = {}
        at = self.streams().get(CRASHPAD_INFO_STREAM)
        if at is None:
            return simple, objects
        info = self.word(at + 4)

        def dictionary(location: int) -> None:
            if self.word(location) == 0:
                return
            rva = self.word(location + 4)
            for i in range(self.word(rva)):
                entry = rva + 4 + i * 8
                simple.append((self.text(self.word(entry)), self.text(self.word(entry + 4))))

        # `MinidumpCrashpadInfo`: a version and two UUIDs, then the process's annotations and the
        # modules' list.
        dictionary(info + 36)
        if self.word(info + 44) != 0:
            modules = self.word(info + 48)
            for i in range(self.word(modules)):
                # `MinidumpModuleCrashpadInfoLink`: an index, then where the module's info is.
                module = self.word(modules + 4 + i * 12 + 8)
                dictionary(module + 12)
                if self.word(module + 20) != 0:
                    listed = self.word(module + 24)
                    for j in range(self.word(listed)):
                        entry = listed + 4 + j * 12
                        objects[self.text(self.word(entry))] = self.byte_array(self.word(entry + 8))
        return simple, objects


def describe_notes(table: bytes) -> list[str]:
    """The note table as the monitor reads it: the reason, each thread's note, and the threads that
    found it full."""
    if len(table) < NOTE_SLOTS_AT or (len(table) - NOTE_SLOTS_AT) % NOTE_SLOT_SIZE != 0:
        return [f"a note table of {len(table)} bytes, which this driver does not read"]

    def text(at: int) -> str:
        return table[at:at + NOTE_CAPACITY].split(b"\0", 1)[0].decode("utf-8", "replace")

    lines = []
    if reason := text(0):
        lines.append(f"reason: {reason}")
    for at in range(NOTE_SLOTS_AT, len(table), NOTE_SLOT_SIZE):
        thread = int.from_bytes(table[at:at + 8], "little")
        whole = int.from_bytes(table[at + 8:at + 12], "little") % 2 == 0
        note = text(at + 12)
        if thread != 0 and (note or not whole):
            lines.append(f"note of thread {thread}: {note}" + ("" if whole else " (half written)"))
    if refused := int.from_bytes(table[NOTE_CAPACITY:NOTE_CAPACITY + 4], "little"):
        lines.append(f"{refused} threads found the note table full")
    return lines


def describe_dump(data: bytes) -> list[str]:
    """**What the log said of a dump**, out of the dump alone, for a player who sent only that: the
    monitor's summary, the annotations, and the note table."""
    dump = Minidump(data)
    lines = ["summary:"] + [f"  {line}" for line in dump.summary() or ["(none)"]]
    simple, objects = dump.annotations()
    lines += ["annotations:"] + ([f"  {key}: {value}" for key, value in simple] or ["  (none)"])
    table = objects.get(NOTES_ANNOTATION)
    lines += ["notes:"] + [f"  {line}" for line in (describe_notes(table) if table is not None else ["(none)"])]
    return lines


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
            print("\n".join(describe_dump(dump.path.read_bytes())))
            run([walker, "--human", dump.path, store])
    return 0
