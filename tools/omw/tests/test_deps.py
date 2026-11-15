import io
import json
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from omw import deps, pins
from omw.deps import pinned_folder
from omw.pins import Pin
from omw.system import DEPS, ROOT, Refusal


class PinnedFolderTest(unittest.TestCase):
    def test_a_folder_is_named_after_its_pins_so_a_changed_pin_is_a_folder_not_there_yet(self):
        old = Pin("https://example.com/a", "a" * 64)
        new = Pin("https://example.com/a", "b" * 64)
        other = Pin("https://example.com/b", "c" * 64)
        self.assertEqual(pinned_folder("crash", old), pinned_folder("crash", old))
        self.assertEqual(pinned_folder("crash", old).parent, DEPS)
        self.assertTrue(pinned_folder("crash", old).name.startswith("crash-"))
        self.assertNotEqual(pinned_folder("crash", old), pinned_folder("crash", new))
        self.assertNotEqual(pinned_folder("tools", old, other), pinned_folder("tools", new, other),
                            "one pin of several changed and the folder did not")


class CMakeTest(unittest.TestCase):
    def test_the_pinned_cmake_is_unpacked_whole_once_and_put_first_on_the_path(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        suffix = ".zip" if deps.WINDOWS else ".tar.gz"
        inner = "cmake-9.9.9-test"
        pin = Pin(f"https://example.com/{inner}{suffix}", "d" * 64)

        def download(_pin: Pin, file: Path) -> None:
            program = b"#!/bin/sh\n"
            if deps.WINDOWS:
                with zipfile.ZipFile(file, "w") as zipped:
                    zipped.writestr(f"{inner}/bin/cmake.exe", program)
            else:
                with tarfile.open(file, "w:gz") as tarred:
                    entry = tarfile.TarInfo(f"{inner}/bin/cmake")
                    entry.size = len(program)
                    entry.mode = 0o755
                    tarred.addfile(entry, io.BytesIO(program))

        def cut_off(_pin: Pin, file: Path) -> None:
            file.write_bytes(b"half")
            raise KeyboardInterrupt

        with (mock.patch.object(deps, "DEPS", folder), mock.patch.dict(deps.pins.CMAKE, {deps.SYSTEM: pin}),
              mock.patch.object(deps.fetch, "download_pin", side_effect=cut_off), self.assertRaises(KeyboardInterrupt)):
            deps.cmake()
        # A download cut off is inside the partial the next fetch clears, and nowhere else.
        self.assertEqual([path.name for path in folder.iterdir()], [f"{pinned_folder('cmake', pin).name}.partial"])

        with (mock.patch.object(deps, "DEPS", folder), mock.patch.dict(deps.pins.CMAKE, {deps.SYSTEM: pin}),
              mock.patch.object(deps.fetch, "download_pin", side_effect=download) as fetched):
            found = deps.cmake()
            self.assertEqual(found, deps.cmake_dir())
            self.assertEqual(found.parent, folder)
            self.assertTrue((found / "bin" / f"cmake{deps.EXE}").is_file())
            if not deps.WINDOWS:
                self.assertTrue(os.access(found / "bin" / "cmake", os.X_OK), "the program lost its mode")
            self.assertEqual(sorted(path.name for path in folder.iterdir()), [found.name], "the archive stayed")

            self.assertEqual(deps.cmake(), found)
            self.assertEqual(fetched.call_count, 1, "a CMake already there was fetched again")

            # CMake first on the path, and the pinned Ninja named for the presets.
            env = {deps.environment_key("PATH"): "/elsewhere"}
            with mock.patch.object(deps, "ninja", return_value=folder / "ninja-x"):
                deps.cmake_environment(env)
            self.assertEqual(env[deps.environment_key("PATH")], os.pathsep.join([str(found / "bin"), "/elsewhere"]))
            self.assertEqual(env[deps.environment_key("OMW_NINJA")], str(folder / "ninja-x" / f"ninja{deps.EXE}"))


class NinjaTest(unittest.TestCase):
    def test_the_pinned_ninja_is_taken_out_of_its_archive_once(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        pin = Pin("https://example.com/ninja-test.zip", "e" * 64)

        def download(_pin: Pin, file: Path) -> None:
            file.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(file, "w") as zipped:
                zipped.writestr(f"ninja{deps.EXE}", b"#!/bin/sh\n")

        with (mock.patch.object(deps, "DEPS", folder), mock.patch.dict(deps.pins.NINJA, {deps.SYSTEM: pin}),
              mock.patch.object(deps.fetch, "download_pin", side_effect=download) as fetched):
            found = deps.ninja()
            self.assertEqual(found, deps.ninja_dir())
            self.assertEqual(sorted(path.name for path in found.iterdir()), [f"ninja{deps.EXE}"], "the archive stayed")
            if not deps.WINDOWS:
                self.assertTrue(os.access(found / "ninja", os.X_OK), "the program cannot run")
            self.assertEqual(deps.ninja(), found)
            self.assertEqual(fetched.call_count, 1, "a Ninja already there was fetched again")


class WheelProgramTest(unittest.TestCase):
    def test_the_one_program_of_a_wheel_is_taken_out_of_it_once_and_nothing_else_of_it_is_kept(self):
        tools = (("clang-format", "CLANG_FORMAT", deps.clang_format, deps.clang_format_dir),
                 ("ruff", "RUFF", deps.ruff, deps.ruff_dir), ("zizmor", "ZIZMOR", deps.zizmor, deps.zizmor_dir))
        for name, pinned, get, where in tools:
            with self.subTest(name=name):
                folder = Path(tempfile.mkdtemp())
                self.addCleanup(shutil.rmtree, folder)
                pin = Pin(f"https://example.com/{name}-test.whl", "a" * 64)

                def download(_pin: Pin, file: Path, name: str = name) -> None:
                    file.parent.mkdir(parents=True, exist_ok=True)
                    with zipfile.ZipFile(file, "w") as zipped:
                        zipped.writestr(f"{name}/__init__.py", b"")
                        zipped.writestr(f"{name}-0.data/scripts/{name}{deps.EXE}", b"#!/bin/sh\n")
                        zipped.writestr(f"{name}-0.dist-info/RECORD", b"")

                with (mock.patch.object(deps, "DEPS", folder),
                      mock.patch.dict(getattr(deps.pins, pinned), {deps.SYSTEM: pin}),
                      mock.patch.object(deps.fetch, "download_pin", side_effect=download) as fetched):
                    found = get()
                    self.assertEqual(found, pinned_folder(name, pin) / f"{name}{deps.EXE}")
                    self.assertEqual(found.parent, where())
                    self.assertEqual(sorted(path.name for path in found.parent.iterdir()), [found.name])
                    if not deps.WINDOWS:
                        self.assertTrue(os.access(found, os.X_OK), "the program cannot run")
                    self.assertEqual(get(), found)
                    self.assertEqual(fetched.call_count, 1, "a program already there was fetched again")


class MypyTest(unittest.TestCase):
    def test_the_type_check_s_wheels_are_pinned_for_every_system_and_interpreter_the_driver_runs_under(self):
        # 3.11, the oldest Python `omw` starts under, to the newest pinned; mypy and mypyc's runtime alike, as one
        # without the other is no mypy.
        wanted = {(system, f"cp3{minor}") for system in ("linux", "windows") for minor in range(11, 15)}
        self.assertEqual(set(pins.MYPY), wanted)
        self.assertEqual(set(pins.LIBRT), wanted)
        self.assertEqual(set(pins.AST_SERIALIZE), {"linux", "windows"})

    def test_mypy_and_what_it_imports_are_unpacked_into_one_folder_this_interpreter_runs_it_from(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)

        def download(pin: Pin, file: Path) -> None:
            package = Path(pin.url).name.split("-")[0]
            file.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(file, "w") as zipped:
                zipped.writestr(f"{package}/__init__.py", b"")

        with (mock.patch.object(deps, "DEPS", folder),
              mock.patch.object(deps.fetch, "download_pin", side_effect=download) as fetched):
            command = deps.mypy()
            where = deps.mypy_dir()
            self.assertEqual(command, [
                deps.sys.executable, "-I", "-c",
                "import sys; sys.path.insert(0, sys.argv.pop(1)); from mypy.__main__ import console_entry; "
                "sys.exit(console_entry())", str(where)])
            self.assertEqual(where, pinned_folder("mypy", *deps.mypy_pins()))
            # Each wheel's package, and no wheel left beside them.
            self.assertEqual(sorted(path.name for path in where.iterdir()),
                             ["ast_serialize", "librt", "mypy", "mypy_extensions", "pathspec", "typing_extensions"])
            self.assertEqual(deps.mypy(), command)
            self.assertEqual(fetched.call_count, 6, "a mypy already there was fetched again")

    def test_an_interpreter_no_wheel_is_pinned_for_is_refused_with_the_ones_that_are(self):
        wheels = {(deps.SYSTEM, "cp310"): Pin("https://example.com/a.whl", "a" * 64),
                  (deps.SYSTEM, "cp312"): Pin("https://example.com/b.whl", "b" * 64),
                  ("another", "cp399"): Pin("https://example.com/c.whl", "c" * 64)}
        self.assertEqual(deps._for_interpreter("mypy", wheels, 12), wheels[(deps.SYSTEM, "cp312")])
        for minor in (9, 11, 99):
            with self.subTest(minor=minor), self.assertRaises(Refusal) as refused:
                deps._for_interpreter("mypy", wheels, minor)
            self.assertEqual(str(refused.exception), f"pins.py names no mypy wheel for Python 3.{minor} on "
                                                     f"{deps.SYSTEM}: CPython 3.10 to 3.12")


class ClangTidyTest(unittest.TestCase):
    def test_the_programs_are_the_sets_and_built_from_the_release_the_compiler_is(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        installed = folder / "installed" / deps.TRIPLET / "tools" / "clang-tidy"
        installed.mkdir(parents=True)
        for program in ("clang-tidy", "clang-apply-replacements"):
            (installed / f"{program}{deps.EXE}").write_bytes(b"")
        with mock.patch.object(deps, "vcpkg_set", return_value=folder):
            self.assertEqual(deps.clang_tidy(), installed / f"clang-tidy{deps.EXE}")
            self.assertEqual(deps.clang_apply_replacements(), installed / f"clang-apply-replacements{deps.EXE}")
        port = json.loads((ROOT / "vcpkg/ports/clang-tidy/vcpkg.json").read_text(encoding="utf-8"))
        self.assertEqual(port["version"], pins.LLVM_VERSION, "clang-tidy and the compiler are other releases")

    def test_the_digest_moves_with_the_port_alone(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root)
        files = {"vcpkg/ports/clang-tidy/vcpkg.json": b"{}\n", "vcpkg/ports/clang-tidy/portfile.cmake": b"x()\n",
                 "vcpkg/ports/zlib/portfile.cmake": b"z()\n", "vcpkg.json": b"{}\n"}
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(text)
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        first = deps.clang_tidy_digest.__wrapped__(root)
        self.assertEqual(len(first), 16)
        # Another port, the manifest and a Windows clone's line endings are the same clang-tidy.
        (root / "vcpkg/ports/zlib/portfile.cmake").write_bytes(b"z(1)\n")
        (root / "vcpkg.json").write_bytes(b"[]\n")
        (root / "vcpkg/ports/clang-tidy/portfile.cmake").write_bytes(b"x()\r\n")
        self.assertEqual(deps.clang_tidy_digest.__wrapped__(root), first)
        seen = {first}
        for name in ("vcpkg/ports/clang-tidy/vcpkg.json", "vcpkg/ports/clang-tidy/portfile.cmake"):
            (root / name).write_bytes((root / name).read_bytes() + b"# moved\n")
            moved = deps.clang_tidy_digest.__wrapped__(root)
            self.assertNotIn(moved, seen, f"{name} moved and the digest did not")
            seen.add(moved)


class PruneTest(unittest.TestCase):
    def test_what_no_pin_names_goes_and_what_one_names_stays(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        with mock.patch.object(deps, "DEPS", folder):
            kept = sorted(deps.pinned_names())
            # Every getter's folder, the formatters' among them, which a prune once took.
            self.assertIn(deps.clang_format_dir().name, kept)
            self.assertIn(deps.gersemi_dir().name, kept)
            # And the lint's, which a bootstrap after a lint would otherwise take.
            for lint_tool in (deps.ruff_dir(), deps.zizmor_dir(), deps.mypy_dir()):
                self.assertIn(lint_tool.name, kept)
            for name in kept:
                (folder / name).mkdir()
            (folder / "vulkan-sdk-0.0.1-00000000").mkdir()
            (folder / "appimage").mkdir()
            (folder / f"{kept[0]}.partial").mkdir()
            (folder / "LLVM-14.0.6-win64.exe").write_bytes(b"left over")
            deps.prune()
            self.assertEqual(sorted(path.name for path in folder.iterdir()), kept)
            deps.prune()
            self.assertEqual(sorted(path.name for path in folder.iterdir()), kept, "a second prune took more")


class SetToolTest(unittest.TestCase):
    def test_a_tool_is_the_one_its_port_installed_in_the_set_and_one_missing_names_the_set(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        installed = folder / "installed" / deps.TRIPLET / "tools" / "Qt6" / "bin"
        installed.mkdir(parents=True)
        (installed / f"lupdate{deps.EXE}").write_bytes(b"")
        with mock.patch.object(deps, "vcpkg_set", return_value=folder):
            self.assertEqual(deps.set_tool("Qt6/bin", "lupdate"), installed / f"lupdate{deps.EXE}")
            # The port's folder is part of the answer: the same name under another port is not this tool.
            with self.assertRaisesRegex(Refusal, "the vcpkg set at .* holds no lupdate"):
                deps.set_tool("shaderc", "lupdate")
            with self.assertRaisesRegex(Refusal, "holds no lrelease"):
                deps.set_tool("Qt6/bin", "lrelease")


class CompilerEnvironmentTest(unittest.TestCase):
    def test_linux_names_the_pinned_clang_over_any_compiler_named_before_and_windows_names_none(self):
        pinned = Path("/deps/llvm-0123456789abcdef")
        with mock.patch.object(deps, "llvm", return_value=pinned), mock.patch.object(deps, "WINDOWS", False):
            named = {"PATH": "/usr/bin", "CC": "gcc", "CXX": "g++"}
            deps.compiler_environment(named)
        self.assertEqual(named, {"PATH": "/usr/bin", "CC": str(pinned / "bin" / "clang"),
                                 "CXX": str(pinned / "bin" / "clang++")})
        with mock.patch.object(deps, "llvm", side_effect=AssertionError("fetched on Windows")), \
                mock.patch.object(deps, "WINDOWS", True):
            windows = {"PATH": "C:\\Windows"}
            deps.compiler_environment(windows)
        self.assertEqual(windows, {"PATH": "C:\\Windows"})


class CachedToolsTest(unittest.TestCase):
    def test_ci_keeps_each_pinned_tool_and_windows_loader_on_windows_alone_and_prune_keeps_them_all(self):
        expected = [
            pinned_folder("cmake", pins.CMAKE["linux"]),
            pinned_folder("ninja", pins.NINJA["linux"]),
            pinned_folder("llvm", pins.LLVM),
        ]
        with mock.patch.object(deps, "SYSTEM", "linux"), mock.patch.object(deps, "WINDOWS", False):
            self.assertEqual(deps.cached_tools(), expected)
            self.assertLessEqual({tool.name for tool in deps.cached_tools()}, deps.pinned_names())
        with mock.patch.object(deps, "SYSTEM", "windows"), mock.patch.object(deps, "WINDOWS", True):
            self.assertEqual(deps.cached_tools(), [
                pinned_folder("cmake", pins.CMAKE["windows"]),
                pinned_folder("ninja", pins.NINJA["windows"]),
                pinned_folder("llvm", pins.LLVM),
                pinned_folder("vulkan-loader", pins.VULKAN_LOADER_WINDOWS),
            ])
            self.assertLessEqual({tool.name for tool in deps.cached_tools()}, deps.pinned_names())
        # A set pinned anew moves no tool's folder, so CI's cache of them stays.
        moved = pins.PublishedSet(**{**vars(pins.VCPKG_SET), "digest": "0" * 16})
        with mock.patch.object(deps, "SYSTEM", "linux"), mock.patch.object(deps, "WINDOWS", False), \
                mock.patch.object(pins, "VCPKG_SET", moved):
            self.assertEqual(deps.cached_tools(), expected)


if __name__ == "__main__":
    unittest.main()
