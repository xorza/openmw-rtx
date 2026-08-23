# Review: the renderer split (`master`…`renderer`)

26 commits, ~9 800 insertions / ~4 900 deletions across 199 files. Reviewed by reading the whole
diff; nothing here was verified by running the game or the harness, and the few places where that
matters are marked **(verify)**.

## Verdict

**The seam is cut in the right place and cut well.** `MWRender::Renderer` + `MWRender::Stage` +
`SceneFrame` + `OffscreenView` is a genuinely renderer-neutral surface: 24 virtuals, none of them
naming a context, a bin, a descriptor set or a swapchain, and the two implementations sit either
side of it without either one bleeding through. `osgViewer::Viewer` is gone from thirteen call sites
and the game reads the frame stamp, the camera, the event queue and the stats through one object
that does not know what draws. `RenderingManager::describeWorld()` replacing twenty `stateUpdater->
setX()` calls with one settled struct is the single best change in the branch — it removes a
two-way channel and replaces it with one that points down.

Three things fall short of the brief:

1. **`RtxRenderer::renderFrame` has two early returns that skip `drawGui()` and `presentFrame()`**,
   and skip the extractor's epoch bookkeeping with them.
2. **`#ifdef OPENMW_RTX` appears in three places, not one.** Two are pre-existing and are asking a
   different question ("was it built"), but the question has an answer that does not need a macro.
3. **`PostProcessor`, `SkyManager` and `Water` live in `mwrender/gl/` and are named by nine files
   outside the renderer**, including `mwworld/weather.hpp`. The include path is the leak: every one
   of those files now says out loud which renderer it expects.

Performance is where the branch is weakest, and mostly on the GUI path: **four separate
submit-and-wait fences per frame** on the Vulkan side, an unbounded accumulation of local-map
framebuffers on the OpenGL side, and a whole-overlay upload on the frame a cell is explored.

---

## A. Architecture and separation

### A1. What is right

- `Renderer` names nothing below itself. The header's own argument — capabilities are a worse
  question than "give me the thing", so there is one capability left and it is a number — is correct
  and held to.
- `Stage` is the right decomposition of `osgViewer::Viewer`: the half that is not OpenGL, named.
  `adopt()` rather than construct-and-hand-over is the right call for the reason given (the viewer
  wires its visitors to its own frame stamp).
- `SceneFrame`/`WorldState` handed down rather than pulled up, in the world's own numbers, with the
  decode left to whoever needs it. `PostProcessor::describe()` and `RtxRenderer::renderFrame()` each
  spell the same twenty facts their own way and neither can drift from the other.
- `OffscreenViewSpec` genuinely names no rasterizer decision. `characterpreview.cpp` lost 417 lines
  of RTT plumbing and reads like game code now.
- `GuiRenderManager` + `Picture` + `AdditiveLayer` is the right shape: one `Platform`, one neutral
  blend switch, two backends.

### A2. Three `#ifdef OPENMW_RTX`, not one

| Site | Question it asks |
| --- | --- |
| `apps/openmw/mwrender/renderer.cpp:8,19` | which renderer to construct — **the intended one** |
| `apps/openmw/mwgui/settingswindow.cpp:303` | was the ray tracer built |
| `apps/launcher/graphicspage.cpp:103` | was the ray tracer built |

The last two are unchanged by this branch and are not "which renderer is running" — they grey out a
switch in a build that cannot honour it. But they are still the macro, outside the factory, and the
factory already knows the answer.

**Fix:** add one function beside `createRenderer`:

```cpp
/// Whether this build has a renderer under this name, so a switch that cannot be honoured can say
/// so rather than silently doing nothing.
bool rendererAvailable(std::string_view name);
```

Both sites become `if (!MWRender::rendererAvailable("raytrace"))`. The launcher does not link
`openmw-lib`, so it needs the same one-line answer from its own translation unit or a tiny shared
header — either way the `#ifdef` count drops to one, in `renderer.cpp`, where the review asked for
it.

### A3. `PostProcessor` — **narrowed to where it is defensible**

`Renderer::getPostProcessor()` returns a neutral-looking pointer, but its *type* lives at
`apps/openmw/mwrender/gl/postprocessor.hpp`, and nine files outside the renderer include it by that
path:

```
apps/openmw/mwgui/postprocessorhud.cpp        apps/openmw/mwlua/postprocessingbindings.cpp
apps/openmw/mwgui/windowmanagerimp.cpp        apps/openmw/mwrender/npcanimation.cpp
apps/openmw/mwlua/debugbindings.cpp           apps/openmw/mwrender/renderingmanager.cpp
apps/openmw/mwlua/luamanagerimp.cpp           apps/openmw/mwworld/scene.cpp
                                              apps/openmw/mwworld/worldimp.cpp
```

Every one of those now states in its include list which renderer it expects. The null check is the
honest part; the path is not.

**Nine files became five, and the five are the defensible ones.**

Counting what the outside actually uses: about twelve methods, a templated `setUniform` over five
types, and two enums. An abstract interface over that is a large refactor with no second implementer
— the ray tracer answers this question with `nullptr` and always will. So the line drawn instead is
about *who* may know:

- **`mwworld` must not**, and no longer does. `scene.cpp` reached in on every cell change to push
  `setExteriorFlag`, which is the same fact `WorldState::mSkyVisible` already carries down the frame
  channel; `PostProcessor::describe` now reads it there and the setter is gone. `worldimp.cpp`'s
  include was unnecessary — it returns a pointer through a forward declaration.
