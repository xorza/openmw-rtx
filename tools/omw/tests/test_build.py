import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw.build import (
    CONFIGURED_FROM,
    MANIFEST_AGED,
    configured_from,
    deps_inputs,
    device_free,
    manifest_inputs,
    ninja_outputs,
    redate_ahead,
    remove_ahead,
)
from omw.system import Refusal
from omw.tests.fixtures import temporary_build, write_cache


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


class BuildOnceTest(unittest.TestCase):
    def test_a_target_is_built_once_a_run_and_one_whose_build_failed_again(self):
        build = temporary_build(self, "debug")
        ran: list[list[str]] = []

        def ninja(command: list, **options) -> None:
            targets = [str(part) for part in command[command.index("--target") + 1:command.index("--")]]
            ran.append(targets)
            if "broken" in targets:
                raise subprocess.CalledProcessError(1, command)

        with (mock.patch.object(build, "configure"), mock.patch.object(build, "_redate_inputs_ahead"),
              mock.patch.object(build, "_env", {}), mock.patch("omw.build.run", ninja)):
            build.build(["openmw", "openmw-rtxtool"])
            build.build(["openmw-rtxtool", "crash-tests"])
            build.build(["openmw"])
            for _ in range(2):
                with self.assertRaises(subprocess.CalledProcessError):
                    build.build(["broken", "openmw"])
        self.assertEqual(ran, [["openmw", "openmw-rtxtool"], ["crash-tests"], ["broken"], ["broken"]])

    def test_the_tests_are_asked_of_ctest_once_a_run(self):
        build = temporary_build(self, "debug")
        listing = '{"tests": [{"name": "a", "properties": [{"name": "OPENMW_TARGET", "value": "a-tests"}]}]}'
        with (mock.patch.object(build, "configure"), mock.patch.object(build, "_env", {}),
              mock.patch("omw.build.output", return_value=listing) as ctest):
            self.assertEqual(build.test_targets(), ["a-tests"])
            self.assertEqual(build.test_targets(without_device=True), ["a-tests"])
        self.assertEqual(ctest.call_count, 1)


