# Where NVIDIA's compiler keeps what a stage's registers do not hold — 2026-11-14

A high-end NVIDIA laptop GPU (Ada, compute capability 8.9), driver 620.70.06, Linux. The debug flavour at
`a65ea95335`, `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` with the log at its debug level, which
prints each pipeline's statistics a line: the shipped modules, every tuple, 246 pipelines the driver reports an
executable of. Registers are `Register Count`, shared memory `Shared Memory Size` in bytes a workgroup of 32
lanes, local memory `Local Memory Size` in bytes a lane.

## The shared memory has a ceiling a register count, and local memory starts at it

| registers | pipelines | most shared memory | with local memory | of those, shared memory at the most |
|---|---|---|---|---|
| 80 | 18 | 4224 | 7 | 7 |
| 96 | 62 | 4992 | 3 | 3 |
| 128 | 123 | 6272 | 37 | 37 |
| 168 | 13 | 5504 | 0 | — |

A kernel of any other register count (28, 72 to 79, 91, 116 to 127) holds no local memory, and no shared
memory but `fog integrate`'s 1792 bytes and `lamp lists`'s 1584. Shared memory steps by 256 bytes from 128:
eight bytes a lane.

- **Local memory is what the shared memory could not take.** All 47 pipelines with local memory hold exactly
  the most shared memory any pipeline of their register count holds, and no pipeline under that figure holds
  any. NVIDIA describes the mechanism for CUDA 13: "the compiler prioritizes spilling registers into shared
  memory", and "any remaining spills fall back to local memory"
  (<https://developer.nvidia.com/blog/how-to-improve-CUDA-kernel-performance-with-shared-memory-register-spilling/>),
  with 5 to 10% gained on spill-heavy kernels over local memory alone, which "resides off-chip in device global
  memory".
- **The three figures are the SM's shared memory over the warps the register count runs.** An SM of this card
  holds 64 K registers and 100 KB of shared memory, 99 KB of it a block's
  (<https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html>). 99 KB over 24,
  20 and 16 warps is 4224, 5068 and 6336 bytes, and the figures are those rounded down to the 256-byte step:
  4224, 4992, 6272. The warps are four times what 16 K registers hold of 32 lanes at 80, 96 and 128 registers
  each: 6, 5 and 4. An inference from the figures, which fits the three; the driver documents none of it. At 168
  registers it gives 12 warps and 8320 bytes, which no kernel reaches.
- **The compiler takes local memory over more registers.** `direct dark` holds 80 registers, 4224 bytes and 16
  of local memory, 116 words a lane in all, which 96 registers and their 4992 bytes hold with none.

## What a stage holds a lane, wherever it is kept

Registers, the shared memory's words a lane (`(bytes − 128) / 128`) and local memory's, summed, over a stage's
tuples:

| stage | pipelines | registers | most shared memory | most local memory | words a lane |
|---|---|---|---|---|---|
| `unbounced` | 12 | 77 to 80 | 0 | 0 | 77 to 80 |
| `bounce` | 12 | 80 to 122 | 384 | 0 | 80 to 122 |
| `landing` | 48 | 96 to 128 | 3968 | 0 | 104 to 130 |
| `visibility` | 48 | 96, 128 | 3712 | 0 | 106 to 128 |
| `direct` | 48 | 80, 96, 128 | 4992 | 48 | 112 to 148 |
| `water` | 24 | 128 | 2944 | 0 | 132 to 150 |
| `media` | 4 | 128 | 6272 | 16 | 170 to 180 |
| `panes` | 48 | 128, 168 | 6272 | 112 | 180 to 210 |

- **`panes` holds 180 to 210 words in every tuple**, the most of any stage, as it holds the most registers on
  AMD's driver, 254 (`2026-11-12-panes-column-ratio.md`). The whole tuple has no local memory because it was
  compiled to 168 registers; 35 of its pipelines to 128, with 16 to 112 bytes over the 6272.
- **A tuple with less in it is compiled to fewer registers and more shared memory**, and the maps decide it in
  most stages: `visibility` 96 registers and 1408 to 2944 bytes without maps, 128 and none with the sea and the
  maps; `landing` 96 and 1152 to 3968 without, 126 to 128 and 384 at most with; `bounce` 80 to 91 registers
  without, 116 to 122 with; `direct` 80 or 96 without, 128 with a relief. So a tuple with code taken out is not
  held by the figures of the tuple it was taken from.
- **By the split**: 35 `panes`, 10 `direct` and 2 `media` pipelines hold local memory. The modules that count
  (the census) differ in a few: `panes sun sea` holds 128 registers and 96 bytes there, 168 and none shipped.

## What lowers what `panes` holds

Each change on top of the one before it, the shipped modules of every tuple compiled again and read as above; a
stage no change touched moved by nothing in any tuple. Words a lane over the 48 `panes` pipelines:

| change | words a lane | moved by, mean (range) | at 168 registers | with local memory, most |
|---|---|---|---|---|
| `4a983b9987` | 180 to 210 | | 13 | 35, 112 bytes |
| the path end's and the direct stage's shadow rays held as seven numbers, each query made at its turn | 172 to 208 | −2.0 (−8 to +4) | 5 | 40, 112 |
| the stage record read again at the end | 170 to 204 | −3.0 (−8 to +4) | 5 | 39, 112 |
| the peel's ray made again from the pixel at each use | 166 to 200 | −2.5 (−8 to 0) | 0 | 44, 96 |
| a layer's glow and response worked out before its rays | 166 to 200 | −0.9 (−6 to 0) | 0 | 41, 96 |
| one pane stack for the arms' and the world's, an upper bound: wrong under see-through arms | 156 to 184 | −16.7 (−26 to −10) | 0 | 12, 32 |

- **The two stacks are what `panes` carries**: 19 words each through every traversal, the eye's and a layer's
  four rays', of which the arms' is nought wherever the arms are not seen through.
- **The compiler already keeps a ray in few words**: holding one as its origin, direction, reach and flags took
  1.0 word off `direct` at the mean and 0.5 off `landing`.
- **Fewer words made more pipelines hold local memory**, 35 to 44, until the stacks went: the compiler took the
  tuples at 168 registers to 128. The count of pipelines with local memory is not what a change is read by.

## What it costs: nothing a bench shows in `panes`

The last row against `4a983b9987`, the release harness, `bench` at 1920x1080 with the default upscale (1280x720
traced), the default suite on vanilla, 20 s a place: a warm-up leg a side, then five legs a side in turn, the
two sides' shader files copied in between legs. The card ran at 1764 MHz at the mean under its power cap. The
median of the legs' medians, and how far each side's legs' medians spread:

| place | its `panes` tuple, registers / local / shared | `panes`, ms | spread, ms |
|---|---|---|---|
| `seyda-neen-ship` | 168 / 0 / 4736 → 128 / 0 / 5504 | 0.0735 → 0.0737 (+0.3%) | 0.0012, 0.0012 |
| `seyda-neen-ship-dawn` | 128 / 112 / 6272 → 128 / 0 / 6016 | 0.0908 → 0.0912 (+0.4%) | 0.0006, 0.0011 |
| `balmora-mages-guild` | 128 / 16 / 6272 → 128 / 0 / 3712 | 0.1114 → 0.1112 (−0.2%) | 0.0021, 0.0022 |

Neither 112 bytes of local memory taken off, nor 168 registers brought to 128, nor 2560 bytes of shared memory
taken off moved the zone past its legs' spread. The frame's medians stood at 6.11 → 6.05, 6.54 → 6.54 and
5.39 → 5.40 ms. The guild's `panes` p99 rose 0.132 → 0.162 ms beside `bounce`'s 0.551 → 0.664, a kernel no
change touched.

## Not measured

`direct`'s tuples with local memory, which no bench place of vanilla's runs: those without maps and away from
the sea under the sun or the moons, and those of a dark map's alpha. RADV and AMD's driver, in the tuples besides
the two their ceilings were measured in and under any of the changes above.
