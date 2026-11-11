import hashlib
import io
import os
import shutil
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

from omw.fetch import build_beside, download, extract_member, partial_of, settle, unpack_stripped
from omw.system import Refusal


class BuildBesideTest(unittest.TestCase):
    def test_a_tree_takes_its_name_only_once_it_is_whole(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        final = folder / "Qt" / "6.8.3"

        def stopped(partial: Path) -> None:
            (partial / "6.8.3" / "msvc2019_64").mkdir(parents=True)
            raise KeyboardInterrupt

        with self.assertRaises(KeyboardInterrupt):
            build_beside(final, stopped, within="6.8.3")
        self.assertFalse(final.exists(), "a tree cut off halfway took the final name")
        self.assertTrue(partial_of(final).exists(), "what the stopped run left is beside the name")

        def whole(partial: Path) -> None:
            (partial / "6.8.3" / "msvc2019_64" / "bin").mkdir(parents=True)

        self.assertEqual(build_beside(final, whole, within="6.8.3"), final)
        self.assertTrue((final / "msvc2019_64" / "bin").is_dir(), "the inner path took the name")
        self.assertFalse(partial_of(final).exists(), "the partial was left behind")

        flat = folder / "clang-format-14"
        build_beside(flat, lambda partial: (partial.mkdir(), (partial / "clang-format.exe").write_text("x")))
        self.assertEqual((flat / "clang-format.exe").read_text(), "x")


class SettleTest(unittest.TestCase):
    def test_a_file_takes_its_name_only_once_its_digest_passes(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        partial, file = folder / "f.partial", folder / "f"
        partial.write_bytes(b"content")
        right = hashlib.sha256(b"content").hexdigest()

        with self.assertRaises(Refusal):
            settle(partial, file, "https://x", sha256="0" * 64)
        self.assertFalse(partial.exists())
        self.assertFalse(file.exists())

        partial.write_bytes(b"content")
        settle(partial, file, "https://x", sha256=right, sha512=hashlib.sha512(b"content").hexdigest())
        self.assertFalse(partial.exists())
        self.assertEqual(file.read_bytes(), b"content")

    def test_a_download_that_is_not_https_is_refused_before_it_starts(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        with self.assertRaises(Refusal):
            download("http://example.com/f", folder / "f")
        self.assertEqual(list(folder.iterdir()), [])


class ExtractMemberTest(unittest.TestCase):
    def test_the_member_is_found_wherever_it_is_and_stands_whole_with_no_partial_beside_it(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        archive = folder / "tools.zip"
        with zipfile.ZipFile(archive, "w") as made:
            made.writestr("bin/other", b"no")
            # A folder of the tool's name, ahead of it, is no member to extract.
            made.writestr("docs/dump_syms/", b"")
            made.writestr("release/bin/dump_syms", b"the tool")

        into = folder / "out"
        self.assertEqual(extract_member(archive, "dump_syms", into), into / "dump_syms")
        self.assertEqual((into / "dump_syms").read_bytes(), b"the tool")
        self.assertEqual(sorted(path.name for path in into.iterdir()), ["dump_syms"])
        with self.assertRaises(Refusal):
            extract_member(archive, "minidump-stackwalk", into)



class UnpackStrippedTest(unittest.TestCase):
    def test_the_top_folder_goes_from_every_member_and_from_a_hard_link_s_target(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        archive = folder / "llvm.tar.gz"
        with tarfile.open(archive, "w:gz") as tarred:
            for name, data in (("llvm-23/bin/clang", b"ELF"), ("llvm-23/lib/clang/23/include/stddef.h", b"#")):
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                tarred.addfile(entry, io.BytesIO(data))
            hard = tarfile.TarInfo("llvm-23/bin/clang-23")
            hard.type = tarfile.LNKTYPE
            hard.linkname = "llvm-23/bin/clang"
            tarred.addfile(hard)
            soft = tarfile.TarInfo("llvm-23/bin/clang++")
            soft.type = tarfile.SYMTYPE
            soft.linkname = "clang"
            tarred.addfile(soft)
            tarred.addfile(tarfile.TarInfo("llvm-23"))

        into = folder / "llvm"
        unpack_stripped(archive, into)
        self.assertEqual(sorted(path.relative_to(into).as_posix() for path in into.rglob("*") if not path.is_dir()),
                         ["bin/clang", "bin/clang++", "bin/clang-23", "lib/clang/23/include/stddef.h"])
        self.assertEqual((into / "bin" / "clang-23").read_bytes(), b"ELF")
        # Windows makes a symbolic link only with a privilege a desk's shell lacks, and copies its target.
        if os.name != "nt":
            self.assertEqual(os.readlink(into / "bin" / "clang++"), "clang")
        self.assertEqual((into / "bin" / "clang++").read_bytes(), b"ELF")


if __name__ == "__main__":
    unittest.main()
