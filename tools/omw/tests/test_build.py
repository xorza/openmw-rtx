import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from omw.build import (
    BUILD_LOCK,
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
from omw.system import Refusal, exclusive
from omw.tests.fixtures import temporary_build, write_cache


class ExclusiveTest(unittest.TestCase):
    def test_a_second_process_waits_and_says_why_until_the_first_lets_go(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        lock = folder / "made" / "here.lock"
        child = (
            "import sys\n"
            "from pathlib import Path\n"
            "from omw.system import exclusive\n"
            f"with exclusive(Path({str(lock)!r}), 'testing'):\n"
            "    print('inside', flush=True)\n"
        )
        env = {**os.environ, "PYTHONPATH": str(Path(__file__).resolve().parents[2])}

        with exclusive(lock, "testing"):
            second = subprocess.Popen(
                [sys.executable, "-c", child], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            self.addCleanup(second.kill)
            assert second.stderr is not None
            self.assertEqual(second.stderr.readline().strip(), "omw: waiting for another omw that is testing")
            self.assertIsNone(second.poll(), "it went in beside the first")

        out, _ = second.communicate(timeout=10)
        self.assertEqual(out.strip(), "inside")
        self.assertEqual(second.returncode, 0)

    def test_a_run_that_holds_its_directory_asks_again_without_waiting_for_itself(self):
        build = temporary_build(self)
        with build._building(), build._building():
            self.assertTrue((build.dir / BUILD_LOCK).is_file())

        # Let go whatever the inside did, so a failed build leaves the run able to build again.
        with self.assertRaises(RuntimeError), build._building():
            raise RuntimeError
        with build._building():
            pass


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


class OutputsAheadTest(unittest.TestCase):
    def test_only_a_directory_a_configure_succeeded_in_is_asked_for_its_outputs(self):
        build = temporary_build(self, "debug")
        write_cache(build, "CMAKE_MAKE_PROGRAM:FILEPATH=/no/ninja")
        # What a failed configure leaves: the cache and a `build.ninja` whose rules it never wrote.
        (build.dir / "build.ninja").write_text("include CMakeFiles/rules.ninja\n")
        with mock.patch("omw.build.output", side_effect=AssertionError("Ninja was asked")) as asked:
            build._remove_outputs_ahead()
            self.assertEqual(asked.call_count, 0)

        (build.dir / CONFIGURED_FROM).write_text("abc\n")
        with mock.patch("omw.build.output", return_value="") as asked:
            build._remove_outputs_ahead()
            self.assertEqual(asked.call_count, 1)


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
                    "OSG_LIBRARY_RELEASE:FILEPATH=OSG_LIBRARY_RELEASE-NOTFOUND")
        self.assertEqual(build.cached("RTX_HARNESS_DIR"), "/checkout/build/rtxtool")
        for refused in ("RTX_TEST_OUTPUT_DIR", "RTX_SPIRV_DIR", "OSG_LIBRARY_RELEASE"):
            with self.subTest(name=refused), self.assertRaises(Refusal):
                build.cached(refused)


class FolderTest(unittest.TestCase):
    def test_a_folder_is_what_the_configure_s_file_names_and_a_missing_or_empty_one_is_refused(self):
        build = temporary_build(self, "debug")
        with self.assertRaises(Refusal):
            build.folder("RTX_SPIRV_DIR")

        (build.dir / "omw-folders.json").write_text(
            '{"RTX_SPIRV_DIR": "/checkout/build/resources/rtx/shaders", "RTX_TEST_OUTPUT_DIR": ""}')
        self.assertEqual(build.folder("RTX_SPIRV_DIR"), Path("/checkout/build/resources/rtx/shaders"))
        for refused in ("RTX_TEST_OUTPUT_DIR", "RTX_SPIRV_SOURCE_DIR"):
            with self.subTest(name=refused), self.assertRaises(Refusal):
                build.folder(refused)


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
