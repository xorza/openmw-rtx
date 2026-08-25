# DLSS pipeline — review findings

Every item below is a checklist entry. **Delete an item when it has been addressed** — this file
lists what is outstanding and nothing else. No item here proposes a fix; each says what was found
and why it matters. `.notes/rtx/dlss.md` is where the fixes are sequenced.

Scope: `components/rtxvulkan/dlss.{hpp,cpp}`, `dlsspass.{hpp,cpp}`, `ngx.hpp`,
`components/rtx/upscale.hpp`, the G-buffer channels Ray Reconstruction reads, and the frame wiring
in `vulkanrenderer.cpp`.

Checked against NVIDIA's own material: the [DLSS-RR programming
guide](https://github.com/NVIDIAGameWorks/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md) and
the vendored SDK's `nvsdk_ngx_helpers_dlssd_vk.h`.

**Line numbers were re-checked against the working tree**, which carries three groups' worth of
fixes on top of `b279e94d15` and has moved them.

Two groups this document opened with are gone. The material placeholders went with `e18e76c5de`,
which gave every shading path a `SurfaceResponse` — the diffuse albedo is the surface alone, the
roughness is per-pixel, and the specular channel is written rather than cleared. And the group about
a run not saying what reconstructed it went with `Rtx::Reconstruction`, which took the rule out of
the frame path: one function decides the denoiser, the jitter and the preset, the renderer drives
every switch from what it answers, `FrameResult` carries the same value back, and `shot`, `bench`,
the profile line and the game all report it. The preset is pinned to Ray Reconstruction's own D and
is selectable, which was measured to move 52% of a frame's pixels against E.

Most of a third went with the two masks: the trace writes where a sprite reached and where the past
is not worth carrying, `pInIsParticleMask` and `pInBiasCurrentColorMask` take them, and `InReset` is
driven by a signal the simulation sends rather than by a camera basis that a cell load never
disturbs. What is left of that group is below, and it is smaller and harder than it looked.

---

## The frame in front of the surface is only half described

`visibility.comp:2762` writes one motion vector per pixel, from the surface a primary ray hit or from
the sky where it hit nothing. Everything the frame composites in front of that surface inherits the
surface's motion.

The frame now *says* which pixels those are — `particleMask` and `biasMask` are written beside the
G-buffer and handed to `pInIsParticleMask` and `pInBiasCurrentColorMask` — so the upscaler stops
accumulating over them. What is still missing is telling it where they went.

- [ ] A sprite still has no motion of its own. Rain, snow, ash and smoke are marked as untrustworthy
      rather than reprojected, which is the header's own remedy and not the same as being right: a
      thousand raindrops crossing the frame are held at the current sample instead of resolved
      across several.
- [ ] Water is shaded by `shadeWater` on the primary hit, so a reflection moves with the water
      surface rather than with what is reflected in it. **Not by the route the guide names**: it asks
      for specular motion vectors, and the vendored header has no `pInSpecularMotionVectors` — it
      offers `pInSpecularHitDistance` with `pInWorldToViewMatrix` and `pInViewToClipMatrix`, and
      those three sit in the block the header marks `/*** OPTIONAL - only for research purposes ***/`.
      The production route is `pInMotionVectorsReflections`, "motion vectors of reflected objects
      like for mirrored surfaces", which is a second motion field and not a wiring job.
- [ ] `InFrameTimeDeltaInMsec` is never set, and the header says it "helps in determining the amount
      to denoise or anti-alias based on the speed of the object from motion vector magnitudes and fps
      as determined by this delta". The renderer does not know the frame delta and its callers do.
- [ ] The colour-pair guides are unset and are **not** the cheap win they look. `pInColorBeforeFog` /
      `pInColorAfterFog` and `pInColorBeforeParticles` / `pInColorAfterParticles` are all inside the
      research-purposes block. Worse, the trace holds both sides of each composite in the *direct*
      channel only, while `pInColor` is the composited frame — `direct + albedo * indirect` — so
      handing over what is already in hand would compare quantities that differ by the whole indirect
      term. Doing it honestly means the stage transmittances reaching the composite pass, which is a
      change to the channel layout rather than two more images.

## The specular albedo is real but is not the quantity that was asked for

- [ ] The guide specifies the specular albedo is the **pre-integrated environment BRDF**, its
      `EnvBRDFApprox2` — a function of `NdotV`, roughness and `F0`. What `visibility.comp:2755`
      writes for water is a scalar Schlick Fresnel, and what it writes for every solid is nought. The
      channel now carries information where it used to carry a cleared image, but not the quantity
      the feature demodulates by.
- [ ] Nothing reaches the shader to build one with on a solid surface: `Surface::Material` carries
      `mSpecularColour` and `mGlossiness` off `NiMaterialProperty`, and `GpuMaterial` carries
      neither.

## Contract gaps inside the pass

- [ ] `DlssPass::record` asserts the colour and output extents match what the feature was built for
      (`dlsspass.cpp:116-117`) and not the five other inputs. A guide, depth, motion or albedo image
      at the wrong resolution is accepted, and the failure mode the header already warns about for
      `SAMPLED_BIT` — success returned, nothing logged, wrong picture — applies equally here.
- [ ] `resourceOf` (`dlsspass.cpp:46`) passes `true` for NGX's read-write flag on every resource
      including the six that are read-only, on the grounds that they were created with
      `STORAGE_BIT`. The header comment explains the reasoning, but the flag is what NGX uses to
      decide barrier behaviour on the resource.
- [ ] `ngxQualityOf` (`ngx.hpp:45`) maps `Upscale::Off` to `MaxPerf` and relies on a comment that
      `Off` never reaches it. If it ever does, the renderer upscales at maximum performance instead
      of refusing.
- [ ] The load-bearing facts about NGX's conventions exist only as prose in `dlsspass.cpp` — the
      negated jitter, `MVLowRes` being a description rather than a request, `InUseHWDepth` describing
      the depth's shape rather than its origin, `DepthInverted` and `AutoExposure` being deliberately
      absent. Each was found by a failure that reported `FAIL_InvalidParameter` and named no
      parameter. Nothing pins them but the comments.

## Resources are sized beyond what their consumer reads

None of this changes the picture; it is bandwidth and memory on the frame path.

- [ ] `gbuffer.cpp:51` makes the depth channel `VK_FORMAT_R32G32_SFLOAT` and binds the whole image to
      `pInDepth`. The guide asks for a single-channel depth; the second component exists for the
      à-trous filter's world-distance test, so the upscaler is fed twice the bandwidth it reads.
- [ ] `mIndirect` and `mUpscaled` are four-channel and appear to use three. The `w` of the direct
      image carries coverage and is read; these two are not.
