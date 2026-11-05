# perf-rtx — the ray tracer's frame on three hosts

Target: the ray tracer's frame (the GPU's stages and passes, and the host's walk), on the desk (a
high-end NVIDIA laptop GPU), an AMD handheld (RADV, its clock held) and a laptop with a low-end AMD
integrated GPU (AMD's Windows driver). Started from `f361c9022c`.

## Hosts

- **Desk**: `./omw release bench` (default suite, 1920x1080, 1280x720 traced), three interleaved rounds
  after a warm-up leg. A/A (2026-11-02): frame medians within
  ±1 % a pair and ±0.1 % over three; zones ±1–2 %, `direct` bimodal (0.42 or 0.45 ms, dawn 0.67 or
  0.77); p99 ±15 %.
- **Handheld**: release build in the release-base container, staged by `omw release remote`, then run
  under omw's own remote script, 1280x800 (853x533 traced). A/A: frame and every zone
  within 0.1 % (`panes` 0.3 %), p99 within 2 %.
- **AMD iGPU**: native MSVC release build, run in the logged-on session through a scheduled task:
  an SSH session's window has no surface a swapchain takes. 1920x1080.
- **GPU-bound everywhere**: `wait` is 4 of 6 ms on the desk and 77 of 88 ms on the handheld; the host's
  walk, place and upload are under 1 ms on the desk and 5 ms on the handheld.
- **Host rows**: the handheld's within 1 % (update 0.2 %), the AMD iGPU's within 1 %; the desk's move
  10–20 % between identical runs, so a host change is judged on the handheld and the AMD iGPU.
- **Build RAM peaks**: desk release from nothing 25.8 GiB (perf-trace), a build of cached objects
  0.4 GiB, the debug flavour with its tests from nothing 33.2 GiB; the container's from nothing
  24.3 GiB; the AMD iGPU's from nothing 9.9 GiB. One build at a time on each host.
- **AMD iGPU noise** (A/A, two rounds): frame within 0.2 % (dawn 1.1 %), zones within 0.2 % (dawn
  0.7 %), p99 ±18 %.
- **The AMD iGPU's host is slow**: walk 11.7, place 5.0, upload 4.2, update 23.4 ms a frame at the
  ship, against the handheld's 3.3, 0.85, 0.75, 6.2 — 3.5 times the handheld's CPU. Under the GPU's 115 ms it costs no frame
  yet.

## Numbers at the start (`f361c9022c`)

Handheld, median ms:

| place | frame | p99 | trace | bounce | landing | direct | air | tlas | upscale | media | clamp | filter0 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| seyda-neen-ship | 88.44 | 97.88 | 15.88 | 14.98 | 12.03 | 9.73 | 7.87 | 3.62 | 3.29 | 3.08 | 2.92 | 2.74 |
| seyda-neen-ship-dawn | 103.37 | 117.48 | 16.06 | 15.13 | 13.94 | 19.06 | 13.51 | 3.62 | 3.28 | — | 3.01 | 2.74 |
| balmora-mages-guild | 58.72 | 63.60 | 5.16 | 9.19 | 16.19 | 6.32 | 1.42 | 0.39 | 3.20 | — | 2.79 | 2.91 |

Desk, median ms: ship 6.04 (trace 0.68, landing 0.61, direct 0.44, upscale 0.44, bounce 0.38), dawn
6.52 (direct 0.72, trace 0.69, landing 0.67), guild 5.32 (landing 0.76, upscale 0.45, bounce 0.37).

## Tip against the start (`421d2dba38` → `2861dd2fd9`, 2026-11-02)

Frame median ms; the handheld two runs a side, the AMD iGPU two rounds, the desk four, interleaved.

| place | handheld | AMD iGPU | desk |
|---|---|---|---|
| seyda-neen-ship | 88.45 → 84.12 (−4.9 %) | 120.81 → 114.23 (−5.4 %) | 6.039 → 6.011 (−0.45 %) |
| seyda-neen-ship-dawn | 103.31 → 99.36 (−3.8 %) | 148.35 → 137.72 (−7.2 %) | 6.520 → 6.475 (−0.7 %) |
| balmora-mages-guild | 58.72 → 58.70 (0.0 %) | 90.30 → 90.22 (−0.1 %) | 5.323 → 5.356 (+0.6 %, GPU zones' sum 4.818 → 4.820) |

Host update at the ship and dawn: handheld −7.2 % / −7.1 %, AMD iGPU −8.5 % / −6.0 %. Gate clean; CI
https://github.com/xorza/openmw-rtx/actions/runs/41385212538 green.

## Ideas

| idea | evidence | verdict |
|---|---|---|
| The trace's water walk as a stage of its own, over the tiles where the eye met the sea | RADV: `visibility … sea` 192 VGPRs (5 waves), 12.8–14.6 k instructions, 20–37 spilled SGPRs, against 128 (8 waves) and 5.2–5.8 k without the sea; the integrated GPU 234–247 against 112–128. Upper bound, the walk cut (picture wrong): handheld ship trace 15.88 → 10.02 ms, VGPRs 128 | **kept `3dd8dc8363`** (Decisions) |
| The water's stage traces the peel again | the simplest form | rejected: desk frame +0.66 / +0.78 % (water 0.135 ms against 0.111 restored), handheld water 2.95 against 1.91 |
| The media stage's phases a loop each (eye ray, eye column, water walk to its leg, the leg's march) | RADV `media … sea` 256 VGPRs, 37 SGPRs spilled; the integrated GPU 256; one loop over every walk's record | **kept `3a439f944c`**: handheld media 3.06 → 2.52 ms (VGPRs 192, no spill), ship frame −0.7 %; AMD iGPU media −3.6 % (225 VGPRs, still four waves); desk media 0.118 → 0.103; frames elsewhere inside the noise; no picture moved |
| A moved reference no held cell holds is searched for once (`CellPlacer::moveReference`) | desk profile: `CellPlacer::standAt` 1.95 % of samples, about 8 % of the main thread, under `PhysicsSystem::moveActors`: every actor the physics moves searched every held cell, each move | **kept `2861dd2fd9`**: handheld update −7.6 % ship, −7.2 % dawn (−0.47 / −0.43 ms); AMD iGPU −8.1 % / −9.9 % (−1.9 / −2.2 ms); guilds unchanged; frames unchanged (GPU-bound) |
| The NVIDIA driver's `[vkrt] Analysis` thread | 47 % of the desk's CPU samples: `clock_gettime` and `pthread_rwlock` in a loop | not the renderer's: the driver's own thread, on a core of its own |
| `SceneDesc::orderLights` sorts ~917 lamps every frame | 0.4 % of the desk's samples | not tried: tens of microseconds, and the order is the total order the frame needs |
| Distant statics merged into a structure a cell (redesign) | at the ship 57 054 of 64 179 instances are distant statics, 3.12 M placed triangles (55 a placement) of 5.84 M; RADV rebuilds the top level whole every frame something moves (Mesa 26.2 `vk_acceleration_structure.c`: a top level is always HPLOC, an update only a bottom level's). Upper bound, the distant statics off (picture wrong), handheld: tlas 3.62 → 0.71 ms, frame −6.7 % ship, −18.1 % dawn (direct −40 %, air −27 %, landing −18 %), guild −0.5 % | spiked, decided below (Decisions); the spike's first finding: 41 180 of the statics are two-sided or flipped, so a group is keyed by its facing as well. Before the spike: needs a hit's instance as custom index + geometry index, facing groups (cull and flip are an instance's flags), gates and moved references rebuilding a cell's structure, and a hit computed in another space, which moves pictures by rounding |

## Decisions

- **The sea's walk is a stage of its own, over the tiles where the eye met the sea** (`3dd8dc8363`,
  `docs/rtx/architecture.md` "The sea's walk, a stage of its own", `research/2026-11-02-water-stage.md`).
  Spiked on `spike/water-stage` (local). Handheld frame −4.2 % ship, −3.8 % dawn; AMD iGPU −5.3 %, −7.0 %;
  desk and every interior inside the noise; no picture moved. The shape is the tree's own
  classify-and-dispatch (the media and the layers' tile lists, FidelityFX's reflections) and
  Laine, Karras and Aila's case against one kernel carrying every walk (2013). Refused: the peel
  traced again in the water's stage (desk +0.7 %); the split on AMD alone (two builds of one
  kernel, which the tree refuses).

