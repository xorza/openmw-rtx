# G-buffer and primary-trace payload audit

Scope: `components/rtx/shaders/gbuffer.h`, `components/rtx/renderer/channel.hpp`,
`components/rtxvulkan/trace/gbuffer.{hpp,cpp}`, `shaders/lib/payload.glsl`,
`shaders/trace/visibility.rgen`, `visibilityhit.rchit`, both miss shaders, `shaders/lib/bindings.glsl`,
and every reader of every channel. Read-only audit; nothing built or run.

Sizes: 1 B/px is 1.64 MB at 1707x960 and 3.69 MB at 2560x1440 (MB = 10^6 B). The G-buffer is
142 B/px at the shown width: 232.7 MB at 1707x960 and 523.5 MB at 2560x1440. With the summed width
it is 198 B/px: 324.5 MB and 729.9 MB. The game (`rtxrenderer.cpp:110`) and measured harness runs
(`verbs.hpp:106`) use the shown width.

Bandwidth: the RTX 4090 Laptop (AD103) has 576 GB/s of DRAM bandwidth and a 64 MB L2. At 1707x960 the
G-buffer is 3.6x the L2, so every later pass reads its channels from DRAM. Writing all 142 B/px once
is 0.40 ms of pure DRAM time at 1707x960, overlapped with traversal inside the 2.78 ms `trace` zone.
Each 16 B/px written and read back once is about 0.045 ms of DRAM time at 1707x960. That is an upper
bound on what can show up in the zone medians; the actual saving is smaller, because the stores
overlap traversal.

## 1. Inventory: who writes, who reads, what is used

The trace writes every channel once per pixel (`visibility.rgen:509-551`). Each channel is a
storage image, created with `STORAGE|SAMPLED` and, except for backdrop and puffs, `TRANSFER_SRC`
(`gbuffer.cpp:132-175`). Each frame they move from `UNDEFINED` to trace-write (`gbuffer.cpp:241`), so
contents are never preserved between frames.

| # | Channel | Format | B/px | Readers (per pixel per frame) | Components read | Dead |
|---|---|---|---|---|---|---|
| 0 | Direct | radiance (RGBA16F) | 8 | composite R+W; FSR (fetch and bilinear); spritecomposite and tone when no upscaler | rgb; a only as `shown` with no upscaler (puff transmittance written by spritecomposite) | a written 1.0 by the trace |
| 1 | Indirect | radiance | 8 | accumulate x1, accumulateclamp (tile plus halo through shared memory), composite only when unfiltered | rgb | **a (2 B)** |
| 2 | Albedo | RGBA16F | 8 | composite x1 | rgb | **a (2 B)** |
| 3 | Surface | RG32F | 8 | accumulate, clamp, atrous (25 taps x levels, sampled), shadowmask x2, shadowtiles x2, shadowfilter 3 levels x2, specular, tone, spritecomposite, FSR | rg | none |
| 4 | Motion | RGBA16F | 8 | accumulate, shadowtiles x2, specular, FSR | xyz | **w (2 B)** |
| 5 | Backdrop | RGBA8 | 4 | tone (rgb), spritecomposite (a) | rgba | none, though rgb is zero on every hit pixel |
| 6 | Puffs | RGBA16F | 8 | spritecomposite (taps) | rgba | none |
| 7 | Shadowed | radiance | 8 | shadowmask (sky field), composite | rgb plus a 1-bit flag in a 16/32-bit float | most of a |
| 8 | Specular | radiance | 8 | specular.comp, historyclamp (neighbourhood), composite when unfiltered | rgb, a = roughness | none |
| 9 | Pane | radiance | 8 | pane.comp, historyclamp, composite when unfiltered | rgb | **a (2 B)** |
| 10 | PaneAlbedo | RGBA16F | 8 | composite x1 | rgb | **a (2 B)** |
| 11 | PaneSurface | RG32F | 8 | pane.comp x1 | rg | none |
| 12 | PaneMotion | RGBA16F | 8 | pane.comp, only where a pane stands | xyz | **w (2 B)** |
| 13 | UpscaleMasks | RG8 | 2 | FSR (r fetched, g bilinear) | rg | none |
| 14 | Fill | radiance | 8 | accumulate, clamp, composite when unfiltered | rgb | **a (2 B)** |
| 15 | AmbientAlbedo | RGBA16F | 8 | composite x1 | rgb | **a (2 B)** |
| 16 | Lift | RGBA8 | 4 | tone, **only while Night-Eye is active** (`tone.comp:138,185`) | rgb | **a (1 B)**; the whole channel goes unread most frames |
| 17 | Penumbra | R16F | 2 | shadowmask (sky field) x1 | r | none |
| 18 | SpecularAlbedo | RGBA16F | 8 | composite x1 | rgb | **a (2 B)**; rgb is RGB9E5-exact |
| 19 | Lamped | radiance | 8 | shadowmask (lamp field), composite | rgb plus a 1-bit flag | most of a |
| 20 | LampPenumbra | R16F | 2 | shadowmask (lamp field) x1 | r | none |

