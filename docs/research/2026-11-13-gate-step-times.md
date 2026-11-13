# Where the gate's time goes on a low-end AMD integrated GPU — 2026-11-13

A low-end AMD integrated GPU (RDNA 2), Windows 11, 16 logical processors, MSVC, the debug flavour at
`e21fad4c21` with a clean tree, the driver's pipeline cache warm. Each step is the gate's own (`STEPS` in
`tools/omw/gate.py`) under a wall clock, one process group at a time. Two tests of `components-tests` fail on
this build (`.notes/ISSUES.md`), so the gate stops at `tests`; the steps after it were run by themselves, as the
gate calls them. One run each: `check` took 176 s in a gate eighteen hours earlier and 178.7 s here.

## The steps

| step | seconds | |
|---|---|---|
| format check | 4.6 | |
| spellings | 1.0 | |
| lint | 4.3 | |
| the driver's tests | 2.9 | |
| build | 3.7 | nothing to build |
| the listing check | 0.1 | |
| the release compile | 362.0 | 277 objects and 28 links: the release tree stood two commits behind |
| tests | 58.6 | `rtx.gpu.0` 58.5, `rtx.gpu.1` 57.1, `components` 49.4, `crash.matrix` 11.4, `openmw` 9.5 |
| their timing | 1.0 | no test over the limit beside the others, so none ran again |
| the integration tests | 67.2 | `test_lua_api` 54.8 s, `test_script_placed` 11.3 s |
| check | 178.7 | |
| repeat | 194.1 | two runs |

516 s without the release compile, 878 s with it. `check` and `repeat` are 373 s of the 516.

## `check` by frame size

`./omw check [--size=...]`, the `[check]` suite: 1537 frames a session at every size, 59 checks asked and none
failed at every size. The spans are between the session's stop lines; the frame is the median over the first
stop's 549 frames, of which the world stood whole after 420.

| shown, traced | whole run | to the first stop | ship | ship at dawn | guild | crossing | frame at the ship |
|---|---|---|---|---|---|---|---|
| 1920x1080, 1280x720 | 178.7 s | 9.0 s | 72.2 s | 19.0 s | 13.0 s | 64.7 s | 129.7 ms |
| 960x540, 640x360 | 89.2 s | 7.3 s | 30.8 s | 6.3 s | 4.7 s | 39.3 s | 56.7 ms |
| 640x360, 426x240 | 78.4 s | 8.3 s | 25.8 s | 5.0 s | 3.9 s | 34.7 s | 44.4 ms |

The crossing's 597 measured frames took 53.9 s, 34.0 s and 29.1 s.

The ship's one measured frame, from its report: at 1920x1080 the frame is 133.5 ms and the card's zones sum to
119 ms, the hold's 8 among them; at 640x360 the frame is 40.7 ms, the zones 30 ms with the same 8, and the host's
walk 23.2 ms. Under the smaller frames the host bounds the frame, in a build with every assert and the layers'
synchronization validation.

What each place held is the same at every size, to the tenth of a MiB: its instances and slots, structures,
tables, and textures (1082 at the ship, 119.2 MiB). The frame's own memory is what moves, 740.3 MiB at
1920x1080, 353.2 at 960x540 and 282.6 at 640x360, and the share of primary rays that hit, 84.1% against 84.4%
at the ship.

## `repeat` by frame size

`./omw repeat --pairs=1 [--size=...]`: two processes, the upscaler and the filter off, so the frame is traced at
the size shown. Every pair identical.

| size | two runs |
|---|---|
| 1920x1080 | 194.1 s |
| 960x540 | 86.0 s |
| 640x360 | 68.2 s |

One run of the pair by itself at 1920x1080: 93.0 s, 5.3 s of it before the stop, then 900 frames at a median
96.0 ms: 420 until the world stood whole, 120 warming up, 360 measured in 35.5 s.

## The GPU test binary

`rtx-gpu-tests`, 499 tests, split by `GTEST_TOTAL_SHARDS` into processes started together; each figure is the
process's own total as Google Test prints it, with its user CPU seconds where they were read.

| processes | tests each | seconds each | user CPU each |
|---|---|---|---|
| one sixth, alone | 84 | 16.5 | 51.0 s |
| one half, alone | 250 | 44.8 | 68.6 s |
| the whole, alone | 499 | 76.5 | 90.7 s |
| two halves | 250, 249 | 53.1, 51.0; again 57.2, 55.1 | 103.0, 102.2 s |
| four quarters | 124 to 125 | 47.8 to 50.1 | |
| six sixths | 83 to 84 | 48.7 to 54.7 | |

- **Four and six processes are no faster than two**, and two take 51 to 57 s against 44.8 s for one half alone
  and 76.5 s for the whole in one process.
- **A start costs 7.2 s and 45 to 46 s of user CPU**, measured twice with one small test
  (`--gtest_filter=RtxPinningTest.*`). The device's own summary: the validated renderer's 337 pipelines made over
  5.48 to 6.10 s, none compiled and all 337 found in the pipeline cache; the unvalidated renderer's 337 over 0.13
  to 0.14 s, likewise all found.
- **The harness pays the same start where it validates, and `repeat`'s runs do not validate**: `check`, under
  the layers' synchronization validation by default, made its 338 pipelines over 6.58 to 7.21 s; the `bench`
  run `repeat` starts, whose validation is off, made 334 over 0.13 s.
- **The start's cost is the core checks', not synchronization validation's**: `./omw info`, which ends before
  the background kernels do, made 103 pipelines over 0.76 s under `--validation=sync`, 102 over 0.75 s under
  `--validation=on`, and all 337 over 0.11 s under `--validation=off`.
- **A process spends more CPU than wall**: 1.5 times its wall alone, 1.8 times beside another.
- **In the gate's own run** the 499 cases sum to 88.1 s, 57 of them at or over half a second summing to 43.8 s,
  and each shard's report runs 13 s longer than its cases.

`components-tests` (2830 tests, one process): 18.5 to 19.7 s alone with 9.7 s of user CPU, 46.1 s beside the two
GPU shards with 21.9 s of user CPU, 49.4 s in the gate's `tests`. In four shards alone: 3.6 to 6.6 s.

## The release compile

From `out/release/.ninja_log`, the 311 commands of the run above: 277 compiles summing to 3979 s across the
processors, 350 s of wall.

| target | objects | compile seconds, summed |
|---|---|---|
| `rtx-gpu-tests` | 75 | 1232 |
| `components-tests` | 55 | 953 |
| `openmw-rtx-vulkan` | 93 | 877 |
| `openmw-lib` | 12 | 253 |
| `openmw-rtxtool-lib` | 16 | 218 |
| `openmw-rtxtool` | 9 | 217 |
| `openmw-tests` | 8 | 169 |
| the other four | 9 | 62 |

The three test binaries are 138 of the 277 objects and 2354 of the 3979 compile seconds, 59%. The links sum to
346 s, `openmw.exe` 40.3 s, `openmw-rtxtool.exe` 37.4 s, `openmw-tests.exe` 20.6 s and `components-tests.exe`
18.6 s among them.

## Not measured

- A high-end NVIDIA laptop GPU's figures for any of the above.
- Where a validated start's six seconds go inside the layers' core checks, and which thread spends the CPU a
  process has over its wall.
- The release compile with nothing to build, and its wall without the test binaries.
