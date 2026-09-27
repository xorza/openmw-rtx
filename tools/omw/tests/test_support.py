import hashlib
import shutil
import tempfile
import unittest
from pathlib import Path

from omw.build import carried_ngx
from omw.fetch import download, settle
from omw.package import used_osg_plugins
from omw.system import Refusal, environment_key, parse_set_output


class ParseSetOutputTest(unittest.TestCase):
    def test_the_variables_and_not_the_drives(self):
        text = "A=1\r\n=C:=C:\\work\r\nPath=x;y\r\nEMPTY=\r\nno equals sign\r\nB=c=d\r\n"
        self.assertEqual(parse_set_output(text), {
            environment_key("A"): "1", environment_key("Path"): "x;y", environment_key("EMPTY"): "",
            environment_key("B"): "c=d",
        })


class CarriedNgxTest(unittest.TestCase):
    def test_only_the_ngx_entries_that_hold_a_value(self):
        entries = [
            "NGX_ROOT:PATH=/sdk",
            "NGX_LIBRARY:FILEPATH=/sdk/lib/libnvsdk_ngx.a",
            "NGX_INCLUDE_DIR:PATH=NGX_INCLUDE_DIR-NOTFOUND",
            "NGX_EMPTY:STRING=",
            "NGX_FOUND:INTERNAL=1",
            "OTHER:PATH=/elsewhere",
        ]
        self.assertEqual(carried_ngx(entries), ["-DNGX_ROOT:PATH=/sdk", "-DNGX_LIBRARY:FILEPATH=/sdk/lib/libnvsdk_ngx.a"])


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
