import io
import os
import shutil
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from omw import deps, pins
from omw.deps import pinned_folder
from omw.pins import Pin
from omw.system import DEPS


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


class PruneTest(unittest.TestCase):
    def test_what_no_pin_names_goes_and_what_one_names_stays(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        with mock.patch.object(deps, "DEPS", folder):
            kept = sorted(deps.pinned_names())
            # Every getter's folder, the formatters' among them, which a prune once took.
            self.assertIn(deps.clang_format_dir().name, kept)
            self.assertIn(deps.gersemi_dir().name, kept)
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
