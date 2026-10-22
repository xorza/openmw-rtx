# A footprint read that hung the trace on a Radeon 680M — 2026-10-22

Radeon 680M (RDNA 2, integrated), AMD proprietary driver 26.8.1, Windows 11. The full flavour,
`rtx-gpu-tests`, a 33x33 frame. A hang is a frame that did not complete within 10 s.

## The case

- **`RtxVisibilityTest.aGlossyArmReflectsThroughTheArmsOwnCone` hung on every run**, alone or in its
  shard; in `./omw gate` the other shard's process then waited behind it at `tone`.
- **The buffer markers stopped at `trace`**, and with only the first stage recorded the frame still
  hung: `visibility.comp`.
- **It hung under the maps variant on a first-person surface, and in no other scene built**: a
  first-person pane with a specular map, a normal map, a normal and a specular map, or a parallax
  normal map hung. The same specular pane placed in the world, a first-person pane with no map, and
  a specular first-person pane covering the whole frame did not hang.
- **The arms' own eye, the wall behind, and the subgroup request did not matter**: the arms' eye left
  unset, the wall removed, full subgroups not required, and the driver's own subgroup size each hung
  as before.
- **Under GPU-assisted validation the frame completed**, with no error reported.

## The read

- **Without `nonuniformEXT` on `texturesAlong` in `sampleDiffuse` the frame completed**; without it
  on `textures` alone, or on `companions` alone, it hung.
- **Without it at the two specular-map reads alone** (`resolve`), the test passed whole.
- **A waterfall in GLSL over `subgroupBroadcastFirst`** at those reads passed; with
  `nonuniformEXT` on the broadcast index, it hung again.
- **Clamping the slot to `TEXTURE_NEUTRAL`** at the footprint read did not help.
- **The driver's code for the read is the nested waterfall it emits for a combined image sampler**:
  an outer loop over the image descriptor and an inner one over the sampler, keyed on one register,
  each turn taking the first active lane's group. Read through
  `VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR`; nothing in it was found that does
  not end.
- **The specular maps read at the long axis's level through `textures` (`atLevel`)** passed every
  case above, and the GPU suite but `noKernelSpillsPastItsCeiling`, which fails the same on the
  shaders before it. So did a normal and an emissive map on the first-person pane, which was not
  run without it.
