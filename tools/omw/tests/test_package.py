import contextlib
import io
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, call, patch

from omw.package import (
    CONTAINER_DIR,
    GLIBC_FLOOR,
    LINUX_LIBRARIES,
    RELEASE_IMAGE,
    WINDOWS_LIBRARIES,
    ShippedLibraries,
    archive,
    container_command,
    executables_in,
    glibc_versions,
    harness_files,
    on_release_base,
    os_release,
    prune_empty,
)
from omw.system import ROOT, WINDOWS, Refusal


class GlibcTest(unittest.TestCase):
    def test_the_versions_a_binary_asks_are_read_as_numbers_and_compared_as_such(self):
        listed = ("0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.2.5) memcpy\n"
                  "0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.28) fcntl64\n"
                  "0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.3.4) __memcpy_chk\n"
                  "0000000000000000      DF *UND*  0000000000000000 (GLIBCXX_3.4.30) _ZSt4cout\n")
        self.assertEqual(glibc_versions(listed), {(2, 2, 5), (2, 28), (2, 3, 4)})
        # 2.28 the newest, though 2.3.4 sorts after it as text.
        self.assertEqual(max(glibc_versions(listed)), GLIBC_FLOOR)
        self.assertGreater(max(glibc_versions(listed + "(GLIBC_2.34) pthread_yield\n")), GLIBC_FLOOR)
        self.assertEqual(glibc_versions(""), set())


class ReleaseBaseTest(unittest.TestCase):
    def test_almalinux_8_alone_builds_a_release_where_it_stands(self):
        alma = 'NAME="AlmaLinux"\nVERSION="8.10 (Cerulean Leopard)"\nID="almalinux"\nVERSION_ID="8.10"\n'
        self.assertEqual(os_release(alma)["VERSION_ID"], "8.10")
        self.assertEqual(os_release("# ID=fedora\nID='fedora'\n"), {"ID": "fedora"})
        self.assertTrue(on_release_base(alma))
        self.assertTrue(on_release_base('ID="almalinux"\nVERSION_ID="8.11"\n'), "a point release is the same base")
        for other in ('NAME="Fedora Linux"\nID=fedora\nVERSION_ID=44\n', 'ID="almalinux"\nVERSION_ID="9.6"\n',
                      'ID="almalinux"\nVERSION_ID="18.0"\n', 'NAME="Ubuntu"\nVERSION_ID="26.04"\nID=ubuntu\n', ""):
            with self.subTest(other=other):
                self.assertFalse(on_release_base(other))

    def test_the_container_runs_omw_on_the_tree_with_its_own_build_and_cache(self):
        tree = f"{ROOT}:{ROOT}"
        build = f"{CONTAINER_DIR / 'release'}:{ROOT / 'build-release'}"
        beside = ROOT.parent / f"{ROOT.name}-repository" / ".git"
        for common, expected in ((ROOT / ".git", [tree, build]), (beside, [tree, f"{beside}:{beside}", build])):
            with self.subTest(common=common), patch("omw.package.output", return_value=f"{common}\n"):
                command = container_command(
                    "/usr/bin/docker", ["python3", "omw", "archive", "v1"], "1000:1000", "release")
                self.assertEqual(command[:4], ["/usr/bin/docker", "run", "--rm", "--user"])
                self.assertEqual(command[command.index("--user") + 1], "1000:1000")
                volumes = [command[at + 1] for at, word in enumerate(command) if word == "--volume"]
                self.assertEqual(volumes, expected)
                # The caller's OMW_LTO by name, so a release's link-time optimisation reaches the build inside.
                self.assertEqual([command[at + 1] for at, word in enumerate(command) if word == "--env"],
                                 ["HOME=/tmp", f"CCACHE_DIR={CONTAINER_DIR / 'ccache'}", "OMW_LTO"])
                self.assertEqual(command[command.index("--workdir") + 1], str(ROOT))
                self.assertEqual(command[-5:], [RELEASE_IMAGE, "python3", "omw", "archive", "v1"])


