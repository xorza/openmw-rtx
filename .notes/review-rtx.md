# Review: the ray tracer, the whole diff against upstream

Base: upstream `4e718b500f`. Scope: `components/rtx`, `components/rtxvulkan`, `components/myguirtx`,
`apps/openmw/mwrender`, the fork's hunks in other upstream code, `apps/rtxtool`, `tools/omw`, and
the tests of all of them. Eleven reviewers read the code. Nothing was built for the review. The
test run below is the debug build that existed.

Two reviewer claims were checked by hand. The reciprocal fold claim: a C program at
`-ffp-contract=off` gives `y * (1/y) != 1` for 23 integers under 200, the first one 41. The caustic
claim: `caustic` in `lib/sea.glsl` adds the cascade curvature without a turn.

## Proposal

### Root causes and the shape that removes each

- **The world frame has no owner, and its description is written in the backend.**
  `VulkanRenderer` composes the frame block (`constantsFor`, `describeWorld`, the `FramePast` fold,
  `sampleFrame`, the picture-light overlay) and holds `FogDrift`, `FramePast` and the previous
  camera. It also carries the world's chain, arena, upscaler and shown radiance loose. The same
  shape for pictures is packaged as `PictureTracer`. Target:
  - `Rtx::FrameDescriber` in the core owns the drift, the past and the previous camera. It turns a
    `FrameRequest` into the sampled constants, `WorldOptions`, `Reconstruction` and `FramePast`, and
    a picture's request into its own. A core test covers the composition without a device. The air
    clock gets one owner, so `holdAir` is one call.
  - `FrameTracer` in the backend is `PictureTracer`'s twin. It owns the chain, the arena, its
    declarations, the upscaler and the shown radiance, with the same `bytesAt`, `resize`, `release`
    and `record`. `createTargets` becomes reserve, world rebuild and `tracer.resize`.
  - Every medium is stepped by the owner before a chain records (`stepRipples` and a new
    `synthesiseSea`). The chain only reads media, and `TraceMedia`'s `const` says something true.
  - Parts are handed their siblings at the call (`PictureTracer::trace`, `TraceChain::record`,
    `SpriteBin`), so member order stops being a correctness rule.

- **`Rtx::Renderer` is one 40-method interface that tests reach through.** Split it by consumer, as
  `GuiRenderer` already is: a scene holder (the six methods `SceneUploader` calls), a picture
  interface (the five `OffscreenTrace` calls), and the GUI half. Test reads move to the concrete
  backend's test probe, never onto the shared contract.

- **Change detection in the mirror samples a face, not every source of change.** A frozen root is
  compared by `Face`, which sees only what the walk visited. A node that the view mask rejects can
  still carry a controller. Target: the walk's child loops record the update state of the children
  the mask rejects, and `Face` carries the identity the walk computed. A frozen root keeps its
  discrete state (lights, switch values) as records and replays it on a pass. A lamp or a plant then
  freezes like every other root.

- **Deferred destruction is a convention per type.** `Owned` puts "everything a submit may read ends
  through the graveyard" into the type for handles. `DeviceMemory` frees at once, and four owners
  bury it by hand. Target: `DeviceMemory` buries itself, one handle template with an end policy
  replaces `Owned`, `Immediate`, `Root` and `Messenger`, `AccelerationStructure` uses volk's
  destroyer, and owners default their moves. A `Batch` owns its read stamp and applies it at
  hand-over, as `keep` already does for burials.

- **Renderer-wide scratch is owned per scene, and transient memory is kept at its peak.** Each
  `DeviceScene` owns a `TextureArrival` whose rooms grow to its largest group and stay. The staging
  ring keeps the size of the largest arrival until the next world load. Target: one `TextureArrival`
  beside `TexturePasses`, handed to `TextureArray::write` at the call. The ring trims after an
  arrival retires.

- **Byte figures are hand-picked from members.** `SceneStats` misses about 120–150 MiB, the bench
  record writes no memory, and `MemoryReport` is Vulkan's heap model in the core. Target: each owner
  answers `getBytes()` over all it allocates, the core reports by use and by three pools (video,
  system, host-written aperture), and the record writes it.

- **The image facts walk the texels several ways.** The mean, the alpha, the shading estimate and the
  mip oracle each walk the finest level. Laid images are widened two or three times. Target: one
  per-block walker in `image/` that yields alpha and a linear palette per block. `layImage` widens
  into the image it returns, and the texture passes read the laid image. One rule: a name identifies
  its texels.

- **Field-stored images meet code that assumes D3D's out-of-bounds reads.** The FSR callbacks map an
  off-frame coordinate onto a real pixel. Target: every read of a field-stored channel at a
  coordinate the kernel did not make goes through one guarded load that answers "nothing" off the
  frame.

- **Quantities are combined across frames of reference.** The caustic adds a tile-frame Hessian to a
  world-frame one. Target: each sea quantity carries its frame, with one turn helper for a vector and
  one for a symmetric tensor.

- **GLSL rules that cannot take an image are written again per kernel.** The split blend, the running
  mean gather and the box statistics are each spelt twice and drift. Target: one macro or helper per
  rule, in the style of `RTX_HISTORY_SHARES`, with kernels supplying only their loads.

- **"Did it happen" is not returned.** `OffscreenTrace::traceInto` returns nothing, and `TracedView`
  infers a recorded copy. The run hook is gated on a timing optional. Target: each step returns
  whether it did its work, and the state follows that.

- **The rule that pairs a frame's figures lives in two places in the harness.** `MeasureWindow`
  pairs samples and arrivals. `Measurer` pairs crossings itself and reads the world inline. Target:
  every per-frame figure goes through `MeasureWindow` as plain input.

- **Hand-kept numbers beside facts the code states.** `sMaxGpuZones = 40` against 37 zones plus the
  six its comment adds. `sMaxHeaps` restates `VK_MAX_MEMORY_HEAPS`. `STREAM_TURN` repeats turns and
  keeps a channel nothing reads. Target: derive each bound from what it bounds, and check each table
  for what its comment promises.

- **The test harness leaves state to each fixture, and tests sit by history, not by unit.** Target:
  `RendererTest::SetUp` resets every setting a test can move, one helper makes an unlit camera, the
  production passes take their sizes so a test drives them, and test files mirror the folders they
  test.

### Observations without a change

- No game code branches on which renderer it has. The kind is chosen once (`engine.cpp:700`). Every
  other question goes through `support()`, `groundReadsGates()`, `isAbandoned()` or the two
  documented null escapes. No Vulkan header reaches the core.
- Every hunk in upstream code outside `mwrender` fits a reason `AGENTS.md` allows, and every
  Accepted-diff entry still matches its code. The exception is two cosmetic reorders in `mwrender`
  (batch 12).
- Pass order and barriers held wherever a reviewer traced them. BLAS/TLAS build, refit, the rebuild
  rota and compaction are correct. The record paths allocate nothing per frame after warm-up. No
  workgroup returns before a `barrier()` non-uniformly. Every shared struct is scalar-packed with a
  size check.
- Math checked sound: the bounded VNDF, Schlick and split-sum, RIS, `falloffAlong`, the fog phase
  constants, RGB9E5, the FFT, the sky fits, the wave spectrum, the harness's nearest-rank quantiles.
- Test run, debug, 2026-10-28: `components` 2543 tests in 17.9 s, `openmw` 625 in 1.5 s,
  `crash.matrix` 9.7 s, the GPU binary 464 tests in two shards of 62.9 s and 58.0 s.
  `RtxVisibilityTest` takes 56 s of the 121 s the shards sum. 56 tests take over 1 s, the slowest
  2.15 s (`RtxBounceTrailTest.theClampShortensTheSkysTrail`). Two `RtxPresenterTest` cases failed
  with "No available video device" (`presenter.cpp:67`), because the session's shell had no
  display.

## Plan

Each batch leaves the build and the tests green. Whoever addresses an item deletes it.

### 1. Shader correctness

