import hashlib
import shutil
import tempfile
import unittest
from pathlib import Path

from omw.build import CONFIGURED_FROM, configured_from
from omw.fetch import build_beside, download, partial_of, settle
from omw.package import used_osg_plugins
from omw.system import Refusal, environment_key, parse_set_output


class ParseSetOutputTest(unittest.TestCase):
    def test_the_variables_and_not_the_drives(self):
        text = "A=1\r\n=C:=C:\\work\r\nPath=x;y\r\nEMPTY=\r\nno equals sign\r\nB=c=d\r\n"
        self.assertEqual(parse_set_output(text), {
            environment_key("A"): "1", environment_key("Path"): "x;y", environment_key("EMPTY"): "",
            environment_key("B"): "c=d",
        })


class ConfiguredFromTest(unittest.TestCase):
    def test_a_directory_is_configured_only_with_its_stamp_its_cache_and_its_ninja_file(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        self.assertFalse(configured_from(folder, "abc"), "an empty directory")

        (folder / CONFIGURED_FROM).write_text("abc\n")
        (folder / "CMakeCache.txt").write_text("")
        (folder / "build.ninja").write_text("")
        self.assertTrue(configured_from(folder, "abc"))
        self.assertFalse(configured_from(folder, "abd"), "a preset that has changed since")

        for missing in ("CMakeCache.txt", "build.ninja", CONFIGURED_FROM):
            with self.subTest(missing=missing):
                content = (folder / missing).read_text()
                (folder / missing).unlink()
                self.assertFalse(configured_from(folder, "abc"))
                (folder / missing).write_text(content)


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


class UsedOsgPluginsTest(unittest.TestCase):
    def test_the_names_between_the_set_and_its_parenthesis(self):
        cases = [
            ("x\nset(USED_OSG_PLUGINS\n    osgdb_bmp\n    osgdb_tga)\n\nset(OTHER y)\n", ["osgdb_bmp", "osgdb_tga"]),
            ("set(USED_OSG_PLUGINS osgdb_png osgdb_dds)\n", ["osgdb_png", "osgdb_dds"]),
        ]
        for text, expected in cases:
            with self.subTest(text=text):
                self.assertEqual(used_osg_plugins(text), expected)
        with self.assertRaises(Refusal):
            used_osg_plugins("set(OTHER y)\n")


if __name__ == "__main__":
    unittest.main()