The dead lanes total **19 B/px (13% of the G-buffer)**: 31.1 MB at 1707x960 and 70.0 MB at
2560x1440, written every frame. At the summed width it is 25 B/px. The format set forces most of
this, because no RGB16F storage format exists. The proposals below recover what the format set
allows.

Channels whose last reader runs mid-frame:
- Indirect and Fill: after `accumulateclamp`.
- Penumbra and LampPenumbra: after each field's `shadowmask`.
- Pane, PaneSurface and PaneMotion: after the pane filter and its `historyclamp`.
- Specular: after the glossy filter.

## 2. Findings, ranked

### F1. Carry each penumbra in its shadowed channel's alpha, with the open bit as the sign. Straightforward and exact.

**What.** `CHANNEL_SHADOWED.a` and `CHANNEL_LAMPED.a` each hold a single bit, 0 or 1, in a half
(a float in summed runs). Beside them, `CHANNEL_PENUMBRA` and `CHANNEL_LAMP_PENUMBRA` are separate
R16F images that hold a radius which is never negative. Store `a = open ? +radius : -radius` instead,
by bits (`floatBitsToUint(r) | (open ? 0 : 0x80000000u)`), so that a closed bit with a zero radius is
`-0.0`. Read the bit back as `(floatBitsToUint(a) >> 31) == 0` and the radius as `abs(a)`. Then drop
both R16F channels.

**Where.**
- `gbuffer.h:71`, `:200-247`: the two channel definitions and `CHANNEL_COUNT`.
- `gbuffer.cpp:128-130`, `:172,175`.
- `visibility.rgen:513-516`: four stores become two.
- `bindings.glsl:150-155`.
- `shadowmask.comp:58-66,101-112`: one `imageLoad` instead of two per pixel per field.
- `shadowpass.cpp:62-63,79`: one binding fewer.
- `composite.comp:99-101`: the unfiltered bit becomes a sign test.
- `lib/shadowed.glsl`: a `packShadowedAlpha` / `shadowedOpen` / `shadowedPenumbra` trio, kept in one
  place. These helpers also suit the payload, where the two open flag bits could move into the two
  penumbra halves' signs. That frees flag bits but saves no word (see P2).

**Why it is exact.** The radius already crosses the payload as a half (`payload.glsl:219-220`), so
it is representable in either radiance width. The bit is binary everywhere it is made: `packAnswer`
folds it to `mOpen > 0`, and the shore mixing draws one bit, so no blended value ever needs the
alpha. A half store keeps the sign under any rounding mode, including this card's toward-zero
(RtxHalfStoreTest). `SHADOW_PENUMBRA_CLEAR` = 65504 is the largest half, so `+65504` and `-65504`
are both finite. The census is unaffected, since nothing here is NaN.