Files: `components/rtxvulkan/shaders/upscale/fsrcallbacks.glsl`, `shaders/lib/sea.glsl`,
`components/rtxvulkan/spirv/spirvpin.cpp`, `shaders/lib/bluenoise.glsl`, `shaders/lib/random.glsl`,
`components/rtx/shaders/scene.h`, the host's blue-noise generation, and their tests.

- [ ] `fsrcallbacks.glsl:157-164` (`LoadInputDepth`), `:207-220` (`LoadInputMotionVector`) and
  `LoadInputColor`: an off-frame coordinate goes through `channelAt`/`storedOf` onto a real pixel.
  For an even width W, x = W reads pixel 0 or 1 of the same row. Rows y ≥ H read out of bounds.
  `FindDepthExtents` (`ffx_fsr3upscaler_prepare_inputs.h:23-47`) loads its nine taps before
  `IsOnScreen`, and `ReconstructPrevDepth` scatters the stolen depth into the previous-depth image.
  It happens when the render width is not a multiple of 8 (Quality at 2560×1440 traces 1707 wide):
  turning left, a band at the right edge loses its history. Target: one guarded load that returns
  depth 0, motion 0 and colour 0 outside `RenderSize()`, with the width read from one source
  (`LoadInputDepth` reads `cbInputs.mEyes.mWorld.mWidth`, `LoadInputMotionVector` reads
  `RenderSize().x`).
- [ ] `sea.glsl:302-331` (`caustic`): the cascades' curvature (tile frame, `:324`) is added to
  `rippleHessian` (world frame, `:331`) without a turn. `mSeaHeading` is about 58° off the x axis, so
  the lens pattern under a wake or a footfall over a sunlit bed is wrong. Target:
  `seaWorldHessian(H) = R H Rᵀ` beside `seaWorld`, applied to the cascade sum before the ripple is
  added. Test: a bed under a turned sea with a ripple, against the same scene unturned.
- [ ] `spirvpin.cpp:1217-1245` (`Pinner::fold`): `x / c` becomes `x * rn(1/c)`, which turns an exact
  integer quotient into `n − ulp` for c = 41, 47, 55, 61, … The `OpFMod`/`OpFRem` lowering
  (`:684-693`) feeds its `OpFDiv` through the same fold, so `mod(41.0, 41.0)` is 41. Latent: the
  only `mod(` (`probes/pinning.comp:74`) has non-constant operands. Target: fold only where c is a
  power of two. Add the 41/41 case to `apps/components_tests/rtxvulkan/spirv/spirvpin.cpp:594-671`,
  which checks only the reciprocal's bits.
- [ ] `bluenoise.glsl` `STREAM_TURN`: entries 0/2/6 are all 0.7548777 and 1/3/7 all 0.5698403, so the
  bounce (2, 3) and the sun-disc ray (6, 7) differ by a fixed offset at every pixel forever.
  `scene.h:147-189` promises uncorrelated draws. Channel 11 (`STREAM_SHADOWED_PICK`, dropped in
  `13a290a78a`) is never read, yet `RANDOM_STREAMS = 13` says "exactly the number drawn", and the
  host generates its 16 KB tile. Target: a distinct irrational turn per channel, channel 11 removed,
  a static check that the turns are distinct, and the pinned-ranks test moved with it.

### 2. Mirror change detection

Files: `components/rtx/mirror/sceneextractor.cpp`, `worlddescent.hpp`, `frozenroots.hpp/.cpp`,
`cells/cellplacer.*`, and `apps/components_tests/rtx/mirror/`.

- [ ] `sceneextractor.cpp:362-365`, `worlddescent.hpp:35-46,64-107`, `frozenroots.hpp:102-114`: a
  root is kept walked only when a node the walk visits has an update callback. A `VisController`
  hides its node with mask `Mask_UpdateVisitor` (`nifosg/controller.cpp:378-386`), which
  `sWorldTraversal` (`worldmirror.cpp:116`) excludes. The root freezes with the part hidden and
  never shows it, because nothing `Face` compares changes. The same blind spot misses a later
  controller under a child whose count is already 1 (`frozenroots.hpp:99-101`, tested only 0→1 at
  `frozen.cpp:232-247`), and `Face` ignores cull callbacks (`TransparencyUpdater` from
  `Animation::setAlpha`, `animation.cpp:1895`). Target: the child loops (`forEachChild`, the
  Switch/Sequence/LOD branches) test the mask themselves. A rejected child with an update callback or
  a nonzero update count marks the root changeable, and cull callbacks count. Tests: a part under a
  `Mask_UpdateVisitor` node with a callback, frozen then shown; a cull-callback updater added to a
  frozen root.
- [ ] `sceneextractor.cpp:314-355`, `:912-971`, `frozenroots.cpp:194-222`: an unstamped root's
  identity is `identityWith(parentHash, childIndex)`, and `pass` compares `Face` only. When a sibling
  before frozen root A goes, the root after A shifts into A's index and its drawables hash to A's held
  keys. Latent: every depth-2 root in the game is stamped, but the freeze tests build unstamped ones
  (`frozen.cpp:57-63`). Target: `Face` carries the identity the walk computed. Test: remove the
  sibling before a frozen unstamped root while the one after it is changeable.
- [ ] `sceneextractor.cpp:362-379`: any `asSwitch()` or `LightSource` marks a root changeable, so every
  lamp, lit window and harvestable plant is walked whole every frame. An estimate from the path:
  0.3–1 ms a frame for several hundred such roots in a town (measure with `mPassedFrozen` against
  `mMeshesReused`). The ring already holds a lamp as a record (`cellplacer.cpp:679-692`). Target: a
  frozen run records its lights (source, place, class) and its switches (node, value mask). `pass`
  adds the lights again and thaws on a changed switch value. A `LightSource` update callback stops
  counting as change.
- [ ] `apps/components_tests/rtx/mirror/cells/cellring.cpp:1324-1349`
  (`unsettledTheRingAdoptsOneCellAFrame`): sleeps 1 ms a frame for up to 400 frames and asserts the
  reader delivered in that time. Target: wait on the supply.

### 3. The game-side owner

Files: `apps/openmw/mwrender/rtx/rtxrenderer.*`, `tracedview.cpp`, `worldmirror.cpp`,
`components/rtx/view/offscreentrace.*`, `components/rtx/mirror/released.hpp`, and
`apps/openmw_tests/mwrender/`.

- [ ] `rtxrenderer.cpp:507`: `renderGui` steps from Gui, Between, Walking, Views, Tracing and Run,
  because `renderFrame` leaves through it from Views, Walking, Tracing and Run (`:798`, `:838`,
  `:1057`). A run hook that presents from inside the frame passes the assert, and the frame presents
  twice. About 15 entry points assert nothing (`applyChangedSettings`, `applyViewMask`,
  `createWorldView`, `createMapOverlay`, `poseForIntersection`, `setVSync`, `awaitShaders`, …), which
  contradicts `architecture.md`'s "every entry point asserts". Target: `renderFrame`'s exits call a
  private `present()` from an explicit end-of-frame phase, the public `renderGui` accepts only
  `Between`, and the other entry points assert their phases.
- [ ] `tracedview.cpp:316-328`: `mCopyState = Recorded` follows `traceInto` unconditionally, but
  `traceInto` (`offscreentrace.cpp:171-175`) records nothing when `describeCamera()` refuses or the
  backend sees a zero extent (`vulkanrenderer.cpp:925-927`). The global map then gets the previous
  picture as this one's, or a paint waits forever in `PendingPaints`. Target: `traceInto` returns
  whether it recorded, and `TracedView` moves to `Recorded` only on true.
- [ ] `rtxrenderer.cpp:755`, `:1049-1064`: `mRun.frame` and `FrameTimer::addFrame` run only inside
  `if (since.has_value())`, which is false for the first `renderFrame`. A save or `--skip-menu` start
  traces the world there, so the run never sees that frame. Target: a traced frame always reaches
  the run, and only `FrameSpend[Frame]` is optional.
