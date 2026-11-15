import os
import shutil
import tempfile
import unittest
from pathlib import Path

from omw import tidycache
from omw.tidycache import ABSENT, Compile, Digests, Remembered

# Written well before any run under test starts: 2001-09-09.
BEFORE_NS = 1_000_000_000 * 1_000_000_000


class FolderTest(unittest.TestCase):
    def setUp(self) -> None:
        self.folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.folder)

    def write(self, name: str, text: str, modified_ns: int = BEFORE_NS) -> str:
        path = self.folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        os.utime(path, ns=(modified_ns, modified_ns))
        return str(path)


class DependenciesTest(unittest.TestCase):
    def test_every_prerequisite_made_plain_and_absolute_and_the_target_left_out(self):
        directory = Path("/build/debug").resolve()
        text = ("a.o: /src/a.cpp /src/a.hpp \\\n"
                "  ../../src/x/../b.hpp /src/with\\ space.hpp /src/hash\\#.hpp /src/dollar$$.hpp \\\n"
                "  /usr/include/c++/v1/vector\n")
        # Absolute as the system reads a rooted path: on Windows, on the drive of the folder it is named from, which
        # here is the working folder's.
        self.assertEqual(tidycache.dependencies(text, directory),
                         [os.path.abspath(path) for path in (
                             "/src/a.cpp", "/src/a.hpp", directory / "../../src/b.hpp", "/src/with space.hpp",
                             "/src/hash#.hpp", "/src/dollar$.hpp", "/usr/include/c++/v1/vector")])
        # A target a drive's colon stands in, and Windows' line ends.
        self.assertEqual(tidycache.dependencies("C:\\b\\a.obj: a.cpp \\\r\n  h.h\r\n", directory),
                         [os.path.normpath(directory / "a.cpp"), os.path.normpath(directory / "h.h")])
        self.assertEqual(tidycache.dependencies("a.o:\n", directory), [])
        self.assertEqual(tidycache.dependencies("", directory), [], "no rule names nothing")


class ConfigurationsTest(FolderTest):
    def test_every_folder_above_every_read_up_to_the_root_once(self):
        a = str(self.folder / "x" / "y" / "a.hpp")
        b = str(self.folder / "x" / "b.hpp")
        found = tidycache.configurations([a, b])
        above = [self.folder / "x" / "y", self.folder / "x", *self.folder.parents, self.folder]
        self.assertEqual(found, {str(folder / ".clang-tidy") for folder in above})
        self.assertEqual(tidycache.configurations([]), set())


class KeyTest(FolderTest):
    def test_each_input_moves_the_key_and_the_order_of_the_reads_does_not(self):
        a = self.write("a.hpp", "a")
        b = self.write("b.hpp", "b")
        missing = str(self.folder / ".clang-tidy")
        compile = Compile("/build", "c++ -c a.cpp")
        digests = Digests()
        digests.fill([a, b, missing])
        self.assertEqual(digests[missing], ABSENT)
        # SHA-256 of the one byte "a".
        self.assertEqual(digests[a], "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb")

        base = tidycache.key(compile, [a, b, missing], digests)
        self.assertEqual(tidycache.key(compile, [missing, b, a], digests), base)
        self.assertNotEqual(tidycache.key(compile, [a, missing], digests), base, "a read left out")
        self.assertNotEqual(tidycache.key(Compile("/other", compile.command), [a, b, missing], digests), base)
        self.assertNotEqual(tidycache.key(Compile(compile.directory, "c++ -O2 -c a.cpp"), [a, b, missing], digests),
                            base)

        self.write("b.hpp", "B")
        edited = Digests()
        edited.fill([a, b, missing])
        self.assertNotEqual(tidycache.key(compile, [a, b, missing], edited), base, "a read edited")
        self.write("b.hpp", "b")
        self.write(".clang-tidy", "Checks: '-*'")
        stood = Digests()
        stood.fill([a, b, missing])
        self.assertNotEqual(tidycache.key(compile, [a, b, missing], stood), base, "a configuration that came to stand")
        Path(missing).unlink()
        gone = Digests()
        gone.fill([a, b, missing])
        self.assertEqual(tidycache.key(compile, [a, b, missing], gone), base, "the same files, the same key")

    def test_a_read_the_dependency_file_may_miss_by_either_driver_s_spelling(self):
        for command in ("clang++ -Xclang -include-pch -Xclang cmake_pch.hxx.pch -c a.cpp",
                        "cl.exe /Yucmake_pch.hxx /c a.cpp", "clang-cl -Yupch.h a.cpp", "clang++ @flags.rsp",
                        "clang++ -ivfsoverlay o.yaml -c a.cpp", "clang++ --config=c.cfg a.cpp",
                        "clang++ -fmodule-map-file=m.modulemap a.cpp", "clang++ -fsanitize-ignorelist=i.txt a.cpp"):
            with self.subTest(command=command):
                self.assertTrue(Compile("/b", command).reads_unlisted)
        # A forced include is named; a flag's letters inside a path or another flag are no flag.
        self.assertFalse(Compile("/b", "clang++ -include config.h -I/a/Yuki -isystem /usr/include -DX=@ -c a.cpp")
                         .reads_unlisted)
        self.assertFalse(Compile("/b", "cl.exe /FIforced.h /c a.cpp").reads_unlisted)

    def test_the_dependency_file_by_the_driver_s_mode_which_the_program_s_name_says(self):
        into = Path("/scratch/0.d")
        gcc = [f"--extra-arg=-Wp,-MD,{into}"]
        msvc = [f"--extra-arg=/clang:-Wp,-MD,{into}"]
        for command, wanted in (("/usr/bin/clang++ -c a.cpp", gcc), ("c++ -c a.cpp", gcc), ("g++ -c a.cpp", gcc),
                                ("C:\\VS\\bin\\cl.exe /c a.cpp", msvc),
                                ('"C:\\Program Files\\VS\\CL.EXE" /c a.cpp', msvc),
                                ("C:/LLVM/bin/clang-cl.exe /c a.cpp", msvc), ("clang --driver-mode=cl /c a.cpp", msvc),
                                ("/opt/cl/bin/clang++ -c a.cpp", gcc)):
            with self.subTest(command=command):
                self.assertEqual(Compile("/b", command).extra_args(into), wanted)

    def test_an_msvc_flag_clang_cl_does_not_take_is_said_as_clang_says_it(self):
        into = Path("/scratch/0.d")
        written = f"--extra-arg=/clang:-Wp,-MD,{into}"
        self.assertEqual(Compile("/b", "cl.exe /O2 /arch:SSE4.2 /c a.cpp").extra_args(into),
                         [written, "--extra-arg=/clang:-march=x86-64-v2"])
        # Another `/arch:`, which clang-cl takes, and one GCC's driver is given, which is not MSVC's: as they are.
        self.assertEqual(Compile("/b", "cl.exe /arch:AVX2 /c a.cpp").extra_args(into), [written])
        self.assertEqual(Compile("/b", "clang++ /arch:SSE4.2 -c a.cpp").extra_args(into),
                         [f"--extra-arg=-Wp,-MD,{into}"])


