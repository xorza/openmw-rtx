import io
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw import deps, depsset
from omw.pins import Pin
from omw.system import Refusal


def _repository(files: dict[str, bytes]) -> Path:
    root = Path(tempfile.mkdtemp())
    for name, text in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(text)
    subprocess.run(["git", "init", "-q", str(root)], check=True)
    return root


class SetDigestTest(unittest.TestCase):
    def test_every_input_names_another_set_and_nothing_else_does(self):
        root = _repository({"vcpkg.json": b"{}\n", "vcpkg-configuration.json": b"{}\n",
                            "vcpkg/triplets/x.cmake": b"set(A 1)\n", "CI/install_almalinux_deps.sh": b"dnf\n",
                            "README.md": b"text\n"})
        self.addCleanup(shutil.rmtree, root)
        first = depsset.set_digest(root)
        self.assertEqual(len(first), 16)
        self.assertEqual(depsset.set_digest(root), first, "the same inputs named another set")

        # A file outside the inputs moves nothing.
        (root / "README.md").write_bytes(b"other\n")
        self.assertEqual(depsset.set_digest(root), first)
        # A Windows clone's line endings are the same set.
        (root / "vcpkg.json").write_bytes(b"{}\r\n")
        self.assertEqual(depsset.set_digest(root), first)

        seen = {first}
        for name in ("vcpkg.json", "vcpkg-configuration.json", "vcpkg/triplets/x.cmake",
                     "CI/install_almalinux_deps.sh"):
            (root / name).write_bytes((root / name).read_bytes() + b"# moved\n")
            moved = depsset.set_digest(root)
            self.assertNotIn(moved, seen, f"{name} moved and the set kept its name")
            seen.add(moved)
        # A new overlay port is an input too.
        (root / "vcpkg/ports/p/vcpkg.json").parent.mkdir(parents=True)
        (root / "vcpkg/ports/p/vcpkg.json").write_bytes(b"{}\n")
        self.assertNotIn(depsset.set_digest(root), seen)

        with mock.patch.object(depsset.pins, "VCPKG_REGISTRY", Pin("https://example.com/r", "e" * 64)):
            self.assertNotIn(depsset.set_digest(root), seen, "another registry kept the set's name")


class ContainerTest(unittest.TestCase):
    def test_linux_builds_its_set_in_almalinux_8_with_gcc_toolset_15(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)

        def checked(release: str, path: str) -> None:
            (folder / "os-release").write_text(release)
            with (mock.patch.object(depsset, "SYSTEM", "linux"),
                  mock.patch.object(depsset, "OS_RELEASE", folder / "os-release"),
                  mock.patch.dict(os.environ, {"PATH": path})):
                depsset.check_container()

        almalinux = 'ID="almalinux"\nVERSION_ID="8.10"\n'
        checked(almalinux, "/opt/rh/gcc-toolset-15/root/usr/bin:/usr/bin")
        with self.assertRaisesRegex(Refusal, "AlmaLinux 8"):
            checked('ID="ubuntu"\nVERSION_ID="26.04"\n', "/opt/rh/gcc-toolset-15/root/usr/bin")
        with self.assertRaisesRegex(Refusal, "AlmaLinux 8"):
            checked('ID="almalinux"\nVERSION_ID="9.6"\n', "/opt/rh/gcc-toolset-15/root/usr/bin")
        with self.assertRaisesRegex(Refusal, "gcc-toolset-15"):
            checked(almalinux, "/usr/bin")
        with mock.patch.object(depsset, "SYSTEM", "windows"):
            depsset.check_container()


