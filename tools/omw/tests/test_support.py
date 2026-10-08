import hashlib
import os
import shutil
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from omw import deps
from omw.build import CONFIGURED_FROM, Build, configured_from, manifest_inputs, redate_ahead
from omw.deps import pinned_folder
from omw.fetch import build_beside, download, extract_member, partial_of, settle
from omw.package import (
    CONTAINER_DIR,
    RELEASE_IMAGE,
    container_command,
    harness_files,
    on_release_base,
    os_release,
    prune_empty,
    used_osg_plugins,
    wayland_platform_plugins,
)
from omw.pins import Pin
from omw.system import DEPS, ROOT, Refusal, environment_key, parse_set_output


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


class CachedFolderTest(unittest.TestCase):
    def test_a_folder_is_the_value_its_name_has_in_the_cache_and_a_missing_one_is_refused(self):
        build = Build("debug")
        build.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, build.dir)
        with self.assertRaises(Refusal):
            build.cached_folder("RTX_HARNESS_DIR")

        (build.dir / "CMakeCache.txt").write_text("".join(line + "\n" for line in (
            "// The harness's folder",
            "RTX_HARNESS_DIR:INTERNAL=/checkout/build/rtxtool",
            "RTX_HARNESS_DIR_EXTRA:INTERNAL=/elsewhere",
            "RTX_TEST_OUTPUT_DIR:INTERNAL=",
        )))
        self.assertEqual(build.cached_folder("RTX_HARNESS_DIR"), Path("/checkout/build/rtxtool"))
        for refused in ("RTX_TEST_OUTPUT_DIR", "RTX_SPIRV_DIR"):
            with self.subTest(name=refused), self.assertRaises(Refusal):
                build.cached_folder(refused)


class ManifestInputsTest(unittest.TestCase):
    def test_every_kind_of_input_and_no_output_or_validation(self):
        query = "".join(line + "\n" for line in (
            "build.ninja:",
            "  input: RERUN_CMAKE",
            "    /checkout/build/CMakeFiles/cmake.verify_globs",
            "    | /checkout/CMakeLists.txt",
            "    | CMakeCache.txt",
            "    || /checkout/files/settings-default.cfg",
            "  outputs:",
            "    /checkout/build/cmake_install.cmake",
            "  validations:",
            "    /checkout/build/validated",
        ))
        build = Path("/checkout/build")
        self.assertEqual(manifest_inputs(query, build), [
            build / "CMakeFiles" / "cmake.verify_globs", Path("/checkout/CMakeLists.txt"), build / "CMakeCache.txt",
            Path("/checkout/files/settings-default.cfg"),
        ])
        self.assertEqual(manifest_inputs("build.ninja:\n  outputs:\n    x\n", build), [])


