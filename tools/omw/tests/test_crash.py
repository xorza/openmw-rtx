import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from omw.crash import Dump, Module, dumps_in, parse_module, store_of
from omw.system import Refusal

OPENMW = Module(file="openmw.exe", debug_file="openmw.pdb", debug_id="A1B0E0D6C3904C1FA4157C04BD01FF477")


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
