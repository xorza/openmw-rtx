# A passage ray held as a narrow record

Measured 2026-11-09. A shadow or an occlusion ray a stage waits on held its whole `SceneQuery`, sixteen words, from
where it was asked to where it was traced; held as a record of eight — where it leaves, how far, which way, its flags —
and made into its query at the site that traces it, no picture moved on the vanilla content or the PBR replacers.

| kernel, tuple | AMD's Windows driver, registers | NVIDIA, shared memory |
|---|---|---|
| `direct`, whole | 144 → 160 | 1920 → 1664 bytes |
| `direct`, without the dark alpha's read | 160 → 160 | 1664 → 1408 bytes |
| `unbounced`, without the dark alpha's read | 91 → 93 | 0 → 0 |

On AMD's driver the record cost `direct` a wave on an RDNA SIMD, six where it ran seven; on NVIDIA it saved a quarter of
a kilobyte of each workgroup's shared memory. The record was not kept.
