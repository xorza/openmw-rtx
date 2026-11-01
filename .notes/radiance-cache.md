# Proposal: a radiance cache for the bounce

Status: proposal, 2026-11-01, split off the bounce rays' plan. The traced depth it was to be judged
against was refused (`docs/rtx/research/2026-11-01-bounce-rays.md`, "A deeper path, refused"); one
bounce with its rays is the reference. When it lands, each lasting decision moves into
`docs/rtx/architecture.md`, and this file is deleted.

## The field

NVIDIA's NRD sample traces one bounce and takes the rest of the path from two caches: L1, the
previous frame's composed light where the hit projects on the screen (`ReprojectIrradiance`,
[`RaytracingShared.hlsli`](https://github.com/NVIDIA-RTX/NRD-Sample/blob/main/Shaders/RaytracingShared.hlsli)),
and L2, SHaRC's world-space hash grid
([integration](https://github.com/NVIDIA-RTX/SHARC/blob/main/docs/Integration.md)). Each frame
then carries the light of every frame before it.

## Outline

- **L1, the previous frame.** The composite also writes the frame's outgoing diffuse radiance
  without the fill (the lights by the albedo, and the glow), held one frame beside the held
  surface (`GBuffer::getHeld`). A landing projects its hit into the previous frame. It takes the
  held light where the held surface's depth agrees within 1 %, the hit faces the same side, and the
  point is inside the screen, as `ReprojectIrradiance` does. The path then stops there. Cost: one
  image the size of the trace (7 MB in RGBA16F at 1280×720), and one read for each landing.
- **L2, a world-space hash grid**, a port of SHaRC (Apache 2.0): an update pass over about 4 % of
  the pixels, a resolve pass over the table, and a query at each landing after L1 misses. 2²²
  entries at 40 B is 160 MiB. Every pass costs the same on every frame, which suits this tree's
  rule on uniform frame times.
- **Why L1 may come first.** L1 is one image and one read for each landing, and gives the light
  of every frame before it where the hit is on the screen, which in a room is most hits. A
  converged reference (`noise`) is what L1's bias is measured against.
- **What the user decides first:** whether the cache keeps the fill (the content's `AMBI`) out of
  what it stores, so the ambient is not multiplied again on each frame of feedback; whether its
  bias is acceptable against a converged reference (`noise`); and whether its determinism holds
  under `repeat`, since a hash insertion's slot depends on the order of the atomics.
