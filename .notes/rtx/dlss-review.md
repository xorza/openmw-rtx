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

Most of a third went with the two masks and the sprite motion behind them: the trace writes where a sprite reached and where the past
is not worth carrying, `pInIsParticleMask` and `pInBiasCurrentColorMask` take them, and `InReset` is
driven by a signal the simulation sends rather than by a camera basis that a cell load never
disturbs. A sprite carries its own travel and owns the motion vector of any pixel it mostly is, and
`InFrameTimeDeltaInMsec` is measured between the frames it describes.

The rest of it went with `SpriteClaim` and `mirrorMotionOf`. A sprite owns its pixel's motion vector
by either of the two ways a sprite can own a pixel — covering most of it, or outshining what is left
of the surface behind — so a flame is caught as well as a raindrop. And water writes a second motion
field for what it reflects, mirrored about the water plane, which `pInMotionVectorsReflections`
takes. **The specular albedo is the right quantity, and every solid reporting nought is the content's own
answer.** The guide names `EnvBRDFApprox2` because it is written for a renderer whose specular half
is a pre-integrated GGX lobe; the channel's job is to be exactly what the specular light was
multiplied by, so that demodulation divides it back out, and here that is the Schlick Fresnel share
of a traced reflection. Dividing by an environment BRDF would divide by a number nothing multiplied.
It now also carries the shore fade and the foam, which scale the reflection along with the rest of
the surface and were missing from what the channel reported.

Solids carry no specular because Morrowind has none: `components/nifosg/nifloader.cpp` forces
specular to black and glossiness to zero for every mesh at the game's NIF version — "Morrowind has
its support disabled" — before anything downstream sees it. Measured across Balmora, Arkngthand,
a Vivec canton and a guild interior: **831 materials, none with a specular colour and none with a
glossiness.** Carrying `mSpecularColour` and `mGlossiness` into `GpuMaterial` would carry zeros.

**All four colour-pair guides are deliberately unset, and that is a measurement rather than a gap.**
`pInColorBeforeFog` / `pInColorAfterFog` and `pInColorBeforeParticles` / `pInColorAfterParticles`
all sit in the block the header marks `/*** OPTIONAL - only for research purposes ***/`. They were
wired correctly — the trace kept each stage's direct light and its transmittance, the composite put
the albedo and the bounce back at each one — and both pairs made the picture worse:

- The **sprite pair** nearly triples the horizontal smear down a turning camera's edge bands:
  neither pair 0.537, fog pair alone 0.339, sprite pair alone 1.385, both 1.385.
- The **fog pair** stops a lamp's highlight converging. Balmora at one in the morning, peak byte over
  frames of history: with the pair 83, 98, 111, 137, 149 at 1, 4, 16, 64 and 128 — still climbing.
  Without it: 101, 137, 161, 177, 179, settled by sixty-four. The renderer's own answer with no
  upscaler is 243 and does not move with frame count at all.

**Neither is about content.** Handing the fog pair two identical images — "the fog did nothing",
which cannot be wrong — fails the same way (58, 104, 156). Pointing the sprite pair's second
parameter at an image that is not `pInColor`, ruling out resource aliasing, reproduces its number to
the last digit. These select a different path through the network rather than answer a question
about the frame, and the machinery that fed them was removed with them rather than left dead.

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
- [ ] `particleMask` and `biasMask` each carry a yes or a no in a `VK_FORMAT_R32_SFLOAT` — a bit of
      information in thirty-two, and eight megabytes of render-resolution image between them at
      1080p. `R8_UNORM` is not among the formats Vulkan requires a device to support as a storage
      image, which is why they are floats; a support check at startup, or packing the pair into one
      two-channel image and handing NGX a view of each, would recover most of it.