**Saving.** 4 B/px (6.6 MB / 14.7 MB) and 2 channels. Also two images, two descriptors, two barrier
entries a frame, and one load per pixel per field in the shadow mask pass. Time: about 0.02 ms of
DRAM time at 1707x960, so negligible. The point is fewer channels and a simpler mask pass.

**Risk and proof.** No pixel should move. Checks:
- `./omw shot --views=all --upscale=off --against=<base>` must report nothing moved.
- `./omw check`.
- The `shadow.cpp` and `light.cpp` tests that read `Channel::Penumbra` (`shadow.cpp:249,305,386,440`,
  `light.cpp:442`) move to the sign-packed alpha. Those reading `Channel::Shadowed`'s alpha as 0 or 1
  need the decoder.
- `./omw repeat --pairs=10`.

A capture name the harness exposes (`g-penumbra`, `g-lamp-penumbra` in `channel.hpp:67,71`)
disappears, so check any `--channels=` uses in `files/rtx` and the docs.

### F2. Store the specular albedo as its own RGB9E5 word. Straightforward and exact. Fold the pane albedo in as an experiment.

**What.** `specularModulation` (`shading.glsl:480-484`) rounds the specular albedo to RGB9E5 on
purpose, and the payload carries it as that word (`payload.glsl:162-164,218`). The channel then
spends 8 bytes (RGBA16F, alpha dead) to hold a 4-byte value. Every RGB9E5 value is exactly a half
(9-bit mantissas, smallest step 2^-24, largest 65408), so today's channel is exact but twice as wide
as needed.
- Make it `R32_UINT` and write `RTX_STORE_WORDS(packRgb9e5(...))`. The composite unpacks it.
- **Then put the pane albedo in the same texel (`RG32UI`):**
  - `visibility.rgen:484-486` already rounds `paneModulation` to a stored-exact value before dividing
    by it. Rounding it with `packRgb9e5` instead keeps the identity the comment there requires: the
    light is divided by the number the composite multiplies back.
  - This copies the decision already taken for the specular albedo, and keeps `PANE_ALBEDO_FLOOR`
    as a floor.
  - Apply the floor after the rounding, or check that `packRgb9e5` never rounds a floored component
    to nought. At max 1, 1/255 lands on 1/256, so it does not.

**Where.**
- `gbuffer.h:64` (a new `GBUFFER_MODULATIONS STORAGE_RG32UI`) and `:157,238`.
- `gbuffer.cpp:106,165,173`.
- `visibility.rgen:520,528`.
- `bindings.glsl:132,158`.
- `composite.comp:71,77,104`.
- `compositepass.cpp:72,75`.

**Why.** Both channels are read exactly once, by the composite. Packing them into one 8-B texel makes
it one fetch, not two of 8 B each. The history images already use RGB9E5-in-uint
(`ACCUMULATE_FAST`, `HISTORY_CLAMP_FAST`), so the pattern is established in this tree.
`E5B9G9R9_UFLOAT` itself is not usable: it is not a typed-UAV or storage format on D3D12-class
hardware, and Vulkan does not require it as storage.

**Cost the change carries.** The digest declares `readonly image2D images[DIGEST_IMAGES]`
(`digest.comp:24`, `floatBitsToUint(imageLoad(...))`). `VulkanRenderer::readChannel`
(`vulkanrenderer.cpp:842`) reads floats. A uint channel needs a `uimage2D` binding in the digest and
a words readback. Storing the RGB9E5 bits through `R32F` is not an option: an exponent of 15 or 31
with blue ≥ 480 is a float NaN pattern, which is common for albedos near 0.5-1. The census would
count it, and a driver may canonicalise it.

**Saving.**
- Specular alone: 4 B/px (6.6 / 14.7 MB).
- With the pane albedo: 8 B/px (13.1 / 29.5 MB) and one channel fewer.
- Composite read traffic falls by the same amount.