- **Statics merged into one structure a cell: refused as spiked** (spike
  `spike/merged-statics`, local). A cell's settled statics (8192 units square, one mask, one facing
  class) become one bottom level whose geometries carry their placements (`transformData`); its
  instance's custom index names a run of a merged table, and a hit's instance row is that run's
  entry at the geometry index (`instanceRowOf`). At the ship 22–29 k rows went into 350 structures.
  handheld: frame −5.2 % ship, −7.8 % dawn, −1.5 % guild (tlas 3.62 → 0.46 ms, trace −11 to −17 %,
  bounce +3 to +4 %, direct +8 % at the ship and −4 % at dawn); desk: −0.2 % ship (inside the
  noise; direct +8 %, air +9 %), −1.6 % dawn, −3.0 % guild (bounce and trace −14 %); AMD iGPU:
  **+5.5 % ship** (bounce +20 %, direct +14 %, air +14 %), −6.0 % dawn, −2.2 % guild. **Refused:
  it regresses the integrated GPU's noon exterior**, where a cell's one structure mixes opaque and cutout
  geometry over 8192 units, and **the pictures move**: a merged hit is intersected in the structure's space, so its
  barycentrics and distance round otherwise; raw frames at a held exposure moved on 0.97 % of
  Balmora's pixels (5 past 5 %), but the denoised shots moved on 5–99 % of the pixels, worst
  165–202/255, which `shot` does not call the denoiser's noise. No instanced hit can be
  reproduced bit for bit from another space. What landing takes beyond the spike: groups made by
  the cell placer from its held cells and not by settled rows; a gate, an enable, a blacklist or a
  move rebuilding its cell's structure; the structures in the content budget with their refusals
  and compaction; and the user's word on a picture that moves by rounding. The shape is NVIDIA's
  advice to merge static geometry whose instances overlap
  ([Best Practices: Using NVIDIA RTX Ray Tracing](https://developer.nvidia.com/blog/best-practices-using-nvidia-rtx-ray-tracing/)) and the
  `transformData` path the Vulkan specification gives a bottom level's geometries; refused before
  on the desk (`research/2026-10-22-instances-per-placed-model.md`, a top-level gain of 0.17 ms),
  where RADV's whole rebuild of the top level every frame was not measured.

## Tried on perf-trace (2026-10-31)

| idea | evidence | verdict |
|---|---|---|
| Sort landings by hit position | coherent-landing spike: landing −3.4 / −2.9 ms on the handheld, −24 % / −18 % on the desk | decided below (Decisions): refused |
| Rate the landing's two rays indoors | each ray 6–9 ms of landing; `look.h` refuses indoor rates by design | picture move not slight: document only |
| p99 is twice the median on the handheld | one present in 31 blocked 58–60 ms in Mesa's headless swapchain under SDL's offscreen driver | fixed in the harness (`bfecf2c3c0`): `omw remote` runs on the handheld's Wayland session; not the renderer's |
| Presenter: a fence per acquire slot in place of the wait on an image's last blit | the wait it removed moved into `vkQueuePresentKHR` | rejected: no gain, the stall was the WSI's |
| A drawn shadow bit's ray stops at the first occluder | its penumbra is `SHADOW_PENUMBRA_DRAWN` whatever stopped it | kept `9ff5929cda`: handheld dawn direct −22.8 %, desk dawn direct −9.9 %, pictures identical |
| Cap the nearest search at the distance where the penumbra saturates the shadow filter (4 footprints), then any-hit beyond | an occluder past it filters as one at it | rejected by analysis: an open ray then walks two traversals, and open sky rays are most of them at noon |
| `direct` at dawn 34 ms on the handheld | 12.7 ms at noon; every sky bit drawn at dawn and each traced to its nearest occluder | answered by `9ff5929cda`; 25.4 ms left |
| Update the top level in place where only transforms and refits moved | `tlas` 3.45 ms every exterior frame on the handheld, 0.40 on the desk, rebuilt whole | kept `7b147fc972`: desk tlas 0.40 → 0.09, frame −3.7 to −3.9 % outside; handheld unchanged (RADV's update costs its build) |
| Every cutout opaque (upper bound, picture not acceptable) | 39 905 cutouts at the ship; every ray asks each cutout candidate's alpha | measured: handheld frame −6.9 % ship, −6.1 % guild; desk ship −9 %. Next: opaque cutouts for indirect rays alone, and a coarser alpha level for them |
| Cutouts solid for the landing's two rays alone (for review) | the landing asks each cutout candidate's alpha for its ambient and its lamp ray | **not landed, picture move not slight**: handheld frame −2.0 % ship, −2.6 % guild (landing −10.2 % / −7.9 %); desk shot: 45 of 70 pictures moved, seyda-neen-boulder 57/255 on 53 % of the pixels, the ship 30/255 on 16 %. |
| One moon ray a froxel only where the sun is down | thought the dawn air paid a moon's ray beside the sun's | rejected: no picture moved and the handheld's air did not move (13.39 against 13.39 ms): the moons fall under `FOG_SHAFT_FLOOR` at dawn already; the dawn air is the low sun's grazing ray |
| RADV's wave64 for the ray tracing launches (`RADV_PERFTEST=rtwave64`) | the fog's two launches are the only `vkCmdTraceRaysKHR` work | rejected: handheld dawn air 13.29 → 16.75 ms |
| `FFX_HALF` for the upscaler | RDNA 2 runs packed halves | not retried: `fsrcallbacks.glsl` records it moved a still picture 0.30–0.58 px |
| The wavelet's per-tap divisions as multiplies by a reciprocal | the first level is ALU-bound (VALU 91 %) | not tried: the taps are already tiled in shared memory with the facing power as squarings; what is left per tap is two `exp`, a `sqrt` and two divisions, a few per cent of a 2.9 ms zone at most, and a reciprocal rounds differently |
| The landing's and the direct stage's light worked out before their rays, the rays at one site in a fixed order (live state across the traversal) | RADV: landing 160–256 VGPRs, 4–6 waves, VALU 46 % busy and the lowest cache hit ratios: latency-bound; direct 128–192 | kept: handheld landing −34 % ship, −34 % dawn, −21 % guild, direct −28 / −28 / −9 %, frame −10.3 / −12.8 / −7.5 %; desk landing −9 / −25 / −30 %, direct −17 / −12 / −15 %, frame −2.2 / −4.8 / −6.8 %. VGPRs: landing 128–144, direct 112–128. No picture moved (`shots/c2`), repeat 10 pairs identical, 472 GPU tests |
| A layer's light worked out before its rays, one `PathEndLight` for the landing and the layer (`seenPaneOf`); `VisitRun` and the eye walk's `EYE_PANE` step gone | panes 256 VGPRs with spills | kept: handheld panes −17.3 % guild, −0.5 / −1.5 % outside (256 → 192 without the sea; 256 with it, spills 37–45 → 0–11); desk panes −4.9 / −4.0 / −8.0 %; no picture moved (`shots/c4`) |
| The bounce's escape worked out before its ray, so the miss keeps 3 numbers and not 10 | bounce 96 VGPRs, pre-scheduling 80; 88 would give 11 waves | not kept yet: RADV still allocates 96 (pre-scheduling 80 again), yet the handheld's bounce went −0.5 to −0.6 % at every place (one run each); the desk not measured |

### Decisions on perf-trace

- **The landings stay in the order the bounce lists them; no sort by where they hit.** Spiked on
  `spike/landing-sort` (local): a global counting sort by the instance met cost the desk's landing
  +40 % to +80 % (77 bins indoors, every landing an atomic on a few of them); a sort by world cell
  (96 units, Morton over a 16-cell block) in shared memory, 1024 landings a block, both runs, gave
  the handheld landing −6.7 % guild, −5.3 % ship, −4.5 % dawn (frame −2.1 / −1.0 / −0.8 %) and the desk
  landing −1 % indoors but +9 to +10 % outdoors, the pictures identical. Refused: it regresses the
  desk, and a path for software traversal alone (RADV's RDNA 2) would buy 1–2 % of the handheld's frame
  for a pass, a list and a vendor switch. Reordering secondary rays by origin is the established
  approach (Pharr and Hanrahan's coherence work, extended to GPUs; Garanzha and Loop's
  compress-sort-decompress; the overhead such a sort carries ([Generate Coherent Rays Directly](https://arxiv.org/pdf/2310.07182))).
- **Cutouts stay tested in the shader; no opacity micromaps (asked again 2026-10-31, refused again).**
  Spiked on `spike/opacity-micromap` (local): 4-state micromaps baked on the host from the finest
  alpha level, conservative over every texel a bilinear read inside a microtriangle can take
  (`uv * size - 0.5`), one microtriangle about 8 texels (level ≤ 6) or 2 texels (level ≤ 9), special
  indices for whole triangles, `DISABLE_OPACITY_MICROMAPS` on a placement not wearing the baked
  material. At the ship 901 micromaps over 4 987 triangles, 1.0 MiB (2.8 MiB at level 9), baked in
  0.27 s at load (0.58 s); 20 % clear, 33 % opaque, 47 % unknown microtriangles (36 % at level 9).
  Desk, the same build on and off, three interleaved rounds: bounce −2.0 to −3.3 %, air −2 to −4 %,
  landing −1 to −2.6 %, but trace +1.7 to +4.5 % and, at dawn, direct +10 % and the shadow filter
  +8 %; frame +0.3 % ship, +3.3 % dawn, +0.6 % guild, −0.2 % vivec, +0.7 % pond — no gain past the
  noise, and a loss at dawn. Pictures: a slight move where a coned ray reads a coarser level than the
  bake (sparse dots in far canopies and water glints, 0.03 % of the pixels past 2/255), which the
  OMM SDK's guide names ("do not use dynamic mip level"). The handheld gains nothing: RADV
  offers no `VK_EXT_opacity_micromap`; the desk's driver offers it and `VK_KHR_opacity_micromap`.
  Refused: the gain is not there on the desk and the handheld cannot have it. Sources: the Vulkan
  specification's ray traversal chapter (micromap states against the instance flags),
  [NVIDIA OMM SDK](https://github.com/NVIDIA-RTX/OMM) and its integration guide.

