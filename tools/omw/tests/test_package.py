import shutil
import tempfile
import unittest
from pathlib import Path

from omw.package import (
    CONTAINER_DIR,
    LINUX_RUNTIME,
    RELEASE_IMAGE,
    WINDOWS_RUNTIME,
    container_command,
    executables_in,
    harness_files,
    missing_runtime,
    on_release_base,
    os_release,
    osg_plugin_folder,
    prune_empty,
    used_osg_plugins,
    wayland_platform_plugins,
)
from omw.system import ROOT, WINDOWS, Refusal
from omw.tests.fixtures import temporary_build, write_cache


class OsgPluginFolderTest(unittest.TestCase):
    def test_the_folder_is_named_for_the_release_beside_the_library_and_a_missing_one_is_refused(self):
        build = temporary_build(self)
        # The release `osg/Version` states, never the suffixed one OpenMW's package gives pkg-config.
        write_cache(build, "OPENSCENEGRAPH_VERSION:INTERNAL=3.6.5",
                    "OSG_LIBRARY_RELEASE:FILEPATH=/usr/lib/x86_64-linux-gnu/libosg.so")
        self.assertEqual(osg_plugin_folder(build), Path("/usr/lib/x86_64-linux-gnu/osgPlugins-3.6.5"))

        for lines in (
            ("OSG_LIBRARY_RELEASE:FILEPATH=/usr/lib/libosg.so",),
            ("OPENSCENEGRAPH_VERSION:INTERNAL=3.6.5",),
            ("OPENSCENEGRAPH_VERSION:INTERNAL=3.6.5", "OSG_LIBRARY_RELEASE:FILEPATH=OSG_LIBRARY_RELEASE-NOTFOUND"),
        ):
            write_cache(build, *lines)
            with self.subTest(lines=lines), self.assertRaises(Refusal):
                osg_plugin_folder(build)


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
        command = container_command("/usr/bin/docker", ["python3", "omw", "archive", "v1"], "1000:1000", "package")
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

    def test_the_programs_are_the_system_s_executables_and_no_library_or_data_file(self):
        for name in ("openmw", "openmw.exe", "libosg.so.161", "openmw.cfg"):
            self.touch(name)
        for name in ("openmw", "libosg.so.161"):
            (self.root / name).chmod(0o755)
        self.assertEqual([path.name for path in executables_in(self.root)], ["openmw.exe"] if WINDOWS else ["openmw"])

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

    def test_the_runtime_an_archive_lacks_is_named_and_a_folder_in_its_place_counts_for_nothing(self):
        self.assertEqual(missing_runtime(self.root, LINUX_RUNTIME), ["usr/lib/libvulkan.so.1"])
        self.touch("usr/lib/libvulkan.so.1")
        self.assertEqual(missing_runtime(self.root, LINUX_RUNTIME), [])

        self.touch("msvcp140.dll")
        (self.root / "vcruntime140.dll").mkdir()
        self.assertEqual(missing_runtime(self.root, WINDOWS_RUNTIME), ["vcruntime140.dll", "vcruntime140_1.dll"])

    def test_pruning_takes_every_folder_without_a_file_and_keeps_the_rest(self):
        self.touch("resources/a.txt")
        (self.root / "rtxtool" / "vfs" / "scripts").mkdir(parents=True)
        (self.root / "test-output").mkdir()
        prune_empty(self.root)
        self.assertEqual(sorted(p.relative_to(self.root).as_posix() for p in self.root.rglob("*")),
                         ["resources", "resources/a.txt"])


if __name__ == "__main__":
    unittest.main()