class ArchiveTest(unittest.TestCase):
    def setUp(self):
        self.dist = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dist)

    def test_linux_packs_in_the_release_base_and_makes_the_symbols_outside_it(self):
        build = Mock(dir=ROOT / "build-release")
        for in_container, packed, symbols in ((True, [], [call(CONTAINER_DIR / "release", "rtx-v1")]),
                                              (False, [call(build, "rtx-v1")], [])):
            with (self.subTest(in_container=in_container), patch("omw.package.WINDOWS", False),
                  patch("omw.package.needs_container", return_value=in_container),
                  patch("omw.package.prepare_container") as container, patch("omw.package.run") as run,
                  patch("omw.package._archive_linux") as linux, patch("omw.package.symbols") as symbol_store,
                  patch("omw.package.DIST", self.dist), contextlib.redirect_stderr(io.StringIO()) as said):
                self.assertEqual(archive(build, ["rtx-v1"]), 0)
                # Where it makes none, it says so.
                self.assertEqual("no symbols:" in said.getvalue(), not in_container)
                self.assertEqual(linux.call_args_list, packed)
                self.assertEqual(symbol_store.call_args_list, symbols)
                self.assertEqual(run.call_count, 1 if in_container else 0)
                if in_container:
                    container.return_value.command.assert_called_once_with(["python3", "omw", "archive", "rtx-v1"])

    def test_windows_packs_and_makes_its_symbols_from_its_own_build(self):
        build = Mock(dir=ROOT / "build-release")
        with (patch("omw.package.WINDOWS", True), patch("omw.package.needs_container") as needs,
              patch("omw.package._archive_windows") as windows, patch("omw.package.symbols") as symbol_store,
              patch("omw.package.DIST", self.dist)):
            self.assertEqual(archive(build, ["rtx-v1"]), 0)
        needs.assert_not_called()
        self.assertEqual(windows.call_args_list, [call(build, "rtx-v1")])
        self.assertEqual(symbol_store.call_args_list, [call(ROOT / "build-release", "rtx-v1")])

    def test_an_unnamed_archive_is_named_after_the_nearest_release_tag(self):
        build = Mock(dir=ROOT / "build-release")
        with (patch("omw.package.WINDOWS", False), patch("omw.package.needs_container", return_value=False),
              patch("omw.package._archive_linux") as linux, patch("omw.package.symbols"),
              patch("omw.package.output", return_value="rtx-v0.4.5-61-g4672dda43b\n") as described,
              patch("omw.package.DIST", self.dist), contextlib.redirect_stderr(io.StringIO())):
            archive(build, [])
        self.assertEqual(described.call_args.args[0][-4:], ["--tags", "--match", "rtx-v*", "--always"])
        self.assertEqual(linux.call_args, call(build, "rtx-v0.4.5-61-g4672dda43b"))


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

    def test_an_archive_holds_the_libraries_it_ships_and_no_other_and_a_folder_counts_for_nothing(self):
        # Linux's, with a system of the test's own in place of the release base's folders, which a desk's
        # system would otherwise answer for.
        self.assertEqual(LINUX_LIBRARIES.system, (Path("/usr/lib64"), Path("/lib64")))
        system = self.root / "system"
        linux = ShippedLibraries(LINUX_LIBRARIES.pattern, LINUX_LIBRARIES.names, (self.root / "elsewhere", system))
        self.assertEqual(linux.mismatch(self.root), ["no usr/lib/libvulkan.so.1"])
        self.touch("usr/lib/libvulkan.so.1")
        self.assertEqual(linux.mismatch(self.root), [])
        # One linuxdeploy brought along, which the static build should have linked in.
        self.touch("usr/lib/libosg.so.161")
        self.assertEqual(linux.mismatch(self.root), ["usr/lib/libosg.so.161 beside them"])
        with self.assertRaisesRegex(Refusal, "libosg.so.161 beside them"):
            linux.refuse_mismatch(self.root)
        # The system's own, by name in one of its folders, rides along; a name with no system copy does not.
        self.touch("usr/lib/libxcb-cursor.so.0")
        self.assertEqual(linux.mismatch(self.root),
                         ["usr/lib/libosg.so.161 beside them", "usr/lib/libxcb-cursor.so.0 beside them"])
        self.touch("system/libxcb-cursor.so.0")
        self.assertEqual(linux.mismatch(self.root), ["usr/lib/libosg.so.161 beside them"])
        # A folder by that name is no library.
        (self.root / "elsewhere" / "libosg.so.161").mkdir(parents=True)
        self.assertEqual(linux.mismatch(self.root), ["usr/lib/libosg.so.161 beside them"])

        (self.root / "openmw-wer.dll").mkdir()
        self.touch("Qt6Core.dll")
        self.touch("tls/qschannelbackend.dll")
        self.assertEqual(WINDOWS_LIBRARIES.mismatch(self.root), ["no openmw-wer.dll", "Qt6Core.dll beside them"])

        # The pattern decides what is a library: a plugin folder's own is, when the pattern reaches it.
        deep = ShippedLibraries("**/*.dll", ("Qt6Core.dll",), ())
        self.assertEqual(deep.mismatch(self.root), ["tls/qschannelbackend.dll beside them"])

    def test_pruning_takes_every_folder_without_a_file_and_keeps_the_rest(self):
        self.touch("resources/a.txt")
        (self.root / "rtxtool" / "vfs" / "scripts").mkdir(parents=True)
        (self.root / "test-output").mkdir()
        prune_empty(self.root)
        self.assertEqual(sorted(p.relative_to(self.root).as_posix() for p in self.root.rglob("*")),
                         ["resources", "resources/a.txt"])


if __name__ == "__main__":
    unittest.main()
