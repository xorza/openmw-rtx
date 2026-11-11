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
from omw.pins import Pin, PublishedSet
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
                            "vcpkg/triplets/x.cmake": b"set(A 1)\n", "env/install_almalinux_deps.sh": b"dnf\n",
                            "README.md": b"text\n"})
        self.addCleanup(shutil.rmtree, root)
        first = deps.set_digest(root)
        self.assertEqual(len(first), 16)
        self.assertEqual(deps.set_digest(root), first, "the same inputs named another set")

        # A file outside the inputs moves nothing.
        (root / "README.md").write_bytes(b"other\n")
        self.assertEqual(deps.set_digest(root), first)
        # A Windows clone's line endings are the same set.
        (root / "vcpkg.json").write_bytes(b"{}\r\n")
        self.assertEqual(deps.set_digest(root), first)

        seen = {first}
        for name in ("vcpkg.json", "vcpkg-configuration.json", "vcpkg/triplets/x.cmake",
                     "env/install_almalinux_deps.sh"):
            (root / name).write_bytes((root / name).read_bytes() + b"# moved\n")
            moved = deps.set_digest(root)
            self.assertNotIn(moved, seen, f"{name} moved and the set kept its name")
            seen.add(moved)
        # A new overlay port is an input too.
        (root / "vcpkg/ports/p/vcpkg.json").parent.mkdir(parents=True)
        (root / "vcpkg/ports/p/vcpkg.json").write_bytes(b"{}\n")
        self.assertNotIn(deps.set_digest(root), seen)

        with mock.patch.object(deps.pins, "VCPKG_REGISTRY", Pin("https://example.com/r", "e" * 64)):
            self.assertNotIn(deps.set_digest(root), seen, "another registry kept the set's name")


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


class VcpkgSetTest(unittest.TestCase):
    def test_this_desk_s_set_first_then_the_pinned_one_joined_from_its_parts_and_else_a_refusal(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        packed = io.BytesIO()
        with tarfile.open(fileobj=packed, mode="w:gz") as tarred:
            for name, data in ((f"{deps.TRIPLET}/scripts/buildsystems/vcpkg.cmake", b"# toolchain\n"),
                               (f"{deps.TRIPLET}/installed/{deps.TRIPLET}/lib/z.lib", os.urandom(3000))):
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                tarred.addfile(entry, io.BytesIO(data))
        whole = packed.getvalue()
        parts = [whole[:1000], whole[1000:2000], whole[2000:]]
        published = PublishedSet("owner/repo", "a" * 16, {deps.SYSTEM: ("1" * 64, "2" * 64, "3" * 64)})

        def download(pin: Pin, file: Path) -> None:
            file.write_bytes(parts[int(pin.url[-1])])

        with (mock.patch.object(deps, "DEPS", folder), mock.patch.object(deps.pins, "VCPKG_SET", published),
              mock.patch.object(deps.fetch, "download_pin", side_effect=download) as fetched):
            # Inputs that moved since the pin, with no set of this desk's for them.
            with mock.patch.object(deps, "set_digest", return_value="b" * 16), self.assertRaisesRegex(
                    Refusal, f"omw deps-set` builds deps-{'b' * 16}"):
                deps.vcpkg_set()
            self.assertEqual(fetched.call_count, 0)

            # This desk's own set for those inputs, which `omw deps-set` left.
            (folder / f"vcpkg-set-{'b' * 16}" / deps.TRIPLET).mkdir(parents=True)
            with mock.patch.object(deps, "set_digest", return_value="b" * 16):
                self.assertEqual(deps.vcpkg_set(), folder / f"vcpkg-set-{'b' * 16}" / deps.TRIPLET)
            self.assertEqual(fetched.call_count, 0)

            # The pinned set, its parts fetched in order, joined, unpacked and gone.
            with mock.patch.object(deps, "set_digest", return_value="a" * 16):
                found = deps.vcpkg_set()
                self.assertEqual(found, folder / f"vcpkg-set-{'a' * 16}" / deps.TRIPLET)
                self.assertEqual((found / "scripts" / "buildsystems" / "vcpkg.cmake").read_bytes(), b"# toolchain\n")
                self.assertEqual(len((found / "installed" / deps.TRIPLET / "lib" / "z.lib").read_bytes()), 3000)
                self.assertEqual([call.args[0].url[-1] for call in fetched.call_args_list], ["0", "1", "2"])
                self.assertEqual(sorted(path.name for path in folder.iterdir()),
                                 [f"vcpkg-set-{'a' * 16}", f"vcpkg-set-{'b' * 16}"], "a part stayed")
                self.assertEqual(deps.vcpkg_set(), found)
                self.assertEqual(fetched.call_count, 3, "a set already there was fetched again")


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
