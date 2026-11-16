# Where NVIDIA's compiler keeps what a stage's registers do not hold — 2026-11-14

A high-end NVIDIA laptop GPU (Ada, compute capability 8.9), driver 620.70.06, Linux.
`RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` at the debug log level prints each pipeline's statistics: the
shipped modules, every tuple, 246 pipelines. Registers are `Register Count`, shared memory `Shared Memory Size` in bytes
a 32-lane workgroup, local memory `Local Memory Size` in bytes a lane.

## Shared memory first, then local memory

- **The shared memory has a ceiling for each register count, and local memory starts at it**: 4224 bytes at 80
  registers, 4992 at 96, 6272 at 128. All 47 pipelines with local memory hold exactly their count's ceiling of shared
  memory, and none under it holds any. NVIDIA describes the mechanism: "the compiler prioritizes spilling registers into
  shared memory", and "any remaining spills fall back to local memory", which "resides off-chip in device global memory"
  ([NVIDIA](https://developer.nvidia.com/blog/how-to-improve-CUDA-kernel-performance-with-shared-memory-register-spilling/)).
- **The ceilings are the SM's shared memory over the warps the register count runs**: 99 KB a block over 24, 20 and 16
  warps, rounded down to the 256-byte step (eight bytes a lane). An inference that fits the three; the driver documents
  none of it.
- **The compiler takes local memory over more registers**: `direct dark` holds 80 registers, 4224 bytes and 16 of local
  memory, which 96 registers and their 4992 bytes would hold with none.
- **A tuple with less in it compiles to fewer registers and more shared memory**, and the maps decide it in most stages
  (`visibility` 96 registers and up to 2944 bytes without maps, 128 and none with them). So a tuple with code taken
  out is not held by the figures of the tuple it was taken from.

## What a stage holds a lane

Registers plus shared and local memory's words a lane: `unbounced` 77–80, `bounce` 80–122, `landing` 104–130,
`visibility` 106–128, `direct` 112–148, `water` 132–150, `media` 170–180, `panes` 180–210. `panes` holds the most on
NVIDIA as on AMD's driver (`2026-11-12-panes-column-ratio.md`); its two pane stacks, the arms' and the world's, are 19
words each through every traversal. Holding a ray as origin, direction, reach and flags took only a word off: the
compiler already keeps a ray in few.

## What it costs: nothing a bench shows

`panes` brought from 168 registers to 128, 112 bytes of local memory taken off and 2560 bytes of shared memory taken
off, in turn, five legs a side at 1920x1080: the zone moved by −0.2 to +0.4%, inside its legs' spread, and the frame
with it. A spill on NVIDIA, held in shared memory first, is not the cost it is on AMD's drivers, so a stage's scratch
there is reported against what its driver holds at no cost.
