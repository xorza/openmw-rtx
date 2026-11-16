# Where the gate's time goes — 2026-11-13

A low-end AMD integrated GPU (RDNA 2), Windows 11, 16 logical processors, MSVC, the debug flavour, the driver's
pipeline cache warm; each step the gate's own under a wall clock. Checked on a high-end NVIDIA laptop GPU on
2026-11-14.

## Before and after the gate's rework

| step | before, s | after, s | what changed |
|---|---|---|---|
| format check, spellings, lint, the driver's tests | 13.7 | 12.1 | |
| build | 128.1 | 2.9 | before, two commits to build |
| tests | 53.7 | 32.8 | two GPU shards and one host process, then four GPU shards and two host shards |
| their timing | 7.5 | 0.2 | |
| the release compile | 541.6 | 60.2 | the test binaries out of `all` |
| the integration tests | 66.7 | 72.7 | |
| check | 180.9 | 96.9 | 1920x1080, then 957x539 |
| repeat | 192.5 | 85.9 | the same |
| the whole gate | 1184.8 | 363.9 | |

## Why each change

- **`check` and `repeat` at a smaller frame**: what each place holds is the same at every size to the tenth of a MiB,
  and all 59 checks pass alike; only the frame's own memory moves. Under the smaller frame the host bounds the frame
  in a build with every assert and synchronization validation. `repeat`'s pair went from 194 s at 1920x1080 to 86 s
  at 960x540. The size is odd on purpose: 957x539 traces at 638x359, so no extent is a whole number of 8-wide
  workgroups, and every `check` and pair still passes there.
- **The test binaries out of the release compile**: they were 138 of its 277 objects and 59% of its compile seconds.
- **More GPU test shards once the layers' shader checks are off in the tests**: a validated start's cost is those
  checks (4.6 of 7.3 s, 44 of 46 CPU seconds), and they ran through every test, so with them on four or six shards
  were no faster than two. No test reads their messages; the build runs `spirv-val` over every module, and `check`
  keeps them. Off, four shards and the host tests finish in 35 s against 56.
- **No GPU suite spends more than 0.21 s outside its own cases**, so no fixture is worth a process of its own.

On the NVIDIA laptop the smaller frame saved 3.5 s of `check` and 6 s of a pair, and four shards beat two.

## The checks still catch what they are for

Each fault put into the tree, run and taken out:

- **A binding hidden from its stage**: `check` stops on `VUID-VkComputePipelineCreateInfo-layout-07988`; the GPU
  binary, without the shader checks, fails at the driver's compile with no word of the layers'.
- **A denoiser barrier left out**: `check` at 957x539 stops on `SYNC-HAZARD-READ-AFTER-WRITE`.
- **Every random draw of a stop seeded from the clock**: `repeat --pairs=3` at 957x539 finds no pair repeatable.
