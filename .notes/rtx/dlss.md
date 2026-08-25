# The DLSS pipeline, and the order to put it right in

What Ray Reconstruction is currently given, what it is missing, and the sequence that gets it there.
Findings came from reading `components/rtxvulkan/dlss*.{hpp,cpp}`, `ngx.hpp`, the G-buffer channels
it reads and the frame wiring in `vulkanrenderer.cpp`, against NVIDIA's [DLSS-RR programming
guide](https://github.com/NVIDIAGameWorks/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md) and
the vendored SDK's `nvsdk_ngx_helpers_dlssd_vk.h`.

**The ordering is by dependency, not by size.** Steps 1 and 2 buy nothing on their own; everything
after them is unjudgeable without them, which is why they come first.

---

## Where it stands

**The material inputs are real.** Each shading path reports what it shaded with (`SurfaceResponse`
in `visibility.comp`), what the water and the air took rides on the light they attenuated rather
than on the surface they did not, and the specular channel carries water's Fresnel term rather than
an image cleared once and never written. That is the baseline everything below rests on.

What is left is everything about the frame *in front* of those surfaces. The network is still told
nothing about the sprites and fog composited over them, and it reprojects every one of those pixels
with the motion of whatever geometry stands behind them.

None of that is currently visible from a run: `--filter` and `--jitter` are both silently inert at
the default upscale, and nothing in a summary line says which denoiser produced the picture.

---

## 1. Make the reconstruction legible

**First because nothing after it can be judged.** Every step below changes the picture, and there is
currently no way to see that it did: the frame's summary reports a trace time and a hit fraction and
says nothing about which of the two denoisers ran, at what preset, or whether the wavelet was
silently skipped.

- Report the reconstruction in `shot`'s summary and in the game's periodic line — upscaler or
  wavelet or neither, the preset, and the render-to-output pair.
- `filtering = options.mFilter && !upscaling` and `jitter = options.mJitter || upscaling` make two
  command-line flags mean nothing unless a third is set a particular way. A run that asks for the
  wavelet and gets Ray Reconstruction should say so rather than quietly comply.
- Settle the three-way comparison that the rest of this document is argued from: one view at
  `--upscale=off --filter=false` (the reference), at `--upscale=off` (the wavelet), and at the
  default (Ray Reconstruction).

**Done when** a shot says what made it, and the same view renders three ways from one command each.

## 2. Pin which network is running

**Second because it moves every measurement taken after it.** No render preset is selected, so the
frame is reconstructed by whatever the installed feature library defaults to — which has changed
between SDK versions and again between the CNN and transformer models (`.notes/todo.txt` already
carries that question). Two machines, or one machine after a driver update, are not comparing the
same thing.

- Select a preset explicitly and record it wherever step 1 reports.
- The guide deprecates presets A–C; D or the default are what it names.

**Done when** two runs of the same view on different days are comparable, and the report says which
network answered.

## 3. The three cheap correctness items

**Third because each changes results and none needs anything built.** Doing them after step 1 means
the change is visible; doing them before step 4 means step 4 is measured against a correct baseline.

- `InReset` fires only when the previous camera basis is zero — a resize or a renderer rebuild. Its
  own doc comment names cell loads, teleports and cuts, and none of those reach it. The renderer
  already learns about scene replacement through `setScene`.
- `DlssPass::record` asserts the colour and output extents and not the other five. A guide, depth,
  motion or albedo at the wrong size is accepted silently — the same failure mode the header already
  warns about for `SAMPLED_BIT`.
- `ngxQualityOf` answers `Off` with `MaxPerf` and relies on a comment that `Off` never arrives.

**Done when** walking through a door stops ghosting, and a mismatched input is a failed assertion
rather than a wrong picture.

## 4. Hand over the two guides the frame already computes

**Fourth because it is the largest gain available with no new scope.** `visibility.comp` composites
fog and then a sprite layer over the finished frame, and holds both sides of each composite in local
variables before discarding the distinction. Those are exactly `pInColorBeforeFog` /
`pInColorAfterFog` and `pInColorBeforeParticles` / `pInColorAfterParticles`, which exist so that
transparent effects are not reconstructed as if they were the surface behind them.

This is where the rain lives. A rainstorm is a thousand sprites crossing the frame at their own
velocity, reprojected with the motion of the wall behind them; the guide pair is what tells the
network to stop.

- The two colour pairs, and `pInBiasCurrentColorMask` for the same population of pixels.
- Two more render-resolution images and the composite handing over what it already has.

**Done when** a `--weather=Rain` shot at the default upscale stops smearing drops, judged against
the three-way comparison from step 1.

## 5. Stop binding what nothing reads

**Fifth because step 4 settles which channels exist**, and doing it earlier means doing it twice.
None of this changes the picture; it is bandwidth and memory on the frame path.

- Depth is bound as `RG32F` because the second component serves the wavelet's world-distance test;
  the guide asks for single-channel and the upscaler reads one.
- `mIndirect` and `mUpscaled` are four-channel and appear to use three.

**Done when** the upscaler's inputs are the size of what it reads, measured rather than assumed.

## 6. Motion for what is not an opaque surface

**Last because it is the hardest and step 4 may have made half of it unnecessary.** The trace writes
one motion vector per pixel, from the surface a primary ray hit or from the sky where it hit nothing.
Everything in front of that surface inherits its motion.

- Sprites move independently of the geometry behind them and have no previous position to difference
  against; the bias mask from step 4 may be the whole answer, and if it is, this shrinks to water.
- Water is shaded on the primary hit, so a reflection moves with the surface rather than with what is
  reflected in it. The SDK's specular motion vectors and specular hit distance exist for that.

**Done when** a camera panning across water and rain holds together at the default upscale.

---

## Out of the chain: a specular response on solid surfaces

Not a step, because it is not this pipeline's to take. Water is the only surface in the frame with a
specular half, so the channel Ray Reconstruction demodulates by is nought across every solid — true
of the shading model as it stands, and not true of the scene. `Surface::Material` carries
`mSpecularColour` and `mGlossiness` off `NiMaterialProperty`; `GpuMaterial` carries neither, so
nothing reaches the shader to report.

Closing that is a milestone in the content pipeline rather than a fix in the upscaler's wiring, and
it is the last placeholder any of the four inputs still holds.

---

## The two things not to lose

- The load-bearing NGX conventions live only as prose in `dlsspass.cpp` — the negated jitter,
  `MVLowRes` as a description rather than a request, `InUseHWDepth` describing the depth's shape and
  not its origin, `DepthInverted` and `AutoExposure` deliberately absent. Each was found through a
  `FAIL_InvalidParameter` that named no parameter. Nothing above should disturb them without
  re-deriving them.
- The Streamline guide and the vendored `nvsdk_ngx_helpers_dlssd_vk.h` state **opposite** conventions
  for `InMVScale{X,Y}`. The code follows the header, which is right for raw NGX; a future reader
  finding the guide first will conclude the code is wrong.