- [ ] `released.hpp` `Released::take` (`taken.reserve(mHeld.capacity()); taken.swap(mHeld)`): after
  a crossing that let go of N objects, every frame that releases one object allocates N × 8 bytes
  plus a `make_shared`, and frees it on the unref worker. For 30k objects that is 240 KB per
  releasing frame, twice with `mPictureReleases` (`worldmirror.cpp:189-201,359`,
  `rtxrenderer.cpp:488,797`). Target: hand over exactly what was kept and `clear()` the sink.
- [ ] `apps/openmw_tests/mwrender/renderer.cpp:194-222`, `:232-263`, `:265`: the tests sleep and
  assert wall-clock bounds (`EXPECT_LT(…, 2ms)`, `EXPECT_GE(slept, 4ms)`), and the comments record a
  Windows runner at 5.9 and 10.5 ms. Target: an injectable sleep and clock on `Renderer`, with exact
  assertions on the requested hold and the answered step.

### 4. Smaller correctness fixes in the core and the backend

Files: `components/rtx/scene/instancerecord.cpp`, `components/rtx/renderer/renderer.hpp`,
`components/rtx/image/textureformat.hpp`, `image/alphaimage.cpp`, `common/radicalinverse.hpp`,
`shaders/lib/traversal.glsl`, `shaders/lib/fog.glsl`, and their tests.

- [ ] `instancerecord.cpp:306`: `osg::Matrixf::inverse(T) * previous` runs in float, while `:170`
  inverts in double and `frame/camera.cpp:190-193` does too. At coordinates near 10⁵ the motion is
  off by 0.01–0.03 units, which is 0.1–0.3 px for an actor 100 units away at 1080p. Target: the
  inverse and the product in double, rounded once. Test: a placement at x ≈ 1.5e5 moved a small
  step, whose motion is the step exactly.
- [ ] `renderer.hpp:265-269`: `sMaxGpuZones = 40`, but its comment's case is 37 zones + 1 + 5 = 43.
  Past the cap `GpuTimer::open` (`gputimer.cpp:71-73`) drops the zone in a release build, and the
  last opened are `Digest` and `Stress`, which `QueueHeld` reads (the failure `04ac0625de` fixed).
  Target: derive the cap from `sFrameZoneCount` and the placement span of the enum.
- [ ] `textureformat.hpp:288-289`: BC7 rows claim `readsColour`, so `texelAt`/`readTexelBand` would
  read a BC7 block's last 8 bytes as a BC1 colour half. Latent: BC7 descriptions carry no bytes
  (`scenetextures.cpp:166-177`). Target: `mColour = false` for BC7.
- [ ] `alphaimage.cpp:146,150`: BC3 interpolated alpha uses truncating `/7` and `/5`, 3/7 LSB low on
  average, where `widenChannel` (`imagedescription.cpp:130-134`) rounds to what a device samples.
  Target: round, `(… + d/2) / d`.
- [ ] `radicalinverse.hpp:12-23`: `place /= base` in float adds rounding for base 3 (the jitter at
  `frame/camera.cpp:232`). Target: reverse the digits as an integer and multiply once by `base⁻ⁿ`.
  `apps/components_tests/rtx/world/fogbuilder.cpp:120` then drops its own double copy.
- [ ] `traversal.glsl` `resolve`, the stacked branch: `layerBitangent = cross(normal, (1,0,0))` has
  length `sqrt(1 - n.x²)`, so on a 45° slope along x a replacer's terrain normal map tilts 0.71 as
  far along y as it should. The comment keeps it for parity with `terrain.vert`. Decided: the ray
  tracer is right where the rasterizer is not. Target: `cross(normal, layerTangent)`, and the comment
  says why the frame differs from the rasterizer's.
- [ ] `fog.glsl` `fogColumnOver`: the midpoint is taken below `|climb| < 1e-4`, but the two errors
  cross near 0.015 (difference ≈ 3e-7/|climb|, midpoint ≈ climb²/12). Between 1e-4 and 1e-2 the code
  takes the worse form by up to 10³. Target: `exp(-(enters+leaves)/(2H)) · sinhc(climb/2)` with a
  short series, or at least the threshold moved to about 1e-2 with the comment corrected.

### 5. The harness's measurements

Files: `apps/rtxtool/measurer.cpp`, `model/measurewindow.*`,
`instruments/framehashes.cpp`, `main.cpp`, `model/runrecord.cpp`, `model/benchspec.hpp`,
`options.hpp`, and `apps/components_tests/rtxtool/`.

- [ ] `measurer.cpp:188-193`: `Crossings::add` takes frame N's upload kind with the raw
  `report.mSpend.at(Frame)`, which is render(N−1) + update(N). Samples and arrivals use
  `MeasureWindow::Taken::mClosed` (`:168-169`). The route report's crossing line measures the frame
  before each rebuild and disagrees with the worst-frames line. A crossing at the last measured frame
  loses its rebuild. The host rows cover [first−1, last−1] while `GpuBreakdown` covers [first, last].
  Target: `WindowFrame` gains the cell identity and the upload kind, `Taken` returns a closed
  crossing, and `Measurer` stops reading `MWBase::Environment` (`getPlayerPtr().getCell()`) and
  `Stager::describePause()`. Test the pairing beside `aFrameTimeClosesTheSpanTheFrameBeforeWorkedIn`,
  with the host/GPU alignment and the film's frame numbering across results that come back out of
  order.
- [ ] `measurer.cpp:368-369`: the pause message divides by `getMeasured(step)`, not by the frames
  measured. Target: `mProgress.mSamples.size()`.
- [ ] `framehashes.cpp` (about `:460-470`): the reference's unmatched frames are
  `refRows - difference.mFrames`, not `refRows - matched`. With a reference of frames 1..10 and a run
  of 5..14 it reports 4 frames not shared where 8 are. A view drawn in two stretches is compared
  twice. The verdict stays right. Target: count matched frames in the per-frame loop.
- [ ] Stale outputs: `RunRecord::begin` (`runrecord.cpp:28-31`), `commandNoise` (`main.cpp:1027`),
  `clearPictures` (`main.cpp:787`) and `clearFrames` each clear their own outputs. `bench
  --frame-times` (`main.cpp:834-845`) and `bench --pictures` (`measurer.cpp:286-290`) clear none, so a
  failed suite leaves the last run's series and pictures beside its own. `shot` and `bench` repeat the
  `out`/`against`/`create_directories`/`checkAgainst`/`hashInto` sequence, and `out()` is defined
  three times. Target: the request names its outputs, and `RunRecord::begin` removes them all.
- [ ] `main.cpp:499-509` (`specFrom`): `--frames` and `--seconds` together silently take the frames,
  while `filmLengthFrom` (`options.hpp:68-72`) refuses both. Target: `BenchSpan`
  (`model/benchspec.hpp:14-35`) as a `std::variant` of frames and seconds, and `specFrom` refuses
  both.
- [ ] `apps/components_tests/rtxtool/perffifoposix.cpp:131`: `sleep_for(100ms)` stands in for a late
  reader. Target: open the reader after the writer's first `ENXIO`, through a hook or a latch.

### 6. The interface's refused slot and the presenter's patience

Files: `components/myguirtx/paintedmirror.cpp`, `slottexture.*`, `texture.cpp`,
`components/rtxvulkan/gui/guitextures.cpp`, `components/rtxvulkan/present/swapchain.cpp`,
`presenter.cpp`, and their tests.

- [ ] `paintedmirror.cpp:24-46`: after `take` leaves the slot none (a side past the device's,
  `guitextures.cpp:41-49`), `refresh` calls `lendGuiTexture(getSlot(), …)` and `sendGuiTexture`
  without a check. `send` (`guitextures.cpp:93-120`) indexes `mImages[0xFFFFFFFF]`: out of bounds in
  a release build. `take` runs again and logs on every draw. `Texture::lock` throws on a refused slot
  (`texture.cpp:105-107`), against `add`'s promise that an oversize image fails one texture and not
  the game. A global map wider than 16384 reaches it. Target: the refusal is a state of
  `SlotTexture`, remembered per size, and the write goes through one `SlotTexture` method that does
  nothing on none. Test `PaintedMirror`: the first draw whole, then the painted rectangle, then a
  refused size that draws nothing.