class PackSetTest(unittest.TestCase):
    def test_the_export_is_cut_into_parts_that_join_into_it_without_the_tools_debug_information(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        exported = folder / "export"
        tree = {"scripts/buildsystems/vcpkg.cmake": b"# toolchain\n",
                "installed/t/lib/osg.lib": os.urandom(5000), "installed/t/lib/osg.pdb": b"kept: a library's",
                "installed/t/tools/Qt6/bin/uic.exe": b"MZ", "installed/t/tools/Qt6/bin/uic.pdb": b"dropped"}
        for name, data in tree.items():
            (exported / name).parent.mkdir(parents=True, exist_ok=True)
            (exported / name).write_bytes(data)
        sets = folder / "sets"
        sets.mkdir()
        # A part of an earlier, longer set, which no join may take.
        (sets / "vcpkg-t.tar.gz.9").write_bytes(b"old")

        with mock.patch.object(deps, "VCPKG_SETS", sets):
            # 2048-byte parts: the incompressible library alone fills two.
            parts = depsset.pack_set(exported, "t", part_size=2048)
            self.assertEqual(parts, depsset.set_parts("t"))
        self.assertGreaterEqual(len(parts), 3)
        self.assertEqual([part.name for part in parts], [f"vcpkg-t.tar.gz.{index}" for index in range(len(parts))])
        self.assertEqual([part.stat().st_size for part in parts[:-1]], [2048] * (len(parts) - 1))
        self.assertTrue(0 < parts[-1].stat().st_size <= 2048)
        self.assertEqual(sorted(path.name for path in sets.iterdir()), sorted(part.name for part in parts),
                         "the staging folder or an old part stayed")

        joined = b"".join(part.read_bytes() for part in parts)
        with tarfile.open(fileobj=io.BytesIO(joined), mode="r:gz") as packed:
            files = {member.name: packed.extractfile(member).read() for member in packed if member.isfile()}
        expected = {f"t/{name}": data for name, data in tree.items() if name != "installed/t/tools/Qt6/bin/uic.pdb"}
        self.assertEqual(files, expected)

        # Ten parts sort by number, not by name.
        with mock.patch.object(deps, "VCPKG_SETS", sets):
            for index in range(len(parts), 11):
                (sets / f"vcpkg-t.tar.gz.{index}").write_bytes(b"")
            self.assertEqual([part.suffix for part in depsset.set_parts("t")], [f".{index}" for index in range(11)])


class VcpkgRootTest(unittest.TestCase):
    def test_the_registry_is_unpacked_with_the_tool_at_its_root_once(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        registry = Pin("https://example.com/vcpkg/archive/abc.tar.gz", "a" * 64)
        tool = Pin("https://example.com/vcpkg-glibc", "b" * 64)

        def download(pin: Pin, file: Path) -> None:
            if pin is registry:
                with tarfile.open(file, "w:gz") as tarred:
                    for name, text in (("vcpkg-abc/.vcpkg-root", b""), ("vcpkg-abc/ports/zlib/vcpkg.json", b"{}")):
                        entry = tarfile.TarInfo(name)
                        entry.size = len(text)
                        tarred.addfile(entry, io.BytesIO(text))
            else:
                file.write_bytes(b"#!/bin/sh\n")

        with (mock.patch.object(deps, "DEPS", folder), mock.patch.object(deps.pins, "VCPKG_REGISTRY", registry),
              mock.patch.dict(deps.pins.VCPKG_TOOL, {deps.SYSTEM: tool}),
              mock.patch.object(deps.fetch, "download_pin", side_effect=download) as fetched):
            root = deps.vcpkg_root()
            self.assertEqual(root, deps.vcpkg_root_dir())
            self.assertTrue((root / ".vcpkg-root").is_file(), "the archive's own folder was kept")
            self.assertTrue((root / "ports" / "zlib" / "vcpkg.json").is_file())
            self.assertTrue((root / f"vcpkg{deps.EXE}").is_file())
            if not deps.WINDOWS:
                self.assertTrue(os.access(root / "vcpkg", os.X_OK), "the tool cannot run")
            self.assertEqual(sorted(path.name for path in folder.iterdir()), [root.name], "the archive stayed")
            self.assertEqual(deps.vcpkg_root(), root)
            self.assertEqual(fetched.call_count, 2, "a vcpkg already there was fetched again")


if __name__ == "__main__":
    unittest.main()
