# perf-trace — the ray tracer's frame on the desk and on the Deck

Target: the GPU frame of the ray tracer (`components/rtxvulkan/shaders/trace`, the stages and the
passes around them), measured with `bench` on this desk (RTX 4090 Laptop, driver 615.78.08) and on
the Steam Deck (`omw release remote deck@192.168.0.31`, RADV VANGOGH, the clock held at 1600 MHz).
Started from `901a138f2c`. Worktrees: `../openmw-perf/base` and `../openmw-perf/cand`.

A picture that moves slightly is acceptable where the gain is real; a move that is not slight is
documented here for review and not landed.

## Hosts

- **Desk**: release build, `./omw release bench`, base and cand interleaved, three rounds after a
  throwaway leg. Noise floor from an A/A (2026-10-31): adjacent pairs within about 1 % on a zone and
  a frame, and every zone drifting up 2–3 % over the run as the card warms, so only adjacent pairs
  are compared.
- **Deck**: release build in the release-base container, `./omw release remote deck@192.168.0.31
  bench`. Runs repeat to 0.01 ms in every zone (2026-10-31, the default suite twice).
- **Build RAM peak (desk)**: 25.8 GiB for a release build from nothing (`omw release build
  openmw-rtxtool`); two at once do not fit with margin in 62 GiB, so builds run one at a time.

## Numbers at the start (Deck, 1280x800, upscale quality)

| place | frame median | p99 | landing | trace | bounce | direct | air |
|---|---|---|---|---|---|---|---|
| balmora-mages-guild | 59.64 | 120.39 | 18.86 | 5.07 | 9.44 | 5.29 | 1.24 |
| seyda-neen-ship | 100.23 | 199.58 | 19.53 | 17.41 | 15.36 | 12.75 | 7.79 |
| seyda-neen-ship-dawn | 123.51 | 256.42 | 22.91 | 16.62 | 15.43 | 34.44 | 13.37 |

## Tip against the start (`901a138f2c` → `7b147fc972`)

| place | Deck frame | Deck p99 | desk frame |
|---|---|---|---|
| balmora-mages-guild | 59.63 → 59.37 (−0.4 %) | 66.84 → 66.67 | 4.983 → 4.848 (−2.7 %) |
| seyda-neen-ship | 100.33 → 99.60 (−0.7 %) | 108.98 → 108.21 | 5.732 → 5.415 (−5.5 %) |
| seyda-neen-ship-dawn | 125.83 → 117.18 (−6.9 %) | 149.13 → 130.89 (−12.2 %) | 6.405 → 6.028 (−5.9 %) |
| balmora | — | — | 4.730 → 4.415 (−6.7 %) |

Desk: three interleaved rounds, the pairs within 2 % of each other. Deck: one run each, which
repeats to 0.01 ms.

## Ideas