- [ ] `swapchain.cpp:249`: the acquire waits `sPatience` (`device/result.hpp:45`, 10 s), the
  lost-device patience, although `Acquired::NotYet` is documented as normal. A compositor that
  withholds images from a shown window stalls the main thread 10 s a frame. Target: an acquire
  patience of its own, about the 100 ms `awaitLatency` uses.
- [ ] `swapchain.cpp:315-316`, `presenter.cpp:115-149,334-336`, `vulkanrenderer.cpp:591-595`:
  `VK_SUBOPTIMAL_KHR` gives `Stale`, and the next frame drains and recreates. A driver that answers
  suboptimal after a fresh recreate (Mesa's X11 copy path) rebuilds every frame. A lost surface has a
  guard (`mRemade`). Target: rebuild once per extent, show a suboptimal image after a fresh rebuild
  at the same extent, and log it once.
- [ ] `apps/components_tests/rtxvulkan/present/presenter.cpp`: test out-of-date and suboptimal into
  stale, and a hidden surface keeping its staleness (`wantsResize`'s `Hidden` branch), driven as the
  lost-surface test is.

### 7. Lua cursors and the window's density

Files: `apps/openmw/mwgui/windowmanagerimp.cpp`, `components/sdlutil/sdlcursormanager.cpp`,
`sdlinputwrapper.cpp`, `sdlgraphicswindow.cpp`. These are the SDL3 port's own hunks.

- [ ] `windowmanagerimp.cpp:1343` (`fitCursors`): the guard compares `mScalingFactor * shown`, which
  `layOutInterface` (`:1331`, `:1367ff`) makes `setting * display`, constant across a change of frame
  or window. `mLuaCursorScale = shown` is never compared. Picking 1280×720 in a 2560×1440 window and
  then Native leaves every Lua cursor at its old size. After `dropCursors`, `createCursor` applies the
  current cursor again only on an exact name match (`sdlcursormanager.cpp:185`), so a name resolved
  to the `"arrow"` fallback stays on SDL's default. Target: compare the inputs (interface scale, Lua
  scale, display scale) as one struct, end a refit with one `_setGUICursor(mCurrentCursor)`, and
  test the "what must be rebuilt" rule as a pure function.
- [ ] `windowmanagerimp.cpp:1870` (`createLuaCursor`): a second registration under one name updates
  `mLuaCursors`, but `createCursor` returns early (`sdlcursormanager.cpp:161`), so the old image shows
  until the next refit. Target: registering a name replaces its cursor.
- [ ] `sdlinputwrapper.cpp` `_setWindowScale`: `SDL_GetWindowPixelDensity` answers 0 on failure,
  which sends every mouse position to (0, 0) and every warp to infinity. `displayScale()` guards the
  sibling call; `GraphicsWindowSDL::setWindowRectangleImplementation` reads it raw too. Target: one
  `SDLUtil` helper that answers 1 where SDL cannot say.

### 8. Device memory: residency and accounting

Files: `components/rtxvulkan/texture/texture.*`, `texturearrival.*`, `scene/devicescene.cpp`,
`scene/sceneacceleration.hpp`, `device/commands.cpp`, `device/pipelinecache.*`, `vulkanrenderer.cpp`,
`components/rtx/renderer/memoryreport.*`, `renderer.hpp`, `apps/rtxtool/model/benchrecord.cpp`.

- [ ] `texture.hpp:333`, `texturearrival.hpp:205-234`: every `DeviceScene` (the world, the doll, the
  race preview, each picture) owns a `TextureArrival` whose rooms (`mSums`, `mBlocks`, `mMeans`,
  `mCanvas`) grow to its largest group and stay. A 4096² normal map's means are about 89 MB and the
  canvas 64–89 MB Essential: up to 150–200 MB per scene that met a large PBR file. Arrivals for every
  scene record on one queue in order. Target: one `TextureArrival` beside `TexturePasses` in
  `ScenePasses`, handed to `TextureArray::write` at the call. This is scratch, not the declined
  "textures shared across scenes".
- [ ] `texture.cpp:118-129,436`: each array allocates two sets at the layout maximum, about 4.8 MB of
  pool per scene on RADV, where `sBindings`' comment says the maximum is paid once. Target: the pool
  is the passes', shared like the arrival.
- [ ] `texture.cpp:480-520`, `texturearrival.cpp:66-83`, `commands.cpp:335-378`: an arrival stages
  all its files in one batch, and the ring is trimmed only after `setScene`
  (`vulkanrenderer.cpp:396`). A PBR crossing of 400–800 MB copies 40–80 ms on the main thread and
  keeps that much host-visible memory for the session. Target: trim to `sStagingKept` once the
  arrival's blocks retire, on a frame that hands nothing over. Optionally, write the texture bytes
  straight into lent staging (`Batch::reserve`). No frame-budget scheduler (declined in
  `architecture.md`).
- [ ] `pipelinecache.hpp` `mLoaded`, `pipelinecache.cpp:231,241-242,323,365`: the loaded blob (20–80
  MB) stays all session only to compare at save. Target: keep its size and `digestOf(blob)`, and drop
  the vector after `vkCreatePipelineCache`.
- [ ] `devicescene.cpp:202-226`, `sceneacceleration.hpp:142-152`: `SceneStats` misses `mRowTable`
  (32 MiB), `mPacked` (16 MiB), `mStarts`, `mPoses` (3 MiB), `mTopLevelScratch`, `mRefitScratch`,
  `BottomLevelStore::mScratch` (64 MiB+), the arrival rooms and the pool: about 120–150 MiB.
  Target: each owner answers `getBytes()` over everything it allocates, and `readStats` sums owners.
- [ ] `memoryreport.hpp:16-86`, `memoryreport.cpp:24-46`: `HeapUse` is a `VkMemoryHeap` with its
  `DEVICE_LOCAL`/`HOST_VISIBLE` bits, and `sMaxHeaps = 16` is `VK_MAX_MEMORY_HEAPS`. Target: the core
  reports by use and by three pools (video, system, host-written aperture: budget, held, reserved,
  live). The per-heap lines become a backend diagnostic in `describeDevice`.
- [ ] `apps/rtxtool/model/benchrecord.cpp:500-566` (`writeJson`): `BenchPlace::mMemory` and
  `mContent` (filled at `measurer.cpp:419-420`) reach the prose report and not the record. Target:
  `asJson(MemoryReport)` and `asJson(ContentMemory)` per place, in the new shape, and a round-trip
  test that holds every member.

### 9. The world frame's owner

Files: `components/rtxvulkan/vulkanrenderer.*`, `picturetracer.*`, a new `frametracer.*`,
`trace/tracechain.*`, `trace/tracemedia.*`, `trace/wavepass.*`, `trace/visibilitypass.*`,
`trace/spritebin.*`, `framering.*`, a new `components/rtx/frame/framedescriber.*` (or under
`renderer/`), `apps/openmw/mwrender/rtx/rtxrenderer.cpp`, and their tests.

- [ ] `vulkanrenderer.cpp:682-745,878,907-918`, `picturetracer.cpp:103-105`, state at
  `vulkanrenderer.hpp:258,263,268`: the frame's composition and its carried state are core arithmetic
  in the backend. The air clock has two owners (`rtxrenderer.cpp:1043-1044`, `:765-766`). Target:
  `Rtx::FrameDescriber` as in the proposal, owned by the backend, with a core test of the composition
  (the loss fold, the transparent backdrop below alpha one, no sky shadows in pictures).
- [ ] `vulkanrenderer.hpp:202-314`, `vulkanrenderer.cpp:70-107,206-304,705-879,996-1022`: the world's
  chain, arena, declarations, upscaler, previous camera and shown radiance sit loose in 29 members,
  and `createTargets` repeats `PictureTracer::resize` (`:281-294`). Target: `FrameTracer`, the twin
  of `PictureTracer`.
- [ ] `wavepass.hpp` (`record(...) const`, `mutable std::optional<osg::Vec2f> mSynthesised`),
  `tracemedia.hpp` (`const WavePass& getWaves() const`), `tracechain.cpp:181-185`: the chain
  rewrites every wave tile through a const reference, and which chain synthesises depends on the
  recording order. Target: `TraceMedia::synthesiseSea(commands, waterTime, timer)` beside
  `stepRipples`, called by both owners before the chain records.
- [ ] `tracemedia.cpp:9` → `visibilitypass.hpp`, `visibilitypass.cpp:53` → `tracemedia.hpp`: a cycle
  through `TraceSubject`. `VisibilityPass` knows each medium's images, samplers and block fields
  (`visibilitypass.cpp:551-586,615-633`). `TraceSubject::mTraceSlot` travels through
  `TraceMedia::describe`, which never reads it (`tracechain.cpp:199-200`). Target:
  `tracesubject.hpp`; `TraceMedia::bindInto(DescriptorWrites&)` and
  `describeFrame(VisibilityConstants&)`; the bin slot on `TraceRecording`.
- [ ] `framering.hpp:150-151`, `framering.cpp:154`: a throw inside a recording is described as
  recoverable ("the next trace begins it again"), but `GBuffer::begin`, `FogVolume::turn`,
  `DenoiseHistory::turn`, `RipplePass`, `WavePass`, `SpriteBin` and `mPreviousCamera` committed
  their turns already. The next frame would bind held surfaces in `UNDEFINED`. No caller catches.
  Target: a throw out of a recording ends the renderer, and the header says so.
- [ ] `picturetracer.hpp` (`mMedia`, `mDisplay`, `mTextures`), `tracechain.hpp` (`mPasses`),
  `spritebin.hpp` (`mShading`, `mPass`), `vulkanrenderer.hpp:288`: siblings held by reference beside
  siblings passed at the call. Target: handed at the call.
- [ ] Stale comments: `framering.hpp:173-174` and `apps/components_tests/rtxvulkan/frames.cpp:129-130`
  say an arrival waits (`extendScene`, `vulkanrenderer.cpp:408`, does not); `visibilitypass.cpp:744`
  says three launches (two); `framestep.hpp:12` says a picture lacks the last three steps (it lacks
  Digest and Upscale).

### 10. The core's renderer interface

Needs batch 9. Files: `components/rtx/renderer/renderer.hpp`, `sceneuploader.*`,
`components/rtx/view/offscreentrace.*`, `components/rtxvulkan/vulkanrenderer.hpp`,
`apps/components_tests/rtx/support/countingrenderer.hpp`, and the tests that read the backend.

- [ ] `renderer.hpp:489-686`: one interface of about 40 methods. `SceneUploader::hand` uses six,
  `OffscreenTrace` five, and `CountingRenderer` overrides all 40. Target: a scene holder, a picture
  interface and `GuiRenderer`, which `Renderer` inherits. Each consumer takes the one it calls.
- [ ] `renderer.hpp:434`: `traceGuiTexture` carries its scene in `GuiTraceOptions::mScene`, default
  `SceneSlot::world()`, where every other scene call takes a leading `SceneSlot`. A caller that
  forgets traces a doll against the world. Target: a leading `SceneSlot` parameter.
- [ ] `renderer.hpp:492,502`, `sceneuploader.cpp:116-119`: `setScene` builds and places, while
  `extendScene` appends and does not place, and the uploader encodes that as a missing `placeScene`
  on one branch. Target: `setScene` builds only, and all three branches end in one `placeScene`.
- [ ] `renderer.hpp:209-213` (`SceneHeld::mTextureCount`, written at `devicescene.cpp:198`): only
  tests read it. Target: remove it. The GPU test reads the count through the backend's probe, and the
  uploader test through the descriptions `CountingRenderer` recorded.
- [ ] `vulkanrenderer.hpp:118-174`: 13 test-only members on the shipping type (`setSea`,
  `setFogField`, `readChannel`, `readComposite`, `readShownRadiance`, `readShown`, `readGuiTexture`,
  `takeValidationErrors`, `getDevice`, `getPictureRoom`, `getTracePasses`, and the block-taking
  `renderFrame` and `traceGuiTexture`), and seven one-line forwards to `mGui.getTextures()`. Target:
  a friend probe type declared in the header and defined in the test tree.

### 11. Device ownership and submission

Files: `components/rtxvulkan/device/memory/*`, `device/owned.hpp`, `device/instance.hpp`,
`device/device.*`, `device/graveyard.*`, `device/readstamp.hpp`, `device/commands.*`,
`device/physicaldevice.cpp`, `device/requirements.*`, `pipeline/shadercode.*`,
`pipeline/tracepipeline.hpp`, `texture/texturearrival.cpp`, and their tests.

- [ ] `memory.cpp:118-122`: `DeviceMemory::release` frees at once. `Buffer::bury` (`buffer.cpp:157-161`),
  `Image::bury` (`image.cpp:228-236`), `ImageArena::bury` (`imagearena.cpp:103-111`) and
  `TextureArrival` (`texturearrival.cpp:42-43,273-274`) bury it by hand, and `ImageArena` writes its
  moves for that alone (`imagearena.hpp:84-87`). An owner that forgets frees memory under a frame in
  flight. Target: `DeviceMemory` buries itself, the graveyard keeps an inner "release now" form, and
  the owners default their destructors and moves.
- [ ] `owned.hpp:39-222`, `instance.hpp`: `Owned`, `Immediate` and `Root` are three near-identical
  templates with written-out moves, and `Instance::Messenger` is a fourth with a move-assign and no
  move-construct. Target: one handle template with an end policy (graveyard, immediate with parent,
  parentless).
- [ ] `accelerationstructure.hpp:15-17`, `accelerationstructure.cpp:69-111`, `device.hpp:119-145`,
  `device.cpp:215-229`: the comment says `Owned` cannot hold the structure, but under
  `VK_NO_PROTOTYPES` every destroyer is a volk pointer (`owned.hpp:37`). The backend has two dispatch
  paths: volk for push descriptors, present waits and timestamps, a per-device table for the ray
  tracing calls. Target: `Owned<VkAccelerationStructureKHR, vkDestroyAccelerationStructureKHR>` plus
  its room; required extension calls through volk with the one null check at creation; optional ones
  (checkpoints, fault info, markers, labels) keep an "offered" flag.
- [ ] `image.hpp:92`: `Image(Image&&) = default` leaves a husk that answers its width, format and
  stamp, where `Buffer`'s move empties its husk (`buffer.cpp:126-136`). Target: one rule for both.
- [ ] `readstamp.hpp`, `Buffer::nameForNext`, `image.cpp:244`, `graveyard.cpp:33-39`,
  `commands.hpp:194,360-436`: every stamp and burial reads "the next submit" at record time. A
  `Batch` open across submits (`GuiTextures::mBatch`, `guitextures.cpp:26`) names its images for a
  submit that is not the one it rides, so `ReadStamp::isIdle` answers early.
  `GrowableBuffer::outgrowKeeping` (`growablebuffer.cpp:41-49`) buries its old buffer at exit and not
  through `batch.keep`. Latent today. Target: a batch owns its stamp and applies names and burials at
  hand-over, and `outgrowKeeping` calls `batch.keep(std::move(old))`.
- [ ] `commands.hpp:13-14` ↔ `image.cpp:13`, `growablebuffer.cpp:7`, `blockedbuffer.cpp:7`: a cycle
  between the command pool and the memory wrappers. `Image::read` and `makeStandIn` submit work.
  Target: those two become functions over `CommandPool` in `commands.*`; `GrowableBuffer`,
  `BlockedBuffer`, `SlotTable` and `SlotBlocks` move to a folder after `commands`.
- [ ] `shadercode.cpp:54-77` (`bindingDisagreement`), `shadercode.cpp:164-176`: `stage()` is never
  told the module's stage, so a binding or a push range whose `stageFlags` exclude it passes
  (VUID-VkComputePipelineCreateInfo-layout-07988). Target: `stage(module, VkShaderStageFlagBits, …)`
  checks both, with a row in `RtxPassBindingsTest`.
- [ ] `physicaldevice.cpp:169-173`, `requirements.cpp:328-338,356-361`: every candidate is asked the
  ray-tracing feature and property structures before its extensions are read (`:209`), which the
  code's own PCI rule (`requirements.hpp`, `:210`) calls invalid. An Intel iGPU, lavapipe or a non-RT
  RADV card is asked. Target: read the extensions first, and build the chain from what is present.
- [ ] `device.hpp`: "Read by the tests and by nothing else" stands above `describeFault` and
  `describeMarkers`, which `result.cpp` calls. Target: move the comment to what it describes.
- [ ] Tests: `Image::buildMips` (`image.cpp:328-386`): exact box values per level, chains of
  different depth stepping together, the last level the mean. `outgrowKeeping`: content kept across
  a growth, rows staged after it landing in the new buffer, the old buffer alive to the submit.
  `apps/components_tests/rtxvulkan/device/stagingfit.cpp` joins the staging tests in `commands.cpp`
  as one table.

### 12. The core's image facts

Files: `components/rtx/image/*`, `components/rtx/preprocess/*`, `components/rtx/common/scratch.hpp`,
`components/rtx/environment/skybuilder.cpp`, `apps/openmw/mwrender/rtx/skyreader.cpp`,
`components/rtx/mirror/materialresolver.cpp`, `apps/components_tests/rtx/support/`, and their tests.

- [ ] `texels.cpp:75-155` (`readTexelBand`, `meanTexel`), `colour.cpp:85-100`: `meanTexel` calls
  `toLinear` on every texel. BC1/BC3 interpolated entries are never k/255, so about 1.5 `powf` per
  texel, and `AlphaImage::build` decodes every block before. A 512² sheet costs 6–10 ms on the frame
  that first meets a glow (`emitterresolver.cpp:362`, `materialresolver.cpp:555`).
  `ImageFactCache`'s comment records 11 ms at seyda-neen. Target: one per-block walker that reads
  alpha and colour together and linearises the four-entry palette once (6 `powf` a block),
  shared with `ShadingMap`'s `readTexels`/`blockSum` (`shadingmap.cpp:43-103`).
- [ ] `imagedescription.cpp:473-506` (`layImage`): widens into scratch, then `new[]` and copies.
  Target: widen straight into the returned image (`laidBytes()` gives its size).
- [ ] `materialresolver.cpp:244-259`, `texturepass.cpp:20-24`, `alphaimage.cpp:281-303`: the texture
  passes widen the original image's finest level again, and `SolidReach` and `TexelMean` each keep a
  `FinestTexels`. For a 2048² 24-bit TGA that is 62–78 MB written where 22 MB is needed, and about
  56 MB of scratch held per `ThreadContent`, two of them. Target: the passes read the laid image;
  `solidReachOf` answers from `FormatTraits` for a format with no alpha, and otherwise reads the
  source alpha with an early exit.
- [ ] `imagefactcache.hpp:34-48`, `laidimagecache.hpp:18-25`, `laidimagecache.cpp:24-33`: one cache
  keys by name alone, the other lays again when the object under a name changed. The VFS is fixed
  for the session and `EmbeddedImage` keys are `model#record`. Target: a name identifies its texels;
  `LaidImageCache` answers with any live laid copy and drops its `mFile` observer.
- [ ] `imagefactcache.hpp:53`, `imagefactcache.cpp:24`: an unbounded `std::unordered_map` on the
  frame's `ThreadContent`, which rehashes thousands of nodes on one frame (20–100 µs on MFR). Target:
  reserved from the content's texture count at attach, or a reserved flat map.
- [ ] `skybuilder.cpp:38-72` (`addCloudSheet`), called per frame from `skyreader.cpp:96-102`: a
  script-named sheet is opened, decoded and averaged on the frame thread. Target: the ring's reader
  reads it, and the deck takes the reading when it arrives.
- [ ] `scratch.hpp:114-123`: `Spares` reserves `mAll.size()` for each new object, O(N²) copies (10k
  models: 10k reallocations). `scratch.hpp:33`: `Recycled::give` pushes without a reserve, on the
  frame side (`cellplacer.hpp:304-333`). Target: geometric growth in `Spares`; `Recycled` reserves to
  the count it made on a fresh `take`, so `give` never allocates.
- [ ] `shadingmap.hpp/.cpp` (about 320 lines): only tests read it, and "what the texture sheet draws"
  is stale. Target: move to `apps/components_tests/rtx/support/` beside `MipChain`, keeping the
  shared walker in `image/`.

### 13. The mirror's materials

Files: `components/rtx/mirror/materialresolver.*`, `chainkeys.cpp`, `sceneextractor.*`, and their
tests.

- [ ] `materialresolver.hpp:207-224`: `HeldMaterial` holds `std::optional<Worn>`, 304 bytes inline,
  which only tens of animated materials use. The world extractor's 30 720 slots take about 10.5 MB,
  each picture extractor 660 KB. Target: `Worn` in a pool with one row per animated material, and a
  4-byte index in the slot.
- [ ] `materialresolver.cpp:99-182,311-351`, `sceneextractor.cpp:401-410,578-589`: the `inherits`
  path copies a node's state set per placement only for its address, which `ChainKeys` already makes
  unique. `describeSurface` folds the same content twice each frame, a glow over an NPC allocates
  30–60 state sets on its frame, and a node whose state set the game replaces keeps its old surface.
  Target: drop the copy, and `resolve` reads again when the near link's `mAnimatedThrough` is set.
- [ ] `materialresolver.cpp:102-139`: upstream hangs a `LightListCallback` on every object root
  (`animation.cpp:1650-1653`), so every reference gets an `mAnimated` entry: 3000–9000 in a dense
  3×3, past the 4096 reserve, a rehash on the load frame. The comment "nearly all of a cell hangs off
  no callback at all" is false. Target: classify callbacks once by class, as `NodeKinds` classifies
  nodes, and make an entry only where a chain holds a `StateSetUpdater`.
- [ ] `chainkeys.cpp:156-186` (`retire`): each pass of the loop copies and sorts the whole sink, 3–5
  passes over about 10⁴ objects on a crossing frame, 0.5–1 ms. It reads `referenceCount()`, which the
  unref worker can move for keys handed over earlier. Target: an explicit holder count per pair, and
  one worklist pass.
- [ ] Tests: a node under an animated chain whose state set the game replaces. Extractor tests that
  need less use `sSubjectExtractorRoom`, not the 20 MB `sWorldExtractorRoom`.

### 14. Shader structure

Files: `components/rtxvulkan/shaders/trace/denoise/*`, `shaders/lib/*`, `shaders/shared/*`,
`shaders/texture/bc7encode.comp`, `components/rtx/shaders/digest.h`, `colour.h`,
`components/rtxvulkan/digestpass.cpp`, `trace/tracechain.cpp`, and their tests.

- [ ] `composite.comp:105-146`, `lib/stage.glsl:242-259`: a split pixel resolves up to four
  neighbours whole (13 loads each), and each neighbour does the same for it: about 34M extra loads a
  frame at 1707×960 with 40% water. The branch states no measurement. The blend rule is written
  twice (light and lift). It is why a sea frame cannot compose in the cascade's last level
  (`tracechain.cpp:244-247`). Target: the workgroup resolves its tile with a one-pixel apron into
  shared memory once and blends without a branch; one helper in `lib/pixels.glsl` holds the mask and
  the blend; the composing level then takes sea frames. Measure.
- [ ] `atrouslevel.glsl:162-170,247-259,373`: "nothing to share" is false at steps 2 (4× reuse) and 4
  (2.25×); only step 8 shares nothing. Each tap rebuilds 3 loads, an octahedral decode and a ray.
  Target: a tile sized by the step as a specialization constant, as `shadowfilter.comp` sizes its
  apron. Measure. At least correct the comment.
- [ ] `specular.comp:144-170` against `pane.comp:86-104`, and `accumulateclamp.comp:185-205,238-242,249`
  against `historyclamp.comp:114-140`: the running-mean gather and the 5×5 box statistics are each
  written twice and have drifted (one clamps the fast push at 0). Target: a macro beside
  `RTX_HISTORY_SHARES` and a shared `squareStatistics`, with kernels supplying their loads.
- [ ] `specular.comp:132-136`: the branch's "not measured, no scene wears a map" no longer holds; the
  PBR profile exists. Target: measure on `~/.config/openmw-pbr` and name the result, or select.
- [ ] `lib/shading.glsl:36-45,1078-1084,1339,1367,1392`: `EVALUATED_*` and `bounceEscape`'s
  `evaluated` take one value; `(EVALUATED_GATHERED & EVALUATED_LAMPS) != 0u &&` is constant true; the
  comment about water's flags is false. `eyeshading.glsl` `stepEye`'s `shaded` is always true;
  `gloss.glsl` `reflectionAt`'s `widened > 0.0 ? … : 1.0` cannot take its else. Target: remove them.
- [ ] Includes that name nothing: `lights.glsl:18` → `sky.glsl`, `fog.glsl:27` → `underwater.glsl`,
  `water.glsl:16` → `shading.glsl`, `peel.glsl:13,25` → `eyeshading.glsl`/`water.glsl`,
  `reproject.glsl:8` → `records.glsl`, `sea.glsl:13` and `stage.glsl:16` → `random.glsl` (they name
  `hash.glsl`), `eyeshading.glsl` and `bounced.glsl` → `stage.glsl` (they name `stagerecord.glsl` and
  `pixels.glsl`). `underwater.glsl:5-8` gives a reason that no longer holds. Target: each file
  includes what it names, and the header states today's reason.
- [ ] `components/rtx/shaders/digest.h:24-62`: the dispatch's bindings, slots and workgroup, read only
  by the backend, sit in the core, while its push constants sit in `shared/channeldigest.h`.
  `DIGEST_DENOISER_FLOATS = 9` and `DIGEST_DENOISER_WORDS = 11` are literals no GLSL reads. Target:
  the core keeps the lanes, the image count, the seeds and the fold; the rest moves beside
  `DigestConstants`; the denoiser counts become `constexpr` in `digestpass.cpp`, derived from
  `DenoiserImage`.
- [ ] `bc7encode.comp:50`: `WEIGHTS[16]`, indexed dynamically at `:141,175,230`, is exactly
  `(64*i + 7) / 15`. The tree's rule (`atrouslevel.glsl:179-186`) is a select and not a table.
  Target: `bc7Weight(i)`.
- [ ] `shading.glsl:131-132,378`, `underwater.glsl:242-243,487`: `sunUnderWater(dir)` and
  `waterCrossingOf(dir)` depend only on the source's direction. Target: the host fills `SkySource`
  with them once a frame.
- [ ] `components/rtx/shaders/colour.h:146-185`: `toneMap` is GLSL-only, so
  `apps/components_tests/rtx/support/displaycurve.hpp:26-42` derives its stretch again by hand.
  Target: a scalar core shared by both sides (as `encodeSrgb` is split), called by `displayedLevel`,
  with exact table cases below the ramp, on it, in the stretch and in the compression.
- [ ] Stale comments: `shared/accumulate.h:15-25` and `accumulate.comp:16-19` describe a history
  surface of the pass's own in half floats; it is the held `CHANNEL_SURFACE`, `STORAGE_RG32F`, and
  two formats, not three.
- [ ] Tests: `packRgb9e5`/`unpackRgb9e5` (round trip, the 512 bump, the clamp, the −15 floor);
  `turnsAt`/`productError` at large `seconds.x` against double; `falloffAlong` against quadrature at
  `e ≥ 1` and a clipped chord; the YCoCg box clamp (`heldToFast`, `fastBoxOf`). In the style of
  `unormround.cpp` and `halfstore.cpp`.

### 15. The seam and the mwrender structure

Files: `apps/openmw/mwrender/renderer.*`, a new `createrenderer.cpp`, `rtx/rtxrenderer.cpp`,
`renderingmanager.cpp`, `ground.hpp`, `rtx/viewqueue.*`, `rtx/tracedview.cpp`,
`rtx/rippleemitters.*`, `framedescriber.*`, `globalmap.hpp`, `characterpreview.hpp`,
`apps/openmw/engine.cpp`, and `apps/openmw_tests/mwrender/`.

- [ ] `renderer.cpp:35,37`: the base class's code is compiled with `createRenderer`, which closes
  RenderingManager → Renderer → GlRenderer/GlWorld → RenderingManager and Renderer ↔ RtxRenderer.
  Target: `createRenderer` and `startRenderer` in their own file. Drop the unused
  `renderingmanager.hpp` include at `rtxrenderer.cpp:36`.
- [ ] `renderer.hpp:349-352`, `renderingmanager.cpp:902-906`: `notifyWorldspaceChanged` must come with
  `notifyCut`, but the ray tracer's already sets `HistoryLoss::Worldspace`, which holds a cut
  (`rtxrenderer.cpp:726`), and the rasterizer has no cut. Target: the worldspace change implies the
  cut, and `RenderingManager` makes one call.
- [ ] `renderer.hpp:203` (`groundReadsGates`), `ground.hpp:182` (`Ground::setGate`, a no-op default),
  `engine.cpp:224,642`: whether a ground hears gates is said twice. Target: ask the ground
  (`Ground::readsGates()`), or make `setGate` pure.
- [ ] `tracedview.cpp:218-222`: `static unsigned next` in `nextViewName()`. Target: the counter lives
  on `ViewQueue` and is handed in at `adopt`.
- [ ] `viewqueue.cpp:120-166`, `tracedview.cpp:299-365`, `rippleemitters.cpp:105-148`,
  `framedescriber.hpp:97-117`, `framedescriber.cpp:235-265`: pure scheduling rules tested only end to
  end. `ViewQueue` holds the concrete `TracedView`; `RippleEmitters` and `FrameDescriber` reach
  `MWBase::Environment`. Target: `ViewQueue` schedules through a small interface (`isOfWorld`,
  `waitsForGround`, `draw`, `abandon`); `RippleEmitters::update` takes the world's two predicates.
  Table tests: the world-view budget, deferral while the ring waits for ground, a view forgotten
  inside its own draw, `leaveScene` abandoning world views only, the `CopyState` transitions, the
  ripple rules, `FrameDescriber`'s reset to paused and its jump list.
- [ ] `globalmap.hpp:8-20`, `characterpreview.hpp:12-27`: the `osg` and `MyGUI` forward-declaration
  blocks are reordered, a cosmetic hunk `AGENTS.md` reverts. Target: upstream's order, keeping only
  the trimmed declarations.

### 16. Fork code in upstream files

Files: `apps/openmw/mwlua/postprocessingbindings.cpp`, `uibindings.cpp`, `camerabindings.cpp`,
`debugbindings.cpp`, `luamanagerimp.cpp`, `apps/openmw/mwscript/miscextensions.cpp`,
`mwscript/visibilitygates.cpp`, `mwrender/objectpaging.cpp`, `components/terrain/objectstorage.hpp`,
`components/platform/*`, `components/crashcatcher/*`, `components/misc/strings/conversion.hpp`,
`components/shader/automaps.*`. Each item changes a hunk the fork owns under an Accepted-diff entry
or the seam.

- [ ] `postprocessingbindings.cpp:208,228` and the `if (!shader.mShader)` guards: Lua asks for the
  chain through `getPostProcessor() == nullptr`, while F2 and the console ask
  `support().declinedSetting("Post Processing", "enabled")`. Target: Lua asks `support()`, and a
  missing processor becomes a `Crash::notNull` contract.
- [ ] `uibindings.cpp` (`screenSize`), `camerabindings.cpp`, `debugbindings.cpp`,
  `luamanagerimp.cpp` (`forgetScriptState`), `miscextensions.cpp` (`declines`): each reaches the
  renderer through `Environment::get().getWorld()->getRenderingManager()->getRenderer()`, while
  `WindowManager` and `InputManager` are handed it. Target: one direct route (the environment, or the
  window manager's `getPresentation()`/`support()`).
- [ ] `visibilitygates.cpp:19` includes `mwrender/objectstorage.hpp` for `handsOver`, and the four
  hand loops at the end of `VisibilityGates::build` must agree with `handsOver`'s `typeFilter`
  (`objectpaging.cpp:1136`). Target: the record-type set beside `Terrain::RefKinds`, from which both
  derive, the loops included.
- [ ] `fifowin32.cpp:6`: platform includes `crashcatcher/crash.hpp` for one `Crash::fatal` (`:20`),
  closing a cycle with `crashpadclient.cpp:26`, `crashimagelinux.cpp:22-24` and
  `crashpadmonitor.cpp:45`. Target: `write` throws the `std::system_error(not_supported)` that
  `openForWriting` throws.
- [ ] `processposix.cpp:25,75,120,135,149,163`, `crashpadclientposix.cpp:15,26,69,89,113,133`,
  `crashpadmonitorposix.cpp:23,44,64,93,128`: Linux and macOS chains inside the POSIX files. Target:
  `processlinux.cpp`/`processother.cpp`, `crashpadclientlinux.cpp`/`crashpadclientmac.cpp` behind
  `crashpadclientsystem.hpp`, and the same for the monitor, chosen in CMake as `crashimage*` is.
- [ ] `conversion.hpp:64`: `sFromCharsReadsFloats` is guessed from compiler macros, which sends clang
  with libstdc++ through the stream. Target: `__cpp_lib_to_chars >= 201611L` from `<version>`.
- [ ] `automaps.hpp` and `automaps.cpp`: the doc promises "the next free unit", but both maps take
  `units.size()`, which does not grow when `writable` is a shallow copy (`recreateShaders`). The
  collision is on upstream's raster-only path, so only the shared function's contract changes.
  Target: count from the state set written to, or narrow the doc.

### 17. The harness's layout

Files: `apps/rtxtool/run.*`, `options.*`, `noise.*`, `film.*`, `compare.*`, `picturemean.*`,
`hosted.cpp`, `main.cpp`, `model/*`, `instruments/*`, `tools/omw/kernels.py`.

- [ ] `run.cpp:78-114` (`shippedDefault`): a static inside a template instantiated four times, so
  `defaults.bin` is parsed four times against the comment's "once per process", the first `config`
  wins for every call, and an empty file is read on every call. Target: a `ShippedDefaults` value
  loaded once in `dispatch` and held by `Command`, with a test.
- [ ] `noise.cpp:24`, `film.cpp:29` → `run.hpp` → `options.hpp` (Boost) → `film.hpp`: a cycle, and
  `run.hpp` holds five responsibilities. `compare.hpp` and `picturemean.hpp` are instruments outside
  `instruments/`. Target: the schedule constants (`sHistoryFrames`, `sAirFrames`, `measureFrames`,
  `sDefaultHour`), `stopFor`, `loadViews` and `loadSuites` move to `model/` beside `BenchSpec`;
  `FilmLength` to `model/`; `compare` and `picturemean` to `instruments/`; `noise.cpp` splits into its
  plan (model) and its judge (instrument); `run.hpp` keeps `Command`, `Framed` and `runHosted`.
- [ ] `model/benchspec.cpp:10` → `blockfile.hpp` (for `trimmed`), `blockfile.cpp:18` →
  `benchrun.hpp`, `benchrun.hpp:21` → `benchspec.hpp`. Target: `splitNames` moves to `blockfile`.
- [ ] `kernels.py` `digest()`: each tuple writes `spirv-opt`'s output to a temporary directory and
  reads it back with `spirv-dis`. Target: `-o -` piped into `spirv-dis -`.

### 18. Tests and their harness

Files: `apps/components_tests/rtx/**`, `apps/components_tests/rtxvulkan/**`,
`apps/openmw_tests/mwrender/precipitation.cpp`, `apps/components_tests/rtx/sourcetree.cpp`.

- [ ] `apps/components_tests/rtxvulkan/framering.cpp:155-214`
  (`aPictureStandsPastTheFrameThatTakesItsSlot`): the check at `:201-203` reads the picture before
  frame 2's copy can run (it is waited at `:208`), so the regression it guards passes. Target: check
  after `ring.finishFrame()`.
- [ ] `support/device/harness.cpp:299-312`: `RendererTest::SetUp` resets the scene and the gamma
  alone, so upscale, sea, anisotropy, fog field and picture room leak between tests, each restored by
  hand (`visibility/upscale.cpp:41-44`, `visibility/fog.cpp:1334`, `visibility/frame.cpp:455-460`).
  `harness.hpp:141,205` still say the upscale mode is fixed at construction. Target: `SetUp` resets
  every setting a test can move, `setFogField` gains an equality early-out, and the comments match.
- [ ] `camera.mSkyZenith = osg::Vec3f()` is written 74 times across `trace/visibility/*.cpp`, with
  `darkCameraAt` (`specular.cpp:80`) and `darkEyeAt` (`pane.cpp:92`) as two diverging copies. Target:
  one `Testing::unlit(camera)` in `testcamera.hpp` that zeroes every source.
- [ ] `trace/wavefield.cpp:46-70,86-168` copies `WavePass`'s bindings and recording order, because the
  pass is fixed to `sWaveTiles`. Target: `WavePass` takes a tile description, and the test drives the
  production recording on its small grid.
- [ ] `gui/guitextures.cpp` (1298 lines): about half tests `PictureTracer` and the view path. Target:
  a `picturetracer` test file beside their fixture.
- [ ] Tests filed under other code: `rtx/common/result.cpp` tests `rtxvulkan/device/result.hpp`;
  `environment/skybuilder.cpp` tests `world/` in 8 of 13 cases; `addMoonFaces` in `world/moon.cpp:103`;
  `imagedescription` in `scene/scenetextures.cpp:290-760`; `ImageFactCache` and `TextureFormat` in
  `image/texels.cpp:200,285`; `colour.h` in `image/colour.cpp:122-216`; `causticGain` in
  `environment/wavecascade.cpp:278`; `toVulkanTransform` in `rtx/scene/instancerecord.cpp:12,24,354-364`
  (it includes `vulkan_core.h`); `packSurfaceNormal` in `rtx/renderer/frameimage.cpp:198-231`.
  Target: one test file per unit, mirroring its folder.
- [ ] `environment/skybuilder.cpp:487-491,535-539,600-604,650-654`, `environment/cloudmesh.cpp`,
  `openmw_tests/mwrender/precipitation.cpp`: the resource-manager fixture is copied five times, and
  `HeldScenes` is local (`skybuilder.cpp:566`). Target: one support fixture owning the VFS and the
  image, NIF, BGSM and scene managers, with `hold(image)` and `hold(node)`.
- [ ] `common/monitor.cpp:191` (`aWaitOutlastsAWorkersTurn`): a 250 ms sleep proves a negative.
  Target: assert `ready() || (mIdle && mBusy == 0)` directly while the worker is in its turn.
- [ ] `renderer/png.cpp:70-86`: the 16-bit round trip runs through one plugin and cannot see byte
  order. Target: inflate the IDAT (`support/pngtext.hpp` already parses chunks) and check sample 1 is
  stored `00 01`.
- [ ] `scene/scenedesc.cpp:286,699,1143`: the lowest-free-slot rule is tested three times. Target:
  one table over table × free order, keeping `:662`.
- [ ] `support/wavemoments.hpp:28-37,69-91`: the oracle sums 262k terms in float where the code it
  checks sums in double. Target: sum in double.
- [ ] `sourcetree.cpp:530-536`: a quoted include passes whenever the path exists, so
  `"../scene/x.hpp"` escapes the folder order, and no test checks `<apps/…>` from the core or the
  backend. Target: refuse a quoted path with `/`, and flag `<apps/` from both.
