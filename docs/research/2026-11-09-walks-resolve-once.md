# The water and media walks resolve their surface once

Measured 2026-11-09 on a low-end AMD integrated GPU under AMD's Windows driver, through
`RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling`, which logs each pipeline's `numUsedVgprs` and
`scratchMemUsageInBytes` for a 32-lane wave. Before: the walk resolved the surface of every hit it met, and carried the
peel's arms, layer, start, the through-shares and the water's hit through the loop. After: the walk carries where each
hit lies, and the stage reads the peel back from its record and resolves the last hit once, after the walk.

| kernel | tuple | before | after |
|---|---|---|---|
| water | whole | 248 | 190 |
| water | without the dark alpha's read | 218 | 177 |
| media | whole | 248 | 216 |
| media | without the dark alpha's read | — | 192 |

No kernel used scratch memory before or after but `panes`, at 80 bytes a lane in both tuples. The eye's `visibility`
(145), `direct` (160), `landing` (160) and `bounce` (144) did not move: `kernels --against` named the 128 water-stage
tuples of `visibility.comp` and the 80 tuples of `media.comp`, and no other shader.

Pictures: `shot --views=all --map --upscale=off` on vanilla content and on the PBR replacers' profile, before and
after, moved no picture of the trace or the scene on a high-end NVIDIA laptop GPU.