| idea | evidence | verdict |
|---|---|---|
| Sort landings by hit position | coherent-landing spike: landing −3.4 / −2.9 ms on the Deck, −24 % / −18 % on the desk | decided below (Decisions): refused |
| Rate the landing's two rays indoors | each ray 6–9 ms of landing; `look.h` refuses indoor rates by design | picture move not slight: document only |
| p99 is twice the median on the Deck | one present in 31 blocked 58–60 ms in Mesa's headless swapchain under SDL's offscreen driver | fixed in the harness (`bfecf2c3c0`): `omw remote` runs on the Deck's Wayland session; not the renderer's |
| Presenter: a fence per acquire slot in place of the wait on an image's last blit | the wait it removed moved into `vkQueuePresentKHR` | rejected: no gain, the stall was the WSI's |
| A drawn shadow bit's ray stops at the first occluder | its penumbra is `SHADOW_PENUMBRA_DRAWN` whatever stopped it | kept `9ff5929cda`: Deck dawn direct −22.8 %, desk dawn direct −9.9 %, pictures identical |
| Cap the nearest search at the distance where the penumbra saturates the shadow filter (4 footprints), then any-hit beyond | an occluder past it filters as one at it | rejected by analysis: an open ray then walks two traversals, and open sky rays are most of them at noon |
| `direct` at dawn 34 ms on the Deck | 12.7 ms at noon; every sky bit drawn at dawn and each traced to its nearest occluder | answered by `9ff5929cda`; 25.4 ms left |
| Update the top level in place where only transforms and refits moved | `tlas` 3.45 ms every exterior frame on the Deck, 0.40 on the desk, rebuilt whole | kept `7b147fc972`: desk tlas 0.40 → 0.09, frame −3.7 to −3.9 % outside; Deck unchanged (RADV's update costs its build) |
| Every cutout opaque (upper bound, picture not acceptable) | 39 905 cutouts at the ship; every ray asks each cutout candidate's alpha | measured: Deck frame −6.9 % ship, −6.1 % guild; desk ship −9 %. Next: opaque cutouts for indirect rays alone, and a coarser alpha level for them |
| Cutouts solid for the landing's two rays alone (for review) | the landing asks each cutout candidate's alpha for its ambient and its lamp ray | **not landed, picture move not slight**: Deck frame −2.0 % ship, −2.6 % guild (landing −10.2 % / −7.9 %); desk shot: 45 of 70 pictures moved, seyda-neen-boulder 57/255 on 53 % of the pixels, the ship 30/255 on 16 %. Pictures in `../openmw-perf/shots/cand3` against `shots/base` |
| One moon ray a froxel only where the sun is down | thought the dawn air paid a moon's ray beside the sun's | rejected: no picture moved and the Deck's air did not move (13.39 against 13.39 ms): the moons fall under `FOG_SHAFT_FLOOR` at dawn already; the dawn air is the low sun's grazing ray |
| RADV's wave64 for the ray tracing launches (`RADV_PERFTEST=rtwave64`) | the fog's two launches are the only `vkCmdTraceRaysKHR` work | rejected: Deck dawn air 13.29 → 16.75 ms |
| `FFX_HALF` for the upscaler | RDNA 2 runs packed halves | not retried: `fsrcallbacks.glsl` records it moved a still picture 0.30–0.58 px |
| The wavelet's per-tap divisions as multiplies by a reciprocal | the first level is ALU-bound (VALU 91 %) | not tried: the taps are already tiled in shared memory with the facing power as squarings; what is left per tap is two `exp`, a `sqrt` and two divisions, a few per cent of a 2.9 ms zone at most, and a reciprocal rounds differently |
| The landing's and the direct stage's light worked out before their rays, the rays at one site in a fixed order (live state across the traversal) | RADV: landing 160–256 VGPRs, 4–6 waves, VALU 46 % busy and the lowest cache hit ratios: latency-bound; direct 128–192 | kept: Deck landing −34 % ship, −34 % dawn, −21 % guild, direct −28 / −28 / −9 %, frame −10.3 / −12.8 / −7.5 %; desk landing −9 / −25 / −30 %, direct −17 / −12 / −15 %, frame −2.2 / −4.8 / −6.8 %. VGPRs: landing 128–144, direct 112–128. No picture moved (`shots/c2`), repeat 10 pairs identical, 472 GPU tests |
| A layer's light worked out before its rays, one `PathEndLight` for the landing and the layer (`seenPaneOf`); `VisitRun` and the eye walk's `EYE_PANE` step gone | panes 256 VGPRs with spills | kept: Deck panes −17.3 % guild, −0.5 / −1.5 % outside (256 → 192 without the sea; 256 with it, spills 37–45 → 0–11); desk panes −4.9 / −4.0 / −8.0 %; no picture moved (`shots/c4`) |
| The bounce's escape worked out before its ray, so the miss keeps 3 numbers and not 10 | bounce 96 VGPRs, pre-scheduling 80; 88 would give 11 waves | not kept yet: RADV still allocates 96 (pre-scheduling 80 again), yet the Deck's bounce went −0.5 to −0.6 % at every place (one run each); the desk not measured |

## Decisions

- **The landings stay in the order the bounce lists them; no sort by where they hit.** Spiked on
  `spike/landing-sort` (local): a global counting sort by the instance met cost the desk's landing
  +40 % to +80 % (77 bins indoors, every landing an atomic on a few of them); a sort by world cell
  (96 units, Morton over a 16-cell block) in shared memory, 1024 landings a block, both runs, gave
  the Deck landing −6.7 % guild, −5.3 % ship, −4.5 % dawn (frame −2.1 / −1.0 / −0.8 %) and the desk
  landing −1 % indoors but +9 to +10 % outdoors, the pictures identical. Refused: it regresses the
  desk, and a path for software traversal alone (RADV's RDNA 2) would buy 1–2 % of the Deck's frame
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
  OMM SDK's guide names ("do not use dynamic mip level"). The Deck gains nothing: RADV (Mesa 26.2)
  offers no `VK_EXT_opacity_micromap`; the desk's driver offers it and `VK_KHR_opacity_micromap`.
  Refused: the gain is not there on the desk and the Deck cannot have it. Sources: the Vulkan
  specification's ray traversal chapter (micromap states against the instance flags),
  [NVIDIA OMM SDK](https://github.com/NVIDIA-RTX/OMM) and its integration guide.