**Risk and proof.**
- **Specular alone:** bit-exact, because the values are already RGB9E5. Check with
  `./omw shot --against` (nothing moves), `./omw check`, and the `light.cpp:1251` test.
- **Pane albedo:** changes how finely texture is demodulated out of the pane light. A saturated
  layer's minor channel is held to 2^-9 of the major one, where a half holds 2^-11 of itself. That
  moves the pane filter's input, not the unfiltered picture, which meets to the rounding of a
  product. Checks:
  - `./omw shot --against` at the pane places.
  - `./omw release noise --ab=...` with `--views=` narrowed to a pane place (bias and noise).
  - `pane.cpp:325`.

### F3. The pane channels: 32 B/px (22.5% of the G-buffer) written every frame, in every scene. Experiment.

**What.** Pane, PaneAlbedo, PaneSurface and PaneMotion are written for every pixel of every frame
(`visibility.rgen:527-530`). The pane filter (`pane` zone 0.047 ms) and its clamp read them, even in
scenes where no instance can ever be peeled. Two alpha/w lanes are dead (4 B).

**Options.**
1. Add a frame fact "no peelable material in the scene", a bit of `VisibilityVariant`
   (`lib/variants.glsl`, which already specialises per tuple of facts). It lets the launch skip the
   four stores and the pane filter skip its dispatch, so the composite reads a constant nought.
   - The digest's "every channel is written" rule (`visibility.rgen:505-507`) then needs those four
     channels cleared once on the transition into that state, with `vkCmdClearColorImage`.
   - `GBuffer::begin`'s per-frame `UNDEFINED` transition would have to leave them alone while the
     fact holds.
2. Fold PaneSurface and PaneMotion into one `RGBA32UI` texel: normal bits, distance bits,
   `packHalf2x16(xy)`, `packHalf2x16(z,0)`. Same 16 B, one channel and one fetch fewer in
   `pane.comp:75,88`. This needs F2's uint path. Low value on its own.

**Why.** Panes are glass and faded actors. In most frames they cover a small share of the pixels,
and in many scenes none. Writing 32 B/px of zeros is 52 MB a frame at 1707x960, about 0.09 ms of
DRAM time for the stores alone. Its reads add as much again.

**Saving.** Up to about 0.1-0.15 ms of DRAM time plus the 0.047 ms pane zone in a pane-free scene,
measured as `trace` + `pane` + `clamp` medians. No memory saving unless the images are also released.

**Risk and proof.** It is a new specialised fact, so the specialised and the carry-everything launch
(`mSpecializeLaunches`) must still agree. Checks:
- `./omw check` (both `--variants` settings).
- `./omw repeat --pairs=10`.
- `./omw release bench`, A/B, read as medians and p99.

The fact must be conservative. A material whose texture alpha can fall under one must count as
peelable even when none happens to be visible.

### F4. The payload's 18 full-float radiance words exist only for summed runs. Experiment.

**What.** 30 words cross the trace (`payload.glsl:146-185`). Of these, 18 are the six radiances
(`mRadiance`, `mBounced`, `mFilled`, `mSky`, `mLamps`, `mSpecular`), kept as f32 because "a term
rounded to a half before the sum does not average away" (`payload.glsl:13-15`,
`reconstruction.hpp:228-237`). That is true of deterministic rounding. It is not true of
**stochastic rounding**, where the half above or below is chosen with probability proportional to
the distance. That rounding is unbiased, so a sum of N frames converges to the f32 mean, with an
extra variance of at most ulp²/4 per frame, divided by N. Each radiance would then cross as three
halves: 9 words instead of 18, a 30-word payload becoming 21, in both radiance widths, along one
path.
- Do the rounding with integer operations in `packAnswer`, the way `visibility.rgen:484-486` rounds
  by bits. Do not trust `packHalf2x16`, whose rounding mode is the driver's, and which this driver
  folds with its unpack (`visibility.rgen:479-482`).