class RedateAheadTest(unittest.TestCase):
    def test_a_file_dated_after_the_clock_takes_its_time_and_one_outside_the_tree_is_refused(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        tree, elsewhere = folder / "tree", folder / "elsewhere"
        tree.mkdir()
        elsewhere.mkdir()
        now = 1_800_000_000.0
        dated = {tree / "ahead.cmake": now + 23_580, tree / "behind.cmake": now - 60, tree / "now.cmake": now,
                 elsewhere / "ahead.cmake": now + 60}
        for path, modified in dated.items():
            path.write_text("")
            os.utime(path, (modified, modified))

        inside = [tree / "ahead.cmake", tree / "behind.cmake", tree / "now.cmake", tree / "missing.cmake"]
        with self.assertRaises(Refusal):
            redate_ahead([*inside, elsewhere / "ahead.cmake"], now, tree)
        self.assertEqual((tree / "ahead.cmake").stat().st_mtime, now + 23_580, "a file was touched before the refusal")

        # Only the one dated later than the clock moves, to the clock; one at it exactly is not after it.
        self.assertEqual(redate_ahead(inside, now, tree), [tree / "ahead.cmake"])
        self.assertEqual({path.name: path.stat().st_mtime for path in dated if path.parent == tree},
                         {"ahead.cmake": now, "behind.cmake": now - 60, "now.cmake": now})
        self.assertEqual(redate_ahead(inside, now, tree), [])


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
            made.writestr("release/bin/dump_syms", b"the tool")

        into = folder / "out"
        self.assertEqual(extract_member(archive, "dump_syms", into), into / "dump_syms")
        self.assertEqual((into / "dump_syms").read_bytes(), b"the tool")
        self.assertEqual(sorted(path.name for path in into.iterdir()), ["dump_syms"])
        with self.assertRaises(Refusal):
            extract_member(archive, "minidump-stackwalk", into)


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


class QuotedTest(unittest.TestCase):
    def test_a_path_is_one_powershell_string_whatever_it_holds(self):
        # Single quotes expand neither `$` nor a backtick, and a quote inside is doubled.
        self.assertEqual(deps._quoted(Path("C:/Users/o'neil/deps")), "'" + str(Path("C:/Users/o''neil/deps")) + "'")
        self.assertEqual(deps._quoted("copy_only=1"), "'copy_only=1'")
        self.assertEqual(deps._quoted("$env:PATH `n"), "'$env:PATH `n'")
        self.assertEqual(deps._quoted(""), "''")


class PruneTest(unittest.TestCase):
    def test_what_no_pin_names_goes_and_what_one_names_stays(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        with mock.patch.object(deps, "DEPS", folder):
            kept = sorted(deps.pinned_names())
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


class WaylandPlatformPluginsTest(unittest.TestCase):
    def test_the_set_this_qt_ships_whole_or_a_refusal(self):
        # Qt 6.11's one plugin, Ubuntu 24.04's Qt 6.4 with its two and the X compositing pair beside
        # them, half of the older pair, and none.
        cases = [
            (["libqwayland.so", "libqxcb.so"], ("libqwayland.so",)),
            (["libqwayland-egl.so", "libqwayland-generic.so", "libqwayland-xcomposite-egl.so", "libqxcb.so"],
             ("libqwayland-generic.so", "libqwayland-egl.so")),
            (["libqwayland-egl.so", "libqxcb.so"], None),
            (["libqxcb.so"], None),
        ]
        for files, expected in cases:
            with self.subTest(files=files):
                platforms = Path(tempfile.mkdtemp())
                self.addCleanup(shutil.rmtree, platforms)
                for name in files:
                    (platforms / name).write_bytes(b"")
                if expected is None:
                    with self.assertRaises(Refusal):
                        wayland_platform_plugins(platforms)
                else:
                    self.assertEqual(wayland_platform_plugins(platforms), expected)


class ReleaseBaseTest(unittest.TestCase):
    def test_ubuntu_24_04_alone_builds_a_release_where_it_stands(self):
        noble = 'PRETTY_NAME="Ubuntu 24.04.3 LTS"\nNAME="Ubuntu"\nVERSION_ID="24.04"\nID=ubuntu\nID_LIKE=debian\n'
        self.assertEqual(os_release(noble)["VERSION_ID"], "24.04")
        self.assertEqual(os_release("# ID=arch\nID='arch'\n"), {"ID": "arch"})
        self.assertTrue(on_release_base(noble))
        for other in ('NAME="Arch Linux"\nID=arch\nBUILD_ID=rolling\n',
                      'NAME="Ubuntu"\nVERSION_ID="26.04"\nID=ubuntu\n', ""):
            with self.subTest(other=other):
                self.assertFalse(on_release_base(other))

    def test_the_container_runs_omw_on_the_tree_with_its_own_build_and_cache(self):
        command = container_command("/usr/bin/docker", ["archive", "v1"], "1000:1000")
        self.assertEqual(command[:4], ["/usr/bin/docker", "run", "--rm", "--user"])
        self.assertEqual(command[command.index("--user") + 1], "1000:1000")
        volumes = [command[at + 1] for at, word in enumerate(command) if word == "--volume"]
        self.assertEqual(volumes, [f"{ROOT}:{ROOT}", f"{CONTAINER_DIR / 'package'}:{ROOT / 'build-package'}"])
        self.assertIn(f"CCACHE_DIR={CONTAINER_DIR / 'ccache'}", command)
        self.assertEqual(command[command.index("--workdir") + 1], str(ROOT))
        self.assertEqual(command[-5:], [RELEASE_IMAGE, "python3", "omw", "archive", "v1"])


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root)

    def touch(self, name: str) -> None:
        (self.root / name).parent.mkdir(parents=True, exist_ok=True)
        (self.root / name).write_text("")

    def test_the_harness_files_are_named_wherever_they_land_and_the_games_are_not(self):
        for name in ("openmw.exe", "openmw-rtxtool", "rtx-gpu-tests.exe", "resources/rtx/shaders/a.spv",
                     "resources/vfs/scripts/a.lua", "rtxtool/views.cfg", "rtxtool/vfs/rtxtool.omwscripts",
                     "resources/rtx/views.cfg", "resources/rtx/shaders-driver-cache/abc/entry",
                     "resources/rtx/shaders-census/a.spv", "test-output/crash-matrix/abort/log.txt"):
            self.touch(name)
        self.assertEqual(harness_files(self.root), [
            "openmw-rtxtool",
            "resources/rtx/shaders-census/a.spv",
            "resources/rtx/shaders-driver-cache/abc/entry",
            "resources/rtx/views.cfg",
            "rtx-gpu-tests.exe",
            "rtxtool/vfs/rtxtool.omwscripts",
            "rtxtool/views.cfg",
            "test-output/crash-matrix/abort/log.txt",
        ])

    def test_pruning_takes_every_folder_without_a_file_and_keeps_the_rest(self):
        self.touch("resources/a.txt")
        (self.root / "rtxtool" / "vfs" / "scripts").mkdir(parents=True)
        (self.root / "test-output").mkdir()
        prune_empty(self.root)
        self.assertEqual(sorted(p.relative_to(self.root).as_posix() for p in self.root.rglob("*")),
                         ["resources", "resources/a.txt"])


if __name__ == "__main__":
    unittest.main()