class StoreTest(FolderTest):
    def test_what_is_saved_loads_as_it_was_and_another_layout_loads_as_nothing(self):
        store = self.folder / "tidy-clean.json"
        self.assertEqual(tidycache.load(store), {}, "no store is no clean run")
        held = {"/s/b.cpp": Remembered("k2", ("/s/b.cpp", "/s/shared.hpp")),
                "/s/a.cpp": Remembered("k1", ("/s/a.cpp", "/s/shared.hpp"))}
        tidycache.save(store, held)
        self.assertEqual(tidycache.load(store), held)
        self.assertEqual(store.read_text(encoding="utf-8").count("/s/shared.hpp"), 1, "a path written once")
        self.assertFalse(store.with_name(store.name + ".new").exists())
        tidycache.save(store, {})
        self.assertEqual(tidycache.load(store), {})

        store.write_text('{"format": 0, "files": [], "sources": {"/s/a.cpp": {"key": "k", "reads": []}}}',
                         encoding="utf-8")
        self.assertEqual(tidycache.load(store), {})


class RememberTest(FolderTest):
    def test_a_clean_run_stands_until_a_read_changes_and_a_read_modified_since_the_start_keeps_it_out(self):
        source = self.write("src/a.cpp", "int a;")
        header = self.write("src/a.hpp", "#pragma once")
        other = self.write("src/b.cpp", "int b;")
        late = self.write("src/late.hpp", "#pragma once", modified_ns=2 * BEFORE_NS)
        compiles = {source: Compile("/build", "c++ -c a.cpp"), other: Compile("/build", "c++ -c b.cpp")}

        remembered = tidycache.remember(compiles, {source: [header], other: [late]}, started_ns=2 * BEFORE_NS)
        self.assertEqual(set(remembered), {source}, "late.hpp was written as the run started")
        reads = remembered[source].reads
        self.assertIn(source, reads)
        self.assertIn(header, reads)
        self.assertIn(str(self.folder / "src" / ".clang-tidy"), reads)
        self.assertEqual(list(reads), sorted(set(reads)))

        self.assertEqual(tidycache.unchanged(compiles, remembered), {source})
        # A source asked of no more is not answered.
        self.assertEqual(tidycache.unchanged({other: compiles[other]}, remembered), set())
        self.assertEqual(tidycache.unchanged({source: Compile("/build", "c++ -O3 -c a.cpp")}, remembered), set())

        self.write("src/a.hpp", "#pragma once // edited")
        self.assertEqual(tidycache.unchanged(compiles, remembered), set())
        self.write("src/a.hpp", "#pragma once")
        self.assertEqual(tidycache.unchanged(compiles, remembered), {source})
        self.write("src/.clang-tidy", "Checks: '-*'")
        self.assertEqual(tidycache.unchanged(compiles, remembered), set())

    def test_a_read_copied_in_with_its_old_modification_time_after_the_start_keeps_its_source_out(self):
        started = self.folder / "started"
        started.touch()
        started_ns = started.stat().st_mtime_ns
        # Written after the start, its modification time set to 2001 as `cp -p` would: its change time is now.
        source = self.write("a.cpp", "int a;")
        compiles = {source: Compile("/build", "c++ -c a.cpp")}
        self.assertEqual(tidycache.remember(compiles, {source: []}, started_ns), {})
        self.assertEqual(set(tidycache.remember(compiles, {source: []}, started_ns + 10**18)), {source})

    def test_a_read_gone_by_the_time_its_run_is_remembered_keeps_its_source_out(self):
        source = self.write("a.cpp", "int a;")
        compiles = {source: Compile("/build", "c++ -c a.cpp")}
        # A configuration that does not stand is a read like any other, and keeps nothing out.
        self.assertEqual(set(tidycache.remember(compiles, {source: []}, started_ns=2 * BEFORE_NS)), {source})
        self.assertEqual(tidycache.remember(compiles, {source: [str(self.folder / "gone.hpp")]}, 2 * BEFORE_NS), {})


if __name__ == "__main__":
    unittest.main()