- Draw the random number from a dedicated `STREAM_*`/`SEED_*` per pixel, so frames stay reproducible
  for `repeat`.

**Why.**
- NVIDIA's guidance: "Keep the ray payload small. Payload size translates to register count, so
  directly affects occupancy" ([NVIDIA RTX best practices](https://developer.nvidia.com/blog/rtx-best-practices/)).
  Its update adds that packing code "is rarely beneficial"
  ([updated best practices](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/)).
  The packing here is already done. This change removes words instead of adding code.
- The tree's own record: `a72bc240f4` measured "up to 0.02 ms more for the payload's word" when the
  payload grew from 25 to 26 words.
- NVIDIA's Indiana Jones write-up got 15% of a path tracer's time from halving FP32 state to FP16
  with "no visible image change"
  ([NVIDIA, Indiana Jones live state](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/)).
  That was live state, not payload, but the same register argument applies.

**Saving.** Nine words of payload. Expect anywhere from nothing to about 0.1 ms of the 2.78 ms
`trace` zone. It is unknown whether the closest-hit shader's own registers dominate the pipeline's
allocation. NVIDIA reports no executable statistics for ray tracing pipelines
(`device.cpp:305-312`), so only a bench, or Nsight Graphics, can tell.

**Risk and proof.**
- Shown runs are unaffected beyond one extra rounding: half the payload, then the store, at most
  1.5 ulp instead of 1.
- Summed references gain a tiny zero-mean noise. Check with:
  - The converged-mean tests `RadianceWidth` cites.
  - `./omw release noise --ab=<switch>` (bias column).
  - `./omw release bench`, hot, back to back, `trace` median and p99.
  - `./omw repeat --pairs=10`, since the dither must be deterministic.
  - `./omw kernels --against` to name the moved modules.

If the bench shows nothing, drop it. The posture puts exactness first, and this trades a
deterministic f32 for an unbiased f16.

### F5. Alias the channels that die mid-frame with the denoiser's frame-only images. Experiment, structural, memory only.

**What.** These channels are dead after their last reader:
- Indirect and Fill (16 B/px) after `accumulateclamp`.
- The penumbras after `shadowmask` (gone under F1).
- Pane, PaneSurface and PaneMotion (24 B/px) after the pane filter.
- Specular (8 B/px) after the glossy filter.

Frame-only images allocated after those points:
- The atrous narrow pairs: `Narrow`, `NarrowOther`, `FillNarrow`, `FillNarrowOther`, RGBA16F,
  32 B/px together, written only after the clamp.
- The second shadow field's `Scratch`, `Tiles`, `Mask` and `Penumbra`.

`GBuffer::begin` already transitions every channel from `UNDEFINED` (`gbuffer.cpp:241-251`). That is
the contract a transient alias needs, and `DenoiseHistory::discard` does the same for its images.