class CachedTest(unittest.TestCase):
    def test_a_value_is_what_its_name_holds_in_the_cache_and_a_missing_or_unfound_one_is_refused(self):
        build = temporary_build(self, "debug")
        with self.assertRaises(Refusal):
            build.cached("RTX_HARNESS_DIR")

        write_cache(build,
                    "// The harness's folder",
                    "RTX_HARNESS_DIR:INTERNAL=/checkout/build/rtxtool",
                    "RTX_HARNESS_DIR_EXTRA:INTERNAL=/elsewhere",
                    "RTX_TEST_OUTPUT_DIR:INTERNAL=",
                    "OSG_LIBRARY_RELEASE:FILEPATH=OSG_LIBRARY_RELEASE-NOTFOUND",
                    "OPENSCENEGRAPH_VERSION:INTERNAL=3.6.5")
        self.assertEqual(build.cached("RTX_HARNESS_DIR"), "/checkout/build/rtxtool")
        self.assertEqual(build.osg_plugin_folder_name(), "osgPlugins-3.6.5")
        for refused in ("RTX_TEST_OUTPUT_DIR", "RTX_SPIRV_DIR", "OSG_LIBRARY_RELEASE"):
            with self.subTest(name=refused), self.assertRaises(Refusal):
                build.cached(refused)


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

    def test_the_deps_log_names_each_header_once_and_no_output(self):
        logged = "".join(line + "\n" for line in (
            "components/a.cpp.obj: #deps 2, deps mtime 8142794389022204 (VALID)",
            "    ../components/a.cpp",
            "    /sdk/vulkan.h",
            "",
            "components/b.cpp.obj: #deps 2, deps mtime 8142794389022205 (STALE)",
            "    ../components/b.cpp",
            "    /sdk/vulkan.h",
            "",
        ))
        build = Path("/checkout/build")
        self.assertEqual(deps_inputs(logged, build), [
            build / "../components/a.cpp", Path("/sdk/vulkan.h"), build / "../components/b.cpp",
        ])
        self.assertEqual(deps_inputs("", build), [])

    def test_the_targets_listing_names_every_output_but_a_phony_one(self):
        listed = "".join(line + "\n" for line in (
            "build.ninja: RERUN_CMAKE",
            "openmw: phony",
            "components/a.cpp.obj: CXX_COMPILER__components_unscanned_RelWithDebInfo",
            "/checkout/files/lang/de.ts: CUSTOM_COMMAND",
        ))
        build = Path("/checkout/build")
        self.assertEqual(ninja_outputs(listed, build), [
            build / "build.ninja", build / "components" / "a.cpp.obj", Path("/checkout/files/lang/de.ts"),
        ])


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

        # A second spelling of one file is the same file.
        inside = [tree / "ahead.cmake", tree / "behind.cmake", tree / "now.cmake", tree / "missing.cmake",
                  tree / ".." / "tree" / "ahead.cmake"]
        with self.assertRaises(Refusal):
            redate_ahead([*inside, elsewhere / "ahead.cmake"], now, tree)
        self.assertEqual((tree / "ahead.cmake").stat().st_mtime, now + 23_580, "a file was touched before the refusal")

        # Only the one dated later than the clock moves, to the clock; one at it exactly is not after it.
        self.assertEqual(redate_ahead(inside, now, tree), [tree / "ahead.cmake"])
        self.assertEqual({path.name: path.stat().st_mtime for path in dated if path.parent == tree},
                         {"ahead.cmake": now, "behind.cmake": now - 60, "now.cmake": now})
        self.assertEqual(redate_ahead(inside, now, tree), [])

    def test_an_output_dated_after_the_clock_is_removed_only_inside_the_build_directory(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        build, tree = folder / "build", folder / "tree"
        build.mkdir()
        tree.mkdir()
        now = 1_800_000_000.0
        dated = {build / "ahead.obj": now + 2_520, build / "behind.obj": now - 60, build / "now.obj": now,
                 build / "build.ninja": now + 60, tree / "ahead.ts": now + 60, tree / "build.ninja": now + 60}
        for path, modified in dated.items():
            path.write_text("")
            os.utime(path, (modified, modified))

        # A path through `..` that leaves the build directory is judged where it lands, and a second
        # spelling of a file removed already is the same file.
        outputs = [*dated, build / "missing.obj", build / ".." / "tree" / "ahead.ts",
                   build / ".." / "build" / "ahead.obj"]
        self.assertEqual(remove_ahead(outputs, now, build), [build / "ahead.obj", build / "build.ninja"])
        self.assertEqual({path.relative_to(folder).as_posix(): path.stat().st_mtime for path in dated if path.exists()},
                         {"build/behind.obj": now - 60, "build/now.obj": now, "build/build.ninja": MANIFEST_AGED,
                          "tree/ahead.ts": now + 60, "tree/build.ninja": now + 60})
        self.assertEqual(remove_ahead(outputs, now, build), [])



class WindowsRuntimeTest(unittest.TestCase):
    def test_the_plugins_of_the_version_the_build_found_are_placed_and_a_folder_vcpkg_lacks_is_refused(self):
        build = temporary_build(self)
        windows_deps = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, windows_deps)
        bin_dir = windows_deps / "installed" / "x64-windows" / "bin"
        (bin_dir / "Release").mkdir(parents=True)
        (bin_dir / "osgPlugins-3.6.5").mkdir()
        for file in ("osg161-osg.dll", "Release/MyGUIEngine.dll", "osgPlugins-3.6.5/osgdb_bmp.dll"):
            (bin_dir / file).write_text(file)

        write_cache(build, "OPENSCENEGRAPH_VERSION:INTERNAL=3.6.5")
        build._place_windows_runtime(windows_deps)
        self.assertEqual(sorted(path.name for path in (build.dir / "osgPlugins-3.6.5").iterdir()), ["osgdb_bmp.dll"])

        # Another release finds no folder of its name, where a glob of the missing folder copied nothing.
        write_cache(build, "OPENSCENEGRAPH_VERSION:INTERNAL=3.6.6")
        with self.assertRaises(Refusal):
            build._place_windows_runtime(windows_deps)


def suite(target: str, *labels: str) -> dict:
    return {"name": target, "properties": [{"name": "LABELS", "value": list(labels)},
                                           {"name": "OPENMW_TARGET", "value": target}]}


class DeviceFreeTest(unittest.TestCase):
    def test_a_target_is_left_out_only_where_every_test_of_it_needs_a_device(self):
        tests = [suite("components-tests"), suite("rtx-gpu-tests", "device"), suite("rtx-gpu-tests", "device"),
                 suite("crash-tests", "device"), suite("crash-tests"), {"name": "no target", "properties": []}]
        self.assertEqual(device_free(tests), ["components-tests", "crash-tests"])
        self.assertEqual(device_free([]), [])


if __name__ == "__main__":
    unittest.main()
