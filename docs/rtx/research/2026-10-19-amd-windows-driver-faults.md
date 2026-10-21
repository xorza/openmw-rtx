# What AMD's Windows driver answers wrong — 2026-10-19

Radeon 680M (RDNA 2, integrated), Adrenalin 26.8.1 ("AMD proprietary shader compiler"), Windows 11.
What the driver answered wrong while the trace was rewritten around it, and what the trace does in
its place. The same driver's compile memory is in `2026-10-19-amd-windows-trace-compile-memory.md`.

## A ray query during a closest-hit shader fails on this driver

With the traversal in a callable stage, and the launch compiling (`2026-10-19-amd-windows-trace-compile-memory.md`, "The traversal in a callable stage"), every frame of the game lost the device ("an invalid read", one stable
address per build), and the GPU tests that cast a shadow from the hit shading failed: the floor
under an occluder stayed lit (`theSunLightsWhatItFacesAndTheOccluderTakesItAway` and all four of
`RtxPenumbraTest`). Core and synchronization validation reported nothing before the loss; GPU-AV's
own submit failed with the device already lost.

- The callable stage receives the request whole and returns the answer whole: forced answers came
  back as written, and the mask arrived as the shadow ray's own, 0x39.
- The shadow query missed the occluder at mask 0x39, at 0x01 alone and with every triangle forced
  opaque; at 0xFF it committed something, and the frame then faulted.
- A query that committed and read its triangle's vertices back
  (`rayQueryGetIntersectionTriangleVertexPositionsEXT(query, true, …)`) faulted; without the read
  it did not.
- The same queries from a ray generation stage are right: the fog's lid tests
  (`aLampBehindALidLightsNoneOfTheVolumesAirEither`, `aLidOverTheMarchTakesTheSunOutOfTheAirBeneathIt`)
  pass with `queryScene` inline in `fogscatter.rgen`, and pass with it run through the callable
  stage from there.
- An inline query in the hit stage, before the callable stage existed, lost the device as well.

## A ray generation stage draws the walk wrong (2026-10-20)

The walk, one loop over every ray a pixel casts (`2026-10-19-amd-windows-trace-compile-memory.md`, "The walk: one traversal site").
As a ray tracing pipeline the launch draws wrong pictures: `theSunLightsWhatItFacesAndTheOccluderTakesItAway`
reads the lit wall right in its first frame and no hit at all in every later one, and the result
moves from run to run. With the shading cut out of the module (the eye's hit answered flat, the
compile 2.4 s) every frame hits. Not the cause: `maxPipelineRayRecursionDepth` 0 or 1; every lane
running the query in every turn of the loop; the pipeline's stack size set at each launch from the
group's own. The same module as a compute dispatch passes the sun test and the penumbra tests.

## A confirmed triangle's committed corners (2026-10-20)

With the walk as a compute dispatch, a wall with a cutout mask (`AlphaMode::Cutout`, so traversal
hands it to the candidate loop, which confirms it) shaded with the normal `(0, 0, -1)` and was lit
from behind; the same wall unmasked shaded with its own `(0, -1, 0)`. The albedo view and the hit
count agreed for both. Taking the corners of a confirmed triangle from its candidate's
`rayQueryGetIntersectionTriangleVertexPositionsEXT(query, false, …)` in place of the committed
read made both walls and a doubled leaf read their expected light. As a ray tracing pipeline the
launch still met nothing after its first frame with this in place, so the two faults are apart.

**An opaque triangle's committed corners are wrong too, where the ray met another.** A floor with a
second sheet ten units under it (`aFloorInsideAFillIsLitFromEverySideAndShadowedByNothing`) shaded
as though it stood at the sheet: its bounce, which must be nought under a black sky, came to
0.048, and its lamp light rose from 0.0798 to 0.0875. With `gl_RayFlagsNoOpaqueEXT` on the rays a
surface is resolved from, every triangle passes the candidate loop, and the floor read 0.0798 and no
bounce with the sheet and without it.

**An eye in a triangle's plane is the driver's to answer.** The sky test's camera stood at the
origin, in the plane of the wall at y = 0, and every eye ray met the wall at a distance of nought,
where NVIDIA's driver reports a miss. The penumbra tests' camera stood at y = -100, in the plane of
the fill's occluder, and the fill's shadow came out wrong; the lamp rays' answers there were not
read apart. The spec leaves the case to the implementation: such a triangle has no area in the
ray's space, and the test is "performed in an implementation specific manner". Neither test needed
its camera in the plane, and moved off it both pass.

## Also seen

The driver counts a heap's use as the allocator's blocks alone, with nothing of its own beside
them; lists no `VK_EXT_pci_bus_info`; and rounds `packHalf2x16` toward nought, as its half store
does.
