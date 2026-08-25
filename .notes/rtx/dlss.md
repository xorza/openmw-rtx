# The DLSS pipeline, and the order to put it right in

What Ray Reconstruction is currently given, what it is missing, and the sequence that gets it there.
Findings came from reading `components/rtxvulkan/dlss*.{hpp,cpp}`, `ngx.hpp`, the G-buffer channels
it reads and the frame wiring in `vulkanrenderer.cpp`, against NVIDIA's [DLSS-RR programming
guide](https://github.com/NVIDIAGameWorks/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md) and
the vendored SDK's `nvsdk_ngx_helpers_dlssd_vk.h`.

`.notes/rtx/dlss-review.md` holds the findings themselves, as a checklist to be deleted from. This
file is the order to take them in and nothing else.

**The ordering is by dependency, not by size.** What made the two steps that have gone come first
was that nothing after them could be judged until a run said what had reconstructed it and which
network had done it; that is now what a run says, so what follows can be measured as it lands.

---

## Where it stands

**The material inputs are real.** Each shading path reports what it shaded with (`SurfaceResponse`
in `visibility.comp`), what the water and the air took rides on the light they attenuated rather
than on the surface they did not, and the specular channel carries water's Fresnel term rather than
an image cleared once and never written. That is the baseline everything below rests on.

What is left is everything about the frame *in front* of those surfaces. The network is still told
nothing about the sprites and fog composited over them, and it reprojects every one of those pixels
with the motion of whatever geometry stands behind them.

**A run now says what reconstructed it**, which is what makes the rest measurable.
`Rtx::Reconstruction` resolves the denoiser, the jitter and the preset in one place; the renderer
drives every switch from it, `FrameResult` carries it back, and `shot`, `bench`, the profile line and
the game report it. The three-way comparison the steps below are argued from — the raw bounce, the
wavelet, and Ray Reconstruction — is one command each, and the network is pinned rather than
whatever the installed library felt like.

---

## 1. The three cheap correctness items

**First because each changes results and none needs anything built**, and because a run reports the
reconstruction now, so each change is visible as it lands. Before step 2, so that step 2 is measured
against a correct baseline.

- `InReset` fires only when the previous camera basis is zero — a resize or a renderer rebuild. Its
  own doc comment names cell loads, teleports and cuts, and none of those reach it. The renderer
  already learns about scene replacement through `setScene`.
- `DlssPass::record` asserts the colour and output extents and not the other five. A guide, depth,
  motion or albedo at the wrong size is accepted silently — the same failure mode the header already
  warns about for `SAMPLED_BIT`.
- `ngxQualityOf` answers `Off` with `MaxPerf` and relies on a comment that `Off` never arrives.

**Done when** walking through a door stops ghosting, and a mismatched input is a failed assertion
rather than a wrong picture.

## 2. Hand over the two guides the frame already computes

**Second because it is the largest gain available with no new scope.** `visibility.comp` composites
fog and then a sprite layer over the finished frame, and holds both sides of each composite in local
variables before discarding the distinction. Those are exactly `pInColorBeforeFog` /
`pInColorAfterFog` and `pInColorBeforeParticles` / `pInColorAfterParticles`, which exist so that
transparent effects are not reconstructed as if they were the surface behind them.

This is where the rain lives. A rainstorm is a thousand sprites crossing the frame at their own
velocity, reprojected with the motion of the wall behind them; the guide pair is what tells the
network to stop.

- The two colour pairs, and `pInBiasCurrentColorMask` for the same population of pixels.
- `pInIsParticleMask` as well, which costs nothing to fill: `spritesAlong` already returns the
  transmittance each sprite left, so which pixels one covered is known where the composite happens.
- Two more render-resolution images and the composite handing over what it already has.

**Done when** a `--weather=Rain` shot at the default upscale stops smearing drops, judged against
the three-way comparison a run can now be asked for.

## 3. Stop binding what nothing reads

**Third because step 2 settles which channels exist**, and doing it earlier means doing it twice.
None of this changes the picture; it is bandwidth and memory on the frame path.

- Depth is bound as `RG32F` because the second component serves the wavelet's world-distance test;
  the guide asks for single-channel and the upscaler reads one.
- `mIndirect` and `mUpscaled` are four-channel and appear to use three.

**Done when** the upscaler's inputs are the size of what it reads, measured rather than assumed.

## 4. Motion for what is not an opaque surface

**Last because it is the hardest and step 2 may have made half of it unnecessary.** The trace writes
one motion vector per pixel, from the surface a primary ray hit or from the sky where it hit nothing.
Everything in front of that surface inherits its motion.

- Sprites move independently of the geometry behind them and have no previous position to difference
  against; the bias mask from step 2 may be the whole answer, and if it is, this shrinks to water.
- Water is shaded on the primary hit, so a reflection moves with the surface rather than with what is
  reflected in it. **Not by the route the guide names**: it asks for specular motion vectors, and the
  vendored header has no such parameter — it offers `pInSpecularHitDistance` with
  `pInWorldToViewMatrix` and `pInViewToClipMatrix`, and lets the feature derive them.

**Done when** a camera panning across water and rain holds together at the default upscale.

---

## Out of the chain: a specular response on solid surfaces

Not a step, because it is not this pipeline's to take. Water is the only surface in the frame with a
specular half, so the channel Ray Reconstruction demodulates by is nought across every solid — true
of the shading model as it stands, and not true of the scene. `Surface::Material` carries
`mSpecularColour` and `mGlossiness` off `NiMaterialProperty`; `GpuMaterial` carries neither, so
nothing reaches the shader to report.

**And what the guide asks that channel to hold is narrower than "a specular albedo".** It names the
pre-integrated environment BRDF — its own `EnvBRDFApprox2`, over `NdotV`, roughness and `F0` — where
water currently reports a scalar Schlick Fresnel. Right in kind, and not the same function, so
whatever gives solids a specular half should give both surfaces that one.

Closing that is a milestone in the content pipeline rather than a fix in the upscaler's wiring, and
it is the last placeholder any of the four inputs still holds.

---

## The two things not to lose

- The load-bearing NGX conventions live only as prose in `dlsspass.cpp` — the negated jitter,
  `MVLowRes` as a description rather than a request, `InUseHWDepth` describing the depth's shape and
  not its origin, `DepthInverted` and `AutoExposure` deliberately absent. Each was found through a
  `FAIL_InvalidParameter` that named no parameter. Nothing above should disturb them without
  re-deriving them.
- The Streamline guide and the vendored `nvsdk_ngx_helpers_dlssd_vk.h` **describe different
  features**, and the code follows the header, which is right for raw NGX. `ngxPresetOf` carries the
  warning for the half of it that is now pinned in code; the rest is only here. They state opposite
  conventions for `InMVScale{X,Y}`; the guide lists specular motion vectors among the required
  inputs and the header has no such parameter; and the guide's optional set is a transparency layer
  with SSS and depth-of-field guides where the header's is the fog and particle pairs step 2 uses.
  A future reader who finds the guide first will conclude the code is wrong on all three.