- **`mwgui` and `mwlua` may.** Post-processing is a documented, versioned user-facing feature —
  `openmw.postprocessing`, with its own API revision — and those five files are the interface and
  the script bindings *for that feature*. A window that lists shader techniques naming the object
  that holds them is not a renderer leaking upwards; it is the feature's own UI.

`mwrender/npcanimation.cpp` also names `gl/postprocessor.hpp`, for a `RenderBin::DrawCallback` doing
raw `glClear` and FBO binds — rasterizer code in a neutral directory. Moving it to `gl/` would only
swap one `gl/` include for another, because `NpcAnimation::setRenderBin` is what installs it and
that has to stay with the animation. Left alone, noted here.

### A4. The weather was compiled against the OpenGL sky — **fixed**

`mwworld/weather.hpp` includes `../mwrender/gl/skyutil.hpp`; `weather.cpp` includes
`../mwrender/gl/sky.hpp`. `RenderingManager` — the neutral, shared object — owns `SkyManager` and
`Water` out of `mwrender/gl/`, constructs both under either renderer, and `describeWorld()` reads
`mSky->getSkyColor()` and `mSky->isEnabled()` to fill a struct that is supposed to be
renderer-neutral.

`SkyManager` is two things wearing one name: a scene-graph dome (the rasterizer's) and the weather's
resolved colour state (everyone's). The dome is rebuilt every frame under the ray tracer for nothing
— the extractor explicitly masks it out — and the second half is what `WorldState` actually wants.

**Fixed.** `WeatherResult` and `MoonState` are plain data — strings, colours, floats, an enum —
authored by `MWWorld::WeatherManager` and read by the dome, and they lived in the dome's header. So
every file that wanted to describe the weather pulled in `osgParticle` shooters and state-set
updaters, and the world's weather system named a renderer to do it. They now live in
`mwrender/weatherresult.hpp`, which `gl/skyutil.hpp` includes.

The six `getSkyManager()->…` calls in `weather.cpp` became forwarders on `RenderingManager`
alongside the ones already there for the fog, the ambient and the sun — `setWeather`,
`setStormParticleDirection`, `setSunVisible`, `setGlareTimeOfDayFade`, `setMoonStates`. **Nothing in
`mwworld` names a renderer any more.**

What is *not* fixed is the other half: `RenderingManager` still constructs and drives `SkyManager`
and `Water` under the ray tracer, which masks both out of the mirror and draws neither. That is a
larger change — the world would have to stop owning them — and it is not an include problem.

### A5. Smaller

- `RtxRenderer`'s `installStatsOverlay`/`reportStats` overrides sit *inside* the `/*internal:*/`
  block, below `updateSubtree`. They are `Renderer` overrides; move them up with the rest.
- `Capabilities` is a struct with one `int`. The header explains why it shrank to one field; at one
  field it may as well be `virtual int getMaxTextureUnits() const`. Keep the struct only if a second
  field is imminent.
- `Stage`'s getters dereference `osg::ref_ptr`s that are null until `adopt()`/`setSceneRoot()`.
  `getSceneRoot()` in particular is null between construction and `Renderer::setSceneRoot`, and
  `LoadingScreen::open` reaches for it. Add `assert(mSceneRoot != nullptr)` to each getter — cold
  path, and it turns a segfault into a sentence.
- `apps/openmw/engine.cpp` now calls `mWorld->getRenderingManager()->renderFrame()` unguarded where
  the old code did `if (auto* rendering = ...)`. Safe today (the loop starts after `World::init`),
  but the guard was free.

---

## B. Symmetry between the two renderers

| `Renderer` method | `GlRenderer` | `RtxRenderer` | Symmetric? |
| --- | --- | --- | --- |
| `getCapabilities` | from `GL_MAX_TEXTURE_IMAGE_UNITS` | constant 32 | yes, documented |
| `attachWorld` | builds the shader chain over the world | records the resource system | yes |
| `getPostProcessor` | the chain | `nullptr` (base default) | yes, the one gate |
| `advance` / `eventTraversal` / `updateTraversal` | viewer's | hand-rolled equivalents | yes |
| `renderFrame` | describe + `renderingTraversals()` | mirror + trace + GUI + present | **no — see C1** |
| `createOffscreenView` | RTT camera in the graph | trace into a GUI slot | **partly — B1, B2** |
| `freezeFrame` | framebuffer copy | 1×1 black | no (logged in ISSUES.md) |
| `renderGui` | `renderingTraversals()` (world culled out by mask) | GUI over the last traced frame | yes in effect |
| `capture` | `ScreenshotManager` | readback + nearest resample | yes |
| `saveScreenshot` | async writer | logs a warning | no (logged in ISSUES.md) |
| `suspendDraw`/`resumeDraw` | viewer threading | no-ops | yes, documented |
| `getCompileOperation`/`setCompileOperation` | viewer's ICO | `nullptr` | yes, documented |
| `setVSync` | per-window | no-op | no, documented |
| `reloadChangedShaders` | `ShaderManager::update` | no-op | yes, documented |
| `installStatsOverlay`/`reportStats` | OSG profiler | no-ops | yes, documented |
| `createGuiPlatform` | OSG backend + `enableShaders` | Vulkan backend | yes |

### B1. `OffscreenView::getCopy()` — contract violated on the ray-tracing side

The interface says:

> That copy, **or null while the most recent `redraw()` has not reached it** … Null forever where
> nothing asked for one.

`GlOffscreenView::getCopy()` honours it (`!mDrawOnce->isDrawDone()` → null).
`TracedView::getCopy()` returns `mCopy` unconditionally — and `keepCopy()` allocates it black and
`memset`s it. So between `keepCopy()` and the first `redraw()` that actually traces, the caller gets
a valid, all-black image. See C3 for what that costs.

### B2. `setExtent` redraws on one side and not the other

`GlOffscreenView::setExtent` ends with `redraw()`. `TracedView::setExtent` only clamps and stores.
`InventoryPreview::setViewport` calls `setExtent` and nothing else, so a window resize repaints the
doll under the rasterizer and does not under the ray tracer.

Decide which is the contract and write it into `offscreenview.hpp` — I would make `setExtent` *not*
redraw (it is a description, not a command) and have `InventoryPreview::setViewport` call
`redraw()` itself.

### B3. `pick()` and the ray convention

Both are correct for their caller, but they agree by accident of two mistakes cancelling in the
documentation:

- `components/rtx/shaders/visibility.h:39` says a ray is `mForward + mRight * x + mUp * y`.
- `components/rtxvulkan/shaders/visibility.comp:1974` computes
  `mForward + mRight * uv.x - mUp * uv.y`, and the orthographic paragraph twenty lines further down
  in the same header says `- mUp * y` too.

`TracedView::pick` matches the shader. The perspective paragraph in `visibility.h` is simply wrong;
fix the prose, not the code.

---

## C. Bugs

### C1. **RTX skips the present on the frames it refuses to trace** — medium

`RtxRenderer::renderFrame` (`rtxrenderer.cpp:530` and `:576`):

```cpp
if (mScene.getPlacedCount() == 0)
    return;                      // ← before drawGui() and presentFrame()
...
if (!canLookAlong(forward))
{
    ...
    return;                      // ← same
}
```

`Engine::frame` calls `RenderingManager::renderFrame()` once per frame and nothing else presents.
`RtxRenderer::renderGui()` — the path that *does* present with no world — is only reached from the
loading screen, the video player and the modal message-box loop.

So on any frame the trace refuses, the swapchain is not presented and MyGUI's triangles are never
collected. `renderGui`'s own comment states the rule this violates: *"The surface has to be fed
either way or the compositor decides the window has stopped answering."* Both returns also skip
`mExtractor->advance()` and `mExtractor->retire()`, so the following frame's motion vectors are
measured against an epoch two frames old and nothing retires.

**Not the main menu, which I first said it was.** Measured with a counter on the untraced branch:
across four seconds of menu it never fired once, so `getPlacedCount()` is *not* zero there — the
scene root carries enough (the water plane among it) that the mirror always places something, and
the menu draws and responds. What is left is the `canLookAlong` return — a camera with no roll,
which the game hands over during a cutscene that looks straight down — and any frame that does
genuinely place nothing.

**Fix (landed):** neither condition is a reason not to present. The trace moved into a private
`traceWorld` returning whether it wrote anything, and the tail — `drawGui(); presentFrame();
advance(); retire();` — now runs on every path. `keep()` stays conditional, because
`OPENMW_RTX_SHOT`'s cap counts pictures rather than frames.

### C2. RTX ignores the field-of-view override — medium

`WorldState::mFieldOfView` is filled with `mFieldOfViewOverridden ? mFieldOfViewOverride :
mFieldOfView` and read by `PostProcessor::describe`. `RtxRenderer::renderFrame` instead passes
`Settings::camera().mFieldOfView` straight to `makeCameraAlong`, so scripted FOV changes, zoom and
anything else going through `World::setFieldOfViewOverride` do not reach the trace. (The convention
is fine — OSG's `perspective()` and `makeCameraAlong` both take a vertical FOV.)

One-line fix: use `world.mFieldOfView`. `WorldState::mProjectionMatrix` staying unread by the RT
path is correct and worth a sentence saying so.

### C3. The world map paints a black cell — medium

`LocalMap::draw` creates the view, calls `keepCopy()`, then `redraw()`. On the RTX path `redraw()`
for a world view returns early via `deferRedraw` when `!mOwner.hasScene()` — which is the case for
the first cell of a session, by design. `MapWindow::cellExplored` then calls `paintExplored()`,
which calls `GlobalMap::exploreCell(x, y, mLocalMapRender->getMapImage(x, y))`.

`exploreCell` returns `false` **only** when the tile is null. `TracedView::getCopy()` never returns
null after `keepCopy()` (B1), so it hands over the black `memset` image, `exploreCell` paints it,
returns `true`, and `std::erase_if` drops the cell from `mExploredPending`. The starting cell is
black on the world map for the rest of the session.

**Fix:** make `TracedView::getCopy()` return `nullptr` until the first `redraw()` has actually
written into `mCopy` — one `bool mCopyIsCurrent`.

### C4. The `isInterior` post-processing uniform changed meaning — medium

Before: `mPostProcessor->getStateUpdater()->setIsInterior(!enabled)` from
`RenderingManager::enableTerrain(bool enable, …)`, and terrain is enabled iff `cell.isExterior()`.

After: `PostProcessor::describe` does `setIsInterior(!world.mSkyVisible)` where
`mSkyVisible = mSky->isEnabled()`, and the sky is enabled iff
`mSky && (isCellExterior() || isCellQuasiExterior())`.

Two divergences: quasi-exterior interiors now report `isInterior == false` where they reported
`true`, and the `tsky` console command now flips the uniform. Every shipped post-processing shader
that branches on `omw.isInterior` is affected.

Fixing A4 — a neutral sky/weather state that carries "is this cell an interior" as its own fact
rather than as a side effect of whether a dome is drawn — resolves this properly. A stopgap is to
carry `mInterior` on `WorldState` alongside `mSkyVisible` and keep `enableTerrain` as its source.

### C5. Two-sidedness default inverted for NIF geometry — medium, **(verify with a shot)**

Deleted from `sceneextractor.cpp`:

```cpp
// OpenGL culls nothing unless told to, and `NifOsg` adds a `CullFace` only where the model's
// stencil property asked for it — so the absence of the attribute means two-sided, not
// one-sided. Getting this backwards lights every sheet in the game from one side only.
bool isTwoSided(const osg::StateSet& stateSet) { … return attribute == nullptr; }
```

`NifOsg` never sets an `osg::CullFace` *attribute* — it sets the `GL_CULL_FACE` *mode*. So the old
predicate returned `true` for every NIF surface: the mirror treated all of it as two-sided, which is
also what the rasterizer draws, because `GL_CULL_FACE` is off unless a `NiStencilProperty` turns it
on.

`Surface::Material::mTwoSided` defaults to **`false`** and is only set true by
`NiStencilProperty{DrawMode::Both}`, `BSLightingShaderProperty::doubleSided()`,
`BSEffectShaderProperty::doubleSided()` and a two-sided BGSM. `testnifloader.cpp:156` asserts this
is deliberate ("`DrawMode::Both` is the only thing in a NIF that means two-sided").

The result is that the ray tracer now back-face-culls geometry the rasterizer draws both ways. That
may well be the better answer, but it is a change in what the picture looks like that arrived inside
a refactor, and the comment that was deleted was warning about precisely it. Either flip the default
to `true` and let the properties turn it off (matching GL), or keep it and confirm with a
before/after `openmw-rtxtool shot` on a foliage-heavy view.

### C6. `SceneExtractor::readMaterial` reads the ancestor's blend and cull state

Also deleted:

> Separate from the nearest one outright because the two can differ: `NifOsg` puts a model's
> textures on the geometry, but a drawable can carry a state set of its own that only sets culling
> or blending. Taking the whole material from whichever state set happened to hold the textures
> would then hand it a parent's two-sidedness.

`findDescription` now takes alpha mode, alpha ref, two-sidedness *and* textures from the first
ancestor that has a `Surface::Material`. That is sound **only** while every described state set
carries a complete material — which `applyDrawableProperties` does guarantee today, because it is
the one place that calls `Surface::setMaterial`. It is an invariant nothing enforces.

Make it enforceable: `ExtractionStats::mUndescribedMaterials` already exists as the canary; assert
on it in the harness (`openmw-rtxtool scene --twice` on a few views) so a future state set built
elsewhere fails loudly rather than inheriting a parent's blend mode.

### C7. Smaller correctness

- **`extern/osg-ffmpeg-videoplayer/include/.../videostate.hpp:15-18`** declares `class Image;` twice
  in `namespace osg` — the second was `Texture2D` and the rename hit both lines.
- **`PostProcessor::setUnderwaterFlag`** has no callers left; `describe()` writes `mUnderwater`
  directly. Delete it.
- **`WindowManager::isPostProcessorHudVisible()`** lost `mPostProcessorHud &&`. Reached from
  `DateTimeManager::updateIsPaused` and `ActionManager`; both run after `initUI()` today, so it is
  latent rather than live — but it was a free guard.
- **`rtxrenderer.hpp:264`** — an orphaned doc comment (*"Whether anything has been traced yet, so
  `renderGui` knows there is something to show."*) followed by a blank line and then
  `std::size_t mFrame = 0;`. The member it described is gone.
- **`sceneextractor.hpp`** — `mUndescribedMaterials`' doc says such surfaces are *"recovered by
  reading OpenGL state back out"*. Nothing does that any more; they get a default material.
- **`myguirtx/texture.cpp:73`** — `mPixels.assign(w * h * elements + 0, 0)`, stray `+ 0`.
- **`myguirtx/rendermanager.cpp:174`** — `update()` keeps its timer in two function-local
  `static`s. Make them members.
- **`myguirtx/rendermanager.cpp:229`** — `"A GUI shader was asked for and this backend has one"`
  reads as the opposite of what it means (the backend has *no* shaders).
- **`Picture::set`** calls `createTexture(mName)` for an existing name without destroying first,
  relying on each backend keeping the address stable. Both do — GL by `insert_or_assign` into a
  by-value map, RTX by `destroy()`-in-place, and each documents it — but the requirement belongs on
  `GuiRenderManager` as a contract, not in two comments that happen to agree.

---

## D. Performance

### D1. The GUI's submits — **measured, and far smaller than I claimed** — low

| Site | When |
| --- | --- |
| `VulkanRenderer::drawGui` (`vulkanrenderer.cpp:442`) | every frame the GUI is up |
| `GuiTextures::write` (`guitextures.cpp:66`) | every `MyGUI::ITexture::unlock()` |
| `GuiTextures::add` (`guitextures.cpp:28`) | every texture created |
| `Image::read` via `GuiTextures::read` | every `keepCopy` redraw |

`submitAndWait` allocates a command buffer, submits, waits on a fence and frees it, and
`CommandPool`'s own doc says it is "Setup only. Recording a frame means reusing a buffer against a
fence … and nothing here is shaped for that." So the shape is wrong for the frame path. What it
costs is not.

Measured in the game, standing in an exterior with the HUD up:

| | in steady play | per call |
| --- | --- | --- |
| `GuiTextures::write` | **0 per frame** | 0.03–0.42 ms; 144 calls over an entire startup |
| `GuiTextures::add` | **0 per frame** | — |
| `VulkanRenderer::drawGui` — record, pass, submit, fence | every frame | **0.21–0.34 ms** |

Against a 6.2–7.0 ms trace and a 16.7 ms budget that is about 1.3% of the frame. **I called this
"the largest single performance item" from the shape of the code, and the measurement says it is
not.** The texture uploads are a load-and-event cost — a menu opening, a save thumbnail, a video —
and nothing writes a GUI texture during play at all.

The waits are also load-bearing: `GuiTextures::drop` says a slot can be given back because "every
submit here waits, and so does the one that drew with it". Removing them needs deferred-drop
machinery, which is real complexity bought with 0.2 ms, on a frame that is about to change shape
anyway. `CLAUDE.md` names this trade exactly: write the number down rather than act on it.

**Do it when something makes it matter** — a video path that has to hold frame rate, or the frame
being restructured for another reason. The shape to move to is unchanged: one staging ring, the
copies recorded at the head of the frame's own command buffer, `drawGui` recording into that buffer
instead of opening its own, and `drop` deferred by a frame.

### D2. Local-map segments are never released — **measured, smaller than this said** — low

`LocalMap::cleanupCameras()` is gone, and with it the only thing that took a map RTT back out of the
graph. Each `MapSegment` now owns a `GlOffscreenView` that stays a child of the scene root for as
long as the segment exists, and `mExteriorSegments` is only cleared when the player moves between
interior and exterior.

Before: the RTT node was removed the frame after it drew; only the colour texture survived.
After: colour **plus** the depth/stencil attachment (`GL_DEPTH24_STENCIL8`) and the FBO survive, per
visited exterior cell, for the session.

Measured, standing in an exterior with nine segments live: `local map resolution` is **256**, not the
1024 I assumed, so a segment is 256 KiB of colour and 256 KiB of depth — **4.5 MiB for the nine**.
Hundreds of cells over a session is tens of megabytes, not hundreds.

And the delta is smaller again than that: the colour texture was *already* kept for the life of the
segment before this branch (`segment.mMapTexture`), on both paths. What the branch added is the
depth attachment and the FBO, so ~256 KiB a cell, on the renderer this fork is not aiming at.

D3's fix takes a further 256 KiB a cell off both paths, because eight of every nine segments no
longer allocate the host-side copy either.

**Worth doing when the rasterizer's memory matters**, and the shape is unchanged: either drop the
depth attachment after the draw — an `OffscreenView::release()` that keeps the colour texture — or
restore the old lifecycle now that the texture is a `MyGUI::ITexture` that outlives the node.

### D3. Every exterior map tile was read back to main memory — **fixed**

`LocalMap::draw` calls `segment.mView->keepCopy()` on every non-interior segment. On the GL path
that attaches an `osg::Image` to the camera's colour buffer, so OSG reads the whole tile back in the
draw traversal — a synchronous `glReadPixels` of 1024²×4 on the frame a cell arrives, which is
already the busiest frame there is. On the RTX path it is `readGuiTexture`, which is a
device-to-host copy behind a fence.

Measured: the read back costs **1.2 ms a tile**, against **0.23 ms** to trace the tile itself — it is
almost entirely submit-and-fence latency, not the 256 KiB of pixels.

And almost none of it was wanted. `WindowManager::changeCell` calls `cellExplored` for the cell the
player walked into and no other, so of the nine tiles an arrival draws, **one** is ever painted into
the world map. The other eight were read off the device and never looked at: ~9.6 ms of fence
latency on the frame a cell arrives, which is the frame with the least room for it.

**Fixed** by asking for the copy where it is wanted. `LocalMap::draw` no longer calls `keepCopy`;
`LocalMap::getMapImage` starts one on the first ask — which is why it is no longer `const` — and
`GlobalMap::exploreCell` already treats a null tile as "ask again later".

The retry then had to be driven from somewhere that always runs: `MapWindow::onFrame` only runs
while the map's mode is up or it is pinned, and cells are walked into with the map closed, so
draining there left the world map unpainted. It now drains from `WindowManager::updateMap`, which
runs every frame the game does. Verified by checksumming the tile that reaches `exploreCell`:
identical to the pre-change tile to three parts in a million, and now the same on every run where
it used to race.

### D4. `GlobalMap::exploreCell` is a frame spike — medium (already half-noted)

Per explored cell, on the main thread, inside the frame:

- a box filter over the whole tile — `tileWidth × tileHeight` texel reads (1 M at 1024²), four
  channel accumulations each;
- a `memcmp` of the cell's footprint;
- `upload(*mOverlayTexture, *mOverlayImage)` — **the entire overlay**, `mWidth × mHeight × 4`, back
  to the device, because MyGUI's `lock` has no sub-rectangle.

`.notes/rtx/todo.txt` says `map overlay cpu render -> gpu`. That note belongs in
`.notes/ISSUES.md` (see E5). Independently of moving it back to the GPU, the downscale is
`mWorkQueue`-shaped work — `CreateMapWorkItem` is right there — and the upload wants a MyGUI-side
sub-rectangle or a second, cell-sized texture composited by the widget.

### D5. `Surface::getMaterial` builds a `std::string` per lookup — **measured free, and one character from not being** — low

```cpp
const osg::Object* found = container->getUserObject(std::string(sUserObjectName));
```

`SceneExtractor::findDescription` calls this once per state set in the shading chain, per drawable,
per frame — tens of thousands of times on an exterior.

Measured: the whole extraction walk is **2.0 ms a frame**, and scanning the container by hand instead
of building the name leaves it at 2.0 ms. Thirteen 300-frame averages either side — 1.98 ms before,
2.04 ms after. It is free, because `"SurfaceMaterial"` is fifteen characters and libstdc++ holds
fifteen inside the string.

**Changed anyway, and the reason is the fifteen.** That is exactly the limit — checked, not assumed —
so the name is one character away from putting an allocation on the frame path tens of thousands of
times a frame, with nothing anywhere to say it had started. The scan is the loop `getUserObject`
would have run regardless, over a container that almost always holds one object, and renaming a
string cannot push it over that edge.

**The 2.0 ms is the finding here.** The walk is the largest CPU item on the frame — a third of what
the trace costs — and nothing in this review had measured it. Whatever is in it, it is not this.

### D6. Video playback on the rasterizer — **measured: microseconds, and the fix I proposed was a race** — low

`VideoWidget` used to hold an `osg::Texture2D` that `VideoState` fed with `setImage` and a
`PixelBufferObject`. It now goes through `Picture::set` → `MyGUIPlatform::OSGTexture::lock` →
`unlock`, and per `.notes/ISSUES.md` that path allocates an `osg::Image` on every `lock` and a fresh
`osg::Texture2D` on every `unlock`. So a playing video now costs, per frame: one image allocation,
one full-frame `memcpy`, one texture-object allocation, and a full re-upload of a texture the driver
has never seen before (no PBO, no reuse).

Measured on the rasterizer, timing only the allocations rather than the caller's fill:

| | cost |
| --- | --- |
| `lock`'s `new osg::Image` | **2–15 µs** |
| `unlock`'s `new osg::Texture2D` | **0.5–3 µs** |
| calls in a whole session | **11** — three 8×8 solids, six font atlases, two global-map textures |

So the shape is as described and the cost is not. Even a video at thirty frames a second would spend
0.3 ms a second on the two allocations.

**The video case itself could not be exercised here**: `Movies_Morrowind_Logo` is absent from this
install's fallback list, so `Engine::go` never calls `playVideo` and no video ran in any
configuration reachable without clicking. The per-call number above is what bounds it.

**And the fix this review proposed would have been a data race.** `unlock` makes a new texture
because of the line directly above it — *"mTexture might be in use by the draw thread, so create a
new texture instead and use that"* — and the image it hands over goes with it. Reusing either while
the draw thread is a frame behind is writing what is being read. The correct shape is
double-buffering, two images and two textures alternated, which is what
`MyGUIPlatform::Drawable` already does with its vertex arrays for exactly this reason. That is real
machinery for ten microseconds, so it waits for something that makes it matter — a cutscene that
has to hold frame rate.

### D7. `MyGUIRtx::Texture::loadFromFile` decodes pixel-by-pixel — low

```cpp
for (int y …) for (int x …) { const osg::Vec4f colour = image->getColor(x, y); … }
```

`Picture::set` already has the fast path — `directFormat()` plus one `memcpy` for contiguous
`GL_UNSIGNED_BYTE` images. Every GUI skin, every font page and every icon on the RTX path goes
through the slow one. Share the converter (E4).

### D8. Noted and fine

- `describeWorld()` returns `WorldState` by value: ~200 bytes, all trivially copyable, no heap.
- `RtxRenderer::mDeferred`/`mDrawing`, `MapWindow::mExploredPending`: linear scans over
  single-digit vectors.
- `MyGUIRtx::RenderManager::mVertices`/`mBatches` and `VulkanRenderer::mGuiDraws` are persistent
  scratch, cleared and refilled. Correct.
- `RtxRenderer::mPixels`, `TracedView::mPixels`, `GlobalMap::mCellScratch`: same. Correct.

---

## E. Simplify, consolidate, encapsulate

### E1. Two `createWindow`s that are 70 % the same

`GlRenderer::createWindow` and `RtxRenderer::createWindow` share the window-mode → position mapping,
the three `SDL_SetHint` calls, the border flag and the whole of `setWindowIcon` (which is duplicated
verbatim, ~30 lines each). What genuinely differs is the GL attribute block and the antialiasing
retry loop.

Hoist a `MWRender::makeWindow(Uint32 extraFlags)` and a `MWRender::setWindowIcon(SDL_Window*, const
std::filesystem::path&)` into `mwrender/windowsetup.{hpp,cpp}`. Neither is a renderer decision.

### E2. `components/myguiplatform/` is now three things

It holds the neutral pieces (`Platform`, `GuiRenderManager`, `Picture`, `AdditiveLayer`,
`DataManager`, the log listener) *and* the OpenGL backend (`RenderManager`, `OSGTexture`,
`Drawable`), while the Vulkan backend lives in `components/myguirtx/`. Both `guirendermanager.hpp`
and `picture.hpp` open with a paragraph apologising for where they are.

Two files that explain their own location are the signal. `components/mygui/{core,osg,rtx}` — or at
minimum moving the neutral four into a `components/myguicore/` — makes the dependency direction
visible in the tree instead of in prose.

### E3. `Surface::Material` authoring has no single entry point

Three writers (`nifloader`, `shadervisitor`, `terrain/material`) each hand-roll "build a value, call
`setMaterial`", and the invariant C6 depends on — *every described state set carries a complete
material* — is stated in a comment in a fourth file. A tiny `Surface::MaterialBuilder`, or just a
`Surface::setMaterial` overload that debug-asserts a diffuse role is filled, makes it checkable.

Separately: `Material::setTexture(role, const osg::Texture*)` silently takes `getImage(0)`. That is
the right behaviour and a surprising signature; the header explains it, the call sites do not.

### E4. One `osg::Image` → RGBA converter, not two

`MyGUIPlatform::Picture::set` (fast path + `getColor` fallback) and
`MyGUIRtx::Texture::loadFromFile` (`getColor` only) do the same job. Expose `Picture`'s
`directFormat`/widen logic as a free function in the neutral half and have both use it. Fixes D7.

### E5. Housekeeping

- **`.notes/rtx/todo.txt`** is a one-line file duplicating the issue log. Fold `map overlay cpu
  render -> gpu` into `.notes/ISSUES.md` and delete it.
- **`CLAUDE.md` "Architecture, in one screen" is stale in two places** that this branch made false:

  > It reaches the screen through **GL/Vulkan interop**, not a Vulkan window, so the GUI, the
  > inventory doll, the local map and video playback keep working.

  and

  > `renderingTraversals()` — and **that last call is the only thing the RT renderer displaces**.

  `components/rtxgl/` is deleted, the RT path owns an SDL Vulkan surface, and what it displaces is
  the whole of `renderingTraversals`. The `.notes/ISSUES.md` entries this branch added are good;
  this section needs the same pass.
- **`components/myguirtx/rendermanager.cpp:227`** exceeds 120 columns under the pinned
  clang-format 14 (the `registerShader` signature). The general 14-vs-22 disagreement is already in
  `.notes/ISSUES.md`; this one file is unambiguous and worth fixing on its own.

---

## Plan

Ordered so that each step is independently landable and each one's verification is cheap.

**Steps 1, 2 and 3 are done.** Three things not in this review were found while running it and are
done too:

- **The DLSS crash that blocked running any of it.** `VulkanRenderer::describeDevice` built a second
  `Dlss` to ask whether Ray Reconstruction was available; NGX keeps one runtime per process and its
  shutdown is unconditional, so that throwaway ended the live one the moment it left scope. `Dlss`
  now has no public constructor and `Dlss::open` hands out shares of the one runtime.
- **The inventory doll tearing apart on a change of clothes.** `SceneExtractor::mMeshes` is keyed on
  an `osg` address; `updateParts` frees body parts and the allocator hands the addresses straight
  back, so a walk found the retired part's entry under the new part's address and wrote the new
  vertices into the old slot — a run inside one shared buffer, so it overran into the meshes after
  it. A view's rebuild now starts from empty tables, and `resolveMesh` refuses to write a pose into
  a slot whose vertex count differs.
- **Every NPC frozen in the pose it had three frames after loading.** The game marks every actor but
  the player `Skeleton::SemiActive`, which skips the update traversal — and so stops moving bones —
  once nothing has reached the skeleton for three traversals. Under a rasterizer its cull keeps
  saying so; the mirror never walked a skeleton with a cull visitor. `Skeleton::markReached` is now
  the one way to answer that, and the mirror is a caller. Measured 19 of 210 deforming meshes moving
  before, 192 of 210 after.

### 1. Present on every path — `RtxRenderer::renderFrame` *(C1)* — **done**

Restructure so the tail always runs:

```cpp
void RtxRenderer::renderFrame(const SceneFrame& frame)
{
    ...mirror, upload...
    if (mScene.getPlacedCount() != 0 && canLookAlong(forward))
    {
        drawDeferredViews();
        ...trace...
    }
    drawGui();
    if (!mRenderer->presentFrame())
        fitToWindow();
    mExtractor->advance();
    mExtractor->retire();
    keep();
}
```

**Verify:** `openmw` on the RTX path, no `--skip-menu` — the menu must animate and respond.
Then `openmw-rtxtool shot --view=balmora` to confirm the traced path is unchanged.

### 2. Three one-line correctness fixes *(C2, C3, C7)* — **done**

- `RtxRenderer::renderFrame`: `Settings::camera().mFieldOfView` → `world.mFieldOfView`.
- `TracedView`: a `bool mCopyIsCurrent`, set in `redraw()` after the read, checked in `getCopy()`.
- `videostate.hpp`: delete the duplicate `class Image;`.
- Delete `PostProcessor::setUnderwaterFlag`; restore the `mPostProcessorHud` null guard; fix the
  orphaned comment in `rtxrenderer.hpp`, the stale `mUndescribedMaterials` prose, the
  `visibility.h` ray formula, the `+ 0` in `texture.cpp`, and the `registerShader` line width.

**Verify:** `./build/components-tests --gtest_filter='*Surface*:*NifOsg*:*Gui*'`, then a shot.

### 3. Decide C4 and C5, then land them together — **done**

Neither needed the shots, for opposite reasons.

**C5 was not a picture change at all.** `Rtx::Material::mTwoSided` is written by the extractor and
read by nothing: `SceneBuffers::toGpu` does not carry it, and the top level disables culling on every
instance anyway. So there was nothing to compare — only a field to make truthful before something
starts reading it. `Surface::Material::mTwoSided` now defaults to **true**, which is what the content
means: OpenGL culls nothing until told to, and the only NIF record that tells it to is a
`NiStencilProperty` with a draw mode other than `Both`. A shader property's "double sided" flag and a
material file's can turn culling off and never on, so their three `= true` assignments were saying
what the default already said and are gone. `testnifloader.cpp` gained the case that makes the old
assertion mean something — a stencil property drawing one face, asserted single-sided — because with
the default true, asserting `Both` gives two-sided proves nothing on its own.

**C4 was a real regression and is fixed by carrying the fact.** `WorldState` gains `mInterior`, read
straight off `MWBase::World::isCellExterior()` in `describeWorld`, and `PostProcessor::describe` uses
it instead of `!mSkyVisible`. The two questions are now both present and documented as different: a
quasi-exterior is an interior cell that draws a sky, so `mInterior` and `mSkyVisible` disagree there
and each has a caller wanting its own answer. `tsky` no longer moves the `isInterior` uniform.

`PostProcessor::setExteriorFlag` still comes from `mwworld/scene.cpp` reaching into the shader chain
directly. That is the A3 leak and belongs to step 9, not here.

### 4. The GUI submit storm *(D1)* — **done: measured, and not worth doing yet**

0.21–0.34 ms a frame for `drawGui`, zero GUI texture writes per frame in play — 1.3% of the budget,
against machinery whose waits `GuiTextures::drop` relies on for correctness. Numbers and the shape
to move to when it does matter are in D1. The original plan, for when that day comes:

1. Give `GuiTextures` a staging ring and a `flush(VkCommandBuffer)`; `write` records into it instead
   of submitting.
2. Have `VulkanRenderer::drawGui` record into the frame's command buffer rather than its own.
3. Defer `GuiTextures::drop` by one frame now that a slot can be in flight.

**Verify:** `openmw-rtxtool view --frames=600` under the validation layers, then `OPENMW_RTX_BENCH`
with the HUD up, before and after — and write the number down.

### 5. The local map's lifecycle *(D2, D3)* — **done: D3 fixed, D2 measured and deferred**

D3 was real and is fixed — 1.2 ms a tile, eight tiles in nine wasted, ~9.6 ms off a cell arrival.
D2 turned out to be 256 KiB a cell on the rasterizer, not the megabytes I estimated from a map
resolution I had guessed at. Numbers in D2 and D3 above. The original plan:

1. `keepCopy()` only where the global map will actually ask — move it from `LocalMap::draw` to the
   point a cell is explored.
2. Give `OffscreenView` a way to say "I am done with this for now" so the GL implementation can drop
   its depth attachment and detach the RTT node, and the segment keeps only its `MyGUI::ITexture`.

**Verify:** walk five exterior cells and watch process RSS / VRAM; confirm the map still fills in.

### 6. `Surface::getMaterial`'s key *(D5)* — **done: measured free, changed for the boundary**

1.98 ms before, 2.04 ms after, thirteen samples each side — no difference. Kept because
`"SurfaceMaterial"` is fifteen characters against libstdc++'s fifteen-character limit, so the
allocation is one rename away. Details in D5.

The measurement's real result is that **the extraction walk costs 2.0 ms a frame** — the largest CPU
item on the frame path, and unaccounted for. That deserves a look of its own before any more of
these.

### 7. `OSGTexture::lock`/`unlock` reuse *(D6, and half of D1)* — **done: measured, and the plan was wrong**

Two allocations totalling under 20 µs, eleven times in a session; the video path is not reachable on
this install. And "reuse when the size and format are unchanged" — what this step said to do — would
have raced the draw thread, which is the reason the allocation is there. Details and the correct
shape (double-buffering, when something makes it matter) are in D6.

### 8. The remaining `#ifdef` *(A2)* — **done**

`Features::hasRayTracing()` in `components/features/features.cpp` is the one place the build option
becomes a value, and `components` now carries the definition so both binaries can ask. The settings
page and the launcher call it, and the launcher's own `OPENMW_RTX` compile definition is gone.
`components/features/` rather than `components/version/`, because a version is what release this
is and this is what was compiled into it — and not `components/build/`, which `.gitignore`'s
unanchored `build*/` swallows at any depth.

Not `MWRender::rendererAvailable` as sketched here: the launcher does not link `openmw-lib`, so the
answer had to live somewhere both binaries already link.

**The floor is two spellings, not one, and the second cannot be helped.**
`MWRender::createRenderer` has to name a type that does not exist in a build without the option, so
its `#ifdef` guards an include and a construction — one decision, one file. Every other use of the
macro in C++ is gone: three sites became one question asked in two places.

### 9. `PostProcessor` and the sky *(A3, A4)* — **done**

A4 fixed outright: `mwworld` names no renderer at all now. A3 narrowed from nine files to five, and
the five are the post-processing feature's own UI and script bindings rather than the world. Details
in A3 and A4 above; the interface extraction is recorded there as deliberately not done and why.

The original plan:

The two structural items, and the two largest. Do them in this order — the sky split is what makes
`describeWorld()` honest, and `PostProcessor`'s move is mechanical once nothing but the renderer
constructs it.

1. Extract the weather's colour and interior/exterior state out of `SkyManager` into a neutral type;
   `mwworld/weather.{hpp,cpp}` stops including `mwrender/gl/`. Fixes C4 properly.
2. Move `PostProcessor`'s declaration to `mwrender/postprocessor.hpp` (option A3(a)), leaving the
   implementation in `gl/`. Nine files change one include line each.

### 10. Consolidation, once the above is quiet *(E1–E5)*

`makeWindow`/`setWindowIcon`; the one image converter; the `components/mygui*` layout; `todo.txt`
into `ISSUES.md`; the `CLAUDE.md` architecture paragraph.
