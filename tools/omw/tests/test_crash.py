import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from omw.crash import (
    CRASHPAD_INFO_STREAM,
    NOTE_SLOT_SIZE,
    NOTE_SLOTS_AT,
    SUMMARY_STREAM,
    Dump,
    Module,
    describe_dump,
    dumps_in,
    parse_module,
    store_of,
)
from omw.system import Refusal

OPENMW = Module(file="openmw.exe", debug_file="openmw.pdb", debug_id="A1B0E0D6C3904C1FA4157C04BD01FF477")


def note_table(reason: str, refused: int, notes: list[tuple[int, int, str]]) -> bytes:
    """A note table as `crashnote.cpp` lays it out, of 64 slots: `notes` is (thread, sequence, text)."""
    table = bytearray(NOTE_SLOTS_AT + 64 * NOTE_SLOT_SIZE)
    table[0:len(reason)] = reason.encode()
    table[256:260] = refused.to_bytes(4, "little")
    for slot, (thread, sequence, text) in enumerate(notes):
        at = NOTE_SLOTS_AT + slot * NOTE_SLOT_SIZE
        table[at:at + 8] = thread.to_bytes(8, "little")
        table[at + 8:at + 12] = sequence.to_bytes(4, "little")
        table[at + 12:at + 12 + len(text)] = text.encode()
    return bytes(table)


def minidump(summary: str, simple: list[tuple[str, str]], objects: dict[str, bytes]) -> bytes:
    """A minidump of the two streams `describe_dump` reads, laid out as `minidump_extensions.h` has
    them: the summary, and Crashpad's info with one module's annotations."""
    data = bytearray(32)
    words = lambda *values: b"".join(value.to_bytes(4, "little") for value in values)

    def put(blob: bytes) -> int:
        at = len(data)
        data.extend(blob)
        data.extend(b"\0" * (-len(data) % 4))
        return at

    def text(value: str) -> int:
        return put(words(len(value.encode())) + value.encode() + b"\0")

    entries = b"".join(words(text(key), text(value)) for key, value in simple)
    dictionary = put(words(len(simple)) + entries)
    listed = b"".join(words(text(name)) + (0).to_bytes(4, "little") + words(put(words(len(value)) + value))
                      for name, value in objects.items())
    annotations = put(words(len(objects)) + listed)
    module = put(words(1, 0, 0, len(entries) + 4, dictionary, len(listed) + 4, annotations))
    modules = put(words(1, 0, 20, module))
    info = put(words(1) + bytes(32) + words(0, 0, 16, modules, 0) + bytes(8))
    text_at = put(summary.encode())
    directory = put(words(SUMMARY_STREAM, len(summary.encode()), text_at, CRASHPAD_INFO_STREAM, 60, info))
    data[0:16] = b"MDMP" + words(0xA793, 2, directory)
    return bytes(data)


class CrashTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)

    def test_the_main_module_is_read_whichever_system_wrote_its_path(self):
        modules = [{"filename": "C:\\Windows\\System32\\ntdll.dll", "debug_file": "ntdll.pdb", "debug_id": "1"},
                   {"filename": "C:\\Games\\OpenMW\\openmw.exe", "debug_file": "openmw.pdb",
                    "debug_id": OPENMW.debug_id}]
        self.assertEqual(parse_module(json.dumps({"main_module": 1, "modules": modules})), OPENMW)
        linux = {"filename": "/opt/openmw/openmw", "debug_file": "openmw", "debug_id": "B2"}
        self.assertEqual(parse_module(json.dumps({"main_module": 0, "modules": [linux]})),
                         Module(file="openmw", debug_file="openmw", debug_id="B2"))
        self.assertEqual(OPENMW.stored(), "openmw.pdb/A1B0E0D6C3904C1FA4157C04BD01FF477/")
        with self.assertRaises(Refusal):
            parse_module(json.dumps({"main_module": None, "modules": []}))

    def test_a_dump_is_itself_and_a_package_gives_its_dumps_under_names_of_the_drivers_own(self):
        out = self.root / "out"
        out.mkdir()
        dump = self.root / "a.dmp"
        dump.write_bytes(b"MDMP" + b"\0" * 28)
        self.assertEqual(dumps_in(dump, out), [Dump("a.dmp", dump)])

        package = self.root / "OpenMW-crash.zip"
        with zipfile.ZipFile(package, "w") as zipped:
            zipped.writestr("openmw.log", "Hang: no frame for 20 seconds")
            zipped.writestr("1.dmp", b"MDMP one")
            zipped.writestr("../../elsewhere/2.dmp", b"MDMP two")
            zipped.writestr("C:3.dmp", b"MDMP three")
            zipped.writestr("CON.dmp", b"MDMP four")
        # A path, a drive and a device on Windows: no name of the package's reaches the folder.
        named = ["1.dmp", "2.dmp", "C:3.dmp", "CON.dmp"]
        self.assertEqual(dumps_in(package, out), [Dump(name, out / f"dump-{i}.dmp") for i, name in enumerate(named)])
        self.assertEqual((out / "dump-1.dmp").read_bytes(), b"MDMP two")
        self.assertEqual(sorted(path.name for path in out.iterdir()), [f"dump-{i}.dmp" for i in range(4)])
        self.assertFalse((self.root / "elsewhere").exists(), "a package wrote outside the folder")

        empty = self.root / "empty.zip"
        with zipfile.ZipFile(empty, "w") as zipped:
            zipped.writestr("openmw.log", "")
        neither = self.root / "neither.txt"
        neither.write_text("a log")
        for refused in (empty, neither):
            with self.assertRaises(Refusal, msg=refused.name):
                dumps_in(refused, out)

    def test_symbols_are_found_only_where_they_are_the_dumps_builds(self):
        out = self.root / "out"
        out.mkdir()
        release = self.root / "openmw-0.52-windows-symbols.zip"
        other = self.root / "openmw-0.51-windows-symbols.zip"
        for zipped, debug_id in ((release, OPENMW.debug_id), (other, "FFFF")):
            with zipfile.ZipFile(zipped, "w") as writing:
                writing.writestr(f"openmw.pdb/{debug_id}/openmw.sym", "MODULE windows x86_64")
        self.assertEqual(store_of(OPENMW, release, out), out / release.name)
        self.assertTrue((out / release.name / OPENMW.stored() / "openmw.sym").is_file())
        self.assertIsNone(store_of(OPENMW, other, out))

        store = self.root / "store"
        (store / OPENMW.stored()).mkdir(parents=True)
        self.assertEqual(store_of(OPENMW, store, out), store)
        # A folder with neither the store's layout nor the executable: no `dump_syms` is run.
        self.assertIsNone(store_of(OPENMW, self.root / "out", out))

    def test_a_dump_alone_says_what_the_log_said(self):
        table = note_table("std::terminate", 3, [(77, 2, "drawing frame 9"), (78, 5, "walking"), (0, 0, "")])
        data = minidump("Crash: SIGABRT in thread 77\nCrash: dump 1.dmp", [("version", "0.53")],
                        {"openmw-notes": table, "other": b"x"})
        self.assertEqual(describe_dump(data), [
            "summary:",
            "  Crash: SIGABRT in thread 77",
            "  Crash: dump 1.dmp",
            "annotations:",
            "  version: 0.53",
            "notes:",
            "  reason: std::terminate",
            "  note of thread 77: drawing frame 9",
            "  note of thread 78: walking (half written)",
            "  3 threads found the note table full",
        ])

        # A dump the monitor could not summarise, nor annotate, says so line by line.
        self.assertEqual(describe_dump(minidump("", [], {})), [
            "summary:", "  (none)", "annotations:", "  (none)", "notes:", "  (none)",
        ])
        # A table of another layout is named, not read.
        self.assertEqual(describe_dump(minidump("", [], {"openmw-notes": b"short"}))[-1],
                         "  a note table of 5 bytes, which this driver does not read")