**Why.** NRD splits its pools into "persistent" and "transient", and "textures from the transient
pool can be reused by the application right after denoising"
([NRD README](https://github.com/NVIDIA-RTX/NRD)). At 1440p its RELAX diffuse+specular sets aside
127.56 MB of 300.06 MB as aliasable. Here the G-buffer and the denoiser each own their images
outright, so nothing is shared.

**Saving.** About 32-48 B/px of device memory: 52-79 MB at 1707x960, 118-177 MB at 2560x1440. No
time saving.

**Risk and proof.** Ownership changes. A frame-transient heap, VMA aliasing, would sit under both
`GBuffer` and `DenoiseHistory`, which crosses into the denoiser's area. A missed barrier between an
alias's last read and the next write is a race `repeat` can catch, so run `./omw repeat --pairs=10`
and validation. It also only pays where memory is the constraint, not on this 16 GB card.

### F6. Lift is written every frame and read only under Night-Eye. Minor.

`visibility.rgen:525-526` writes 4 B/px each frame. `tone.comp:138` reads the channel only while
`mNightEye > 0`, a spell effect that is off almost always. The saving is 6.6 MB of writes a frame at
1707x960, about 0.01 ms. This is not worth a launch variant on its own. If F3's variant bit lands,
a "no lift" bit next to it is close to free.

## 3. Payload, word by word

| Words | Field | Verdict |
|---|---|---|
| 18 | six radiance vec3, f32 | See F4. Only summed runs need f32, and stochastic rounding makes halves unbiased for both. |
| 2 | diffuse rgb plus opacity, halves | Tight. |
| 1 | ambient gb, halves (r rides in motion's 4th half) | Tight. |
| 1 | specular albedo, RGB9E5 | Tight and exact (F2 makes the channel match). |
| 2 | lift rgb plus sky penumbra, halves | Tight. |
| 1 | lamp penumbra, **one half of the word spare** | 16 bits wasted, but no other 16-bit field is left to fill it. |
| 1 | normal code | Tight. |
| 2 | motion xyz plus ambient r | Tight. |
| 1 | distance f32 | Needed whole. |
| 1 | flags: 5 bits, roughness byte, backdrop/mis-moved half | 3 bits spare. |

The 12 non-radiance words carry 15 halves, three 32-bit words and 13 flag bits: 365 bits, so the
floor is 11.4 words. **The non-radiance part is already within one word of minimal.** F1's sign trick
frees two flag bits but saves no word. Every word that can still go is in the radiances.

**Live state in the launch.** `PaneStack` is 19 floats, two of them live (`armsStack`, `panes`),
plus `NearestPane` (5), origin, direction, cone and layer. These are live across the `traceRayEXT`
inside `peelLayers`' loop (`visibility.rgen:205-215`) and across the world trace after the arms'
peel (`:295`). NVIDIA's compiler spills such state at each call site
([updated best practices](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/)).
In Indiana Jones a loop around a trace alone cost 72 B of spilled state. Here those call sites run
only on pixels with a pane or with see-through arms, so this is not a frame-wide cost. **Look in
Nsight Graphics' live-state view before acting.** If a pane-heavy place (Red Mountain's translucent
materials, Chameleon actors) shows a trace tail, the cheap fixes are these:
- `mDrawn`, `mAlbedo`, `mGlow` and `mLift` carried as halves.
- The arms' linear accumulators folded into the world stack's. `mRadiance`, `mThrough` and
  `mScattered` must stay apart, because the arms' medium split is not a prefix of the world's.

**The miss shaders** write the whole 30-word record (`noAnswer`). The unshaded miss
(`visibilityunshaded.rmiss`) needs only the hit flag and the distance. Payload writes are register
moves, so this is not worth a second payload type.

## 4. Considered and rejected

- **R11G11B10F (`B10G11R11_UFLOAT`) for any radiance channel.** It has 6/6/5-bit mantissas. Under
  this card's toward-zero stores that is a systematic bias of up to 1/32 (3%) on blue, and it is
  unsigned. Precision comes first.
- **RGB9E5 for the shown-width indirect, fill and pane radiance** (−12 B/px, and shader-rounded to
  nearest, which would also remove the store's toward-zero bias). It needs a uint/float reader split
  per radiance width in accumulate, clamp, pane, historyclamp and composite. The radiance width is a
  creation-time choice and the readers are format-less by design (`bindings.glsl:56-60`), so this
  means two reader paths. Its minor-channel step (2^-9 of the major channel) is coarser than a
  half's. Not recommended unless F1-F3 are done and memory is still the constraint. If tried, run
  `./omw release noise --ab` across the suite.
- **RGBA8, RGB10A2 or RGB9E5 for the diffuse and ambient albedos.** Unlike the specular and pane
  albedos, nothing is divided by them: their rounding is an error in the picture, not a
  demodulation choice. A UNORM store's rounding is the driver's (this one rounds to nearest, `RtxHalfStoreTest`). `composedLight` must also meet
  the trace "to the bit" (`compose.glsl:16-19`), which works today only because the payload rounds
  to half and the half store keeps a half exactly.
- **Merging Surface and Motion into one RGBA32 texel.** The atrous cascade reads Surface 25 times
  per tap per level through the sampler. `gbuffer.h:43-51` already argues for one 8-B fetch there,
  and a 16-B texel would double the widest read in the frame.
- **Normal plus roughness in RGB10A2, NRD style.** NRD's guide pair is R10G10B10A2 normal+roughness
  (4 B) plus R32F viewZ (4 B) = 8 B ([NRD README](https://github.com/NVIDIA-RTX/NRD)). Here it is a
  24-bit octahedral code plus f32 distance = 8 B, with roughness in `CHANNEL_SPECULAR.a`. That is the
  same size, and finer: 0.06° at worst, per `gbuffer.h:47-50`. Octahedral coding is the field's
  choice ([Cigolle et al. 2014, JCGT 3(2)](https://jcgt.org/published/0003/02/01/)).
- **Using the backdrop's rgb lanes on sky pixels for the albedos, which are nought there.** That
  saves 4 B, but makes a channel's meaning depend on the surface's kind and depends on the wavelet
  leaving sky pixels at exactly nought. Not worth it.
- **A visibility buffer (instance, primitive, barycentrics) with re-derivation later.** The hit
  shader has already sampled the textures and resolved layers to shade. Re-deriving the albedos in
  the composite would pay those texture reads twice.
- **Dropping Fill by deriving it.** Fill is the sky's glow or the far hit's `pathEnd`, a coloured
  per-sample quantity (`shading.glsl:858-899,950-958`), not a scalar share of the bounce. One
  question is open: at a pixel whose ambient albedo equals its diffuse albedo, the fill adds
  `(ambient − albedo) × fill = 0` (`compose.glsl:29`). The share of such pixels in vanilla content is
  unmeasured. If it is near total, the fill's whole wavelet branch is mostly spent on nought, which
  is the denoiser's question to take up. A `check` assertion or a `scene` statistic comparing the
  two albedo channels would answer it.

## 5. Order of work

1. **F1** (penumbra into alpha): −4 B/px, −2 channels, exact.
2. **F2** with the specular albedo alone: −4 B/px, exact. This introduces the uint channel path in
   the digest and readback. Then decide on the pane albedo, measured with `noise` at the pane places.
3. **F3** (pane-free scenes skip the pane channels): measured with `bench`.
4. **F4** (half radiances in the payload): measured with `bench`, held to `noise` and the reference
   tests.
5. **F5** (aliasing): only if memory becomes the constraint.

F1 plus specular-only F2 take the shown-width G-buffer from 142 to 134 B/px and 21 to 19 channels,
with no pixel moving. With the pane albedo folded in it is 130 B/px and 18 channels.

## Sources

- NVIDIA, "Best Practices: Using NVIDIA RTX Ray Tracing": https://developer.nvidia.com/blog/rtx-best-practices/
- NVIDIA, "Best Practices for Using NVIDIA RTX Ray Tracing (Updated)": https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/
- NVIDIA, "Path Tracing Optimization in Indiana Jones: SER and Live State Reductions": https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/
- NVIDIA NRD README (input guides, 2.5D motion, persistent/transient pools): https://github.com/NVIDIA-RTX/NRD
- Cigolle, Donow, Evangelakos, Mara, McGuire, Meyer, "A Survey of Efficient Representations for Independent Unit Vectors", JCGT 3(2), 2014: https://jcgt.org/published/0003/02/01/
- Vulkan specification, conversion from floating-point to normalized fixed-point: https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-fpfixedconv
- Mesa RADV, "radv/rt: Lower ray payloads to registers", on payload cost on AMD's open driver: https://gitlab.neroreflex.duckdns.org/NeroReflex/mesa/commit/658ce711d5b9c3137d28f6e2b29e56ba847729bb
