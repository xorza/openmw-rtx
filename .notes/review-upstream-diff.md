# Review of the fork's diff against upstream

> **When you address an item, delete it.** Do not mark it done. The file lists open findings only.

Scope: every production file the fork adds or changes against upstream's merge base `46bd4599`, about 150k lines in 891 files. Tests and the APIs they use are out of scope. Seven reviewers read one area each. Every high finding, and the present-target finding, was then checked against the code a second time.

Severity: **high** is a wrong value, a crash, undefined behaviour, or a check that passes while broken. **medium** is an ownership, design or duplication problem with a real cost. **low** is a simplification, dead code, a name, or a comment that breaks the guide.

## 1. An index derived from something that moves, where a stable key was needed

- [ ] **low** — `components/rtx/scene/scenedesc.cpp:274`–`:277`, `components/rtx/scene/lightgrid.cpp:92` — `orderLights` sorts by position, but `rebuild` compares each lamp by its rank. A carried torch changes rank whenever its x passes another lamp's, and every lamp between the two ranks then compares against its neighbour's old entry, so the grid is filled again. The saving claimed at `lightgrid.cpp:84` fails for the lamp most likely to move. Fix: compare by a key that stays the same from frame to frame.

## 2. One fact with two sources

- [ ] **medium** — `files/settings-default.cfg:1287`, `apps/launcher/ui/graphicspage.ui:224` (and its seven `files/lang/launcher_*.ts` copies), `cmake/ForkOptions.cmake:3`, `components/rtx/build.cmake:6`, `:9` — These say the renderer needs an NVIDIA GPU. `README.md:52`, `docs/source/reference/modding/settings/rtx.rst:9` and `AGENTS.md` also name AMD RDNA 2 and later. The two texts players read (the settings file and the launcher tooltip) turn AMD owners away. Fix: state the requirement once, as the rst does, and make the other places match. The CMake error only needs to say that macOS has no Vulkan ray tracing.

## 3. The rasterizer's picture or cost changed outside the three allowed places

- [ ] **low** — `components/sdlutil/sdlinputwrapper.cpp:161`, `:241` — The `SDL_CONTROLLERDEVICEREMAPPED` case and the `std::dec` reset fix upstream's logging, which the ray tracer does not need. Fix: send both upstream, and drop them here once upstream has them.
- [ ] **low** — `apps/openmw/mwworld/weather.hpp:350`, `:382` — `mTimeSettings` and `mResult` are reference members bound to parts of the object's own `mSky`, so the implicit copy constructor would bind a copy to the source's sky. Nothing copies a `WeatherManager` today. Fix: delete the copy constructor, or replace the aliases with accessors.

## 4. Frames drawn outside the engine's main loop are not treated as frames

- [ ] **low** — `apps/openmw/mwgui/windowmanagerimp.cpp:781`, `:2108` — Both nested loops hold a hidden window the same way: sleep 5 ms, then `mRenderer.advance(mRenderer.getFrameStamp().getSimulationTime())`. The seam owns the drawn case (`renderGuiFrame`). Fix: a seam member for a GUI frame that is not drawn, called from both loops.
- [ ] **low** — `apps/openmw/mwrender/rtx/rtxrenderer.cpp:730` — When `!drawsWorld()`, `renderFrame` returns before `traceWorld`, the only caller of `drawViews`. `tws` hides only `sToggleWorldMask`, so the rasterizer keeps redrawing the doll and the map tiles, and the ray tracer freezes them. Fix: draw the deferred subject views on frames where the world is hidden, or document the difference on `toggleRenderMode`.

## 7. The water column asks a narrower sun question than the surfaces in it

- [ ] **high** — `components/rtxvulkan/shaders/lib/underwater.glsl:273` against `:115`–`:128` — The shaft march tests the sun only from the water surface up (`skyVisible(vec3(met, frame.mWaterLevel), ...)`). `skyPassageThrough` states that an occluder between an underwater point and the surface needs its own ray, and traces it for every submerged surface. So a sunken hull shadows the bed, and the water in front of it stays lit by a full shaft. Fix: call `skyPassageThrough(frame.mSun, at, draw)` at each step. It costs one short ray per step, `WATER_SHAFT_STEPS` of them, only where a shaft shows, so measure it.

## 11. Untrusted values that reach arithmetic, loops or files without a check

- [ ] **low** — `docs/source/reference/modding/settings/rtx.rst:27` — `distant land cells` is documented as "≥ 0", and the sanitizer clamps it to `[0, 10]`. Fix: document `0 to 10`.

## 12. One number spelled several ways

- [ ] **low** — `components/rtxvulkan/display/displaychain.cpp:200`–`:203` against `:198`–`:217` (`toneFor`) — The lines take the traced extent from the channels' size and the grid from the target's size, while the tone pass takes both from the sampled camera and `mExtent`, and says why the channels' size is the wrong source. They agree today only because `renderFrame` asserts it. Fix: read them as `toneFor` does.
- [ ] **low** — `apps/openmw/mwrender/rtx/worldmirror.hpp:91`, `apps/openmw/mwrender/rtx/skyreader.hpp:85`, `apps/openmw/mwrender/renderingmanager.cpp:944`, `apps/openmw/mwrender/framedescriber.cpp:132` — Arguments repeat what the callee can read: `mirror` takes `frame.mWhen.getFrameNumber()` beside `frame`, `SkyReader::read` takes five pieces of one `SceneFrame`, and the projection goes from the describer to `RenderingManager` and back. Fix: take `const SceneFrame&`, and let `FrameDescriber::describe` fill `mProjectionMatrix` itself.

## 13. Work done per frame, per pixel or per arrival that could be done once

- [ ] **low** — `components/rtx/preprocess/texture/finesttexels.cpp:33`, `components/rtx/image/alphaimage.cpp:217`, `components/rtx/preprocess/contentpreprocessor.cpp:20`–`:27` — `SolidReach` keys its input by hashing every byte of the finest level, while the pass stops at the first solid block, and the cache the key is for holds nothing. Fix: skip the key while `ContentCache` holds nothing, and turn it on with the store.
- [ ] **low** — `components/rtx/image/alphaimage.cpp:195`–`:205` — `describeFinest` widens or gathers every level into the scratch, then keeps level 0 only. Fix: describe level 0 alone.
- [ ] **low** — `components/rtxvulkan/scene/skintables.cpp:23`, `components/rtxvulkan/scene/sceneacceleration.cpp:47`–`:49`, `components/rtxvulkan/scene/scenebuffers.cpp:131`–`:133`, `components/rtx/scene/scenetextures.cpp:29`–`:30` — "Every index below N" is built four ways, and `SceneBuffers` allocates a fresh vector on every build. Fix: one helper and one held list.
- [ ] **low** — `apps/openmw/mwrender/rtx/tracedterrain.hpp:57`, `:87` — The comment says an arriving cell "allocates nothing after the first few", but `mCells` is a `std::map`, so every `loadCell` allocates a node. Fix: a flat vector searched by `(x, y)`, beside `mSpare`.
- [ ] **low** — `components/rtx/scene/placementtable.hpp:169`–`:171` — Duplicates in `mMoved` are justified as "a memcpy of a hundred bytes", but each entry costs `recordOf`, a 4×4 inverse and a row in both device tables. Fix: dedupe with a `SlotSet`, or state the real cost.
- [ ] **low** — `tools/omw/main.py:69`–`:70`, `tools/omw/gate.py:32`–`:33`, `tools/omw/testing.py:18`–`:21` — `test_targets()` configures, and the `build()` after it configures again. Fix: let `build` take its targets from the configure it already made.

## 15. Code in the folder or file of another responsibility

- [ ] **low** — `components/rtxvulkan/scene/spritebin.hpp:55`, `components/rtxvulkan/scene/spritepasses.hpp` — `SpriteBin` calls itself "the trace's and not the placement's", and `TraceChain` owns the bins and the passes. Fix: move both to `trace/`, and keep `SpriteSource` in `scene/`.
- [ ] **low** — `components/rtx/scene/debuglines.hpp` — `DebugLines` is a per-frame option no scene holds, and `renderer.hpp:21` includes it without use. Fix: move it to `frame/` and drop the include.
- [ ] **low** — `components/rtx/renderer/frameimage.hpp:16`–`:70` — The channel identity every denoise pass needs shares a header with the conversion to `osg::Image`. Fix: a header of its own for the channel list.
- [ ] **low** — `components/rtx/scene/tangent.hpp`, `components/rtx/scene/tangent.cpp` — A one-line wrapper with one caller (`meshtable.cpp:150`). Fix: fold it into `meshtable.cpp`.
- [ ] **low** — `components/rtx/frame/frameextents.hpp:11`, `components/rtx/renderer/renderer.hpp:180`, `:43`–`:58` — Core headers cite `VulkanRenderer::readChannel` and `SceneAcceleration::sRebuildEvery`, and `ValidationLevel` documents how the Vulkan validation layers behave. Fix: state the rule in the core, and move the Vulkan facts to the backend.
- [ ] **low** — `apps/openmw/mwrender/rtx/rtxrun.hpp:23`, `:26`, `:68` — Only `apps/rtxtool` reads `sStepRate`, `sStepSeconds` and `RunSetup::getWorldStep()`. Fix: move them to the harness.
- [ ] **low** — `components/rtx/environment/frameworld.hpp:44` — `mirrorPrecipitation` is a `SceneExtractor` walk that only `WorldMirror` calls. Fix: move it to `mirror/` or into `WorldMirror::mirror`.
- [ ] **low** — `apps/openmw/mwrender/sceneframe.hpp:171` — `sunDiscOf` is declared in the data header and defined in `framedescriber.cpp:29`. Fix: define it in a `sceneframe.cpp`, or declare it in `framedescriber.hpp`.
- [ ] **low** — `apps/openmw/mwrender/rtx/rtxrenderer.cpp:348`, `apps/openmw/mwrender/glrenderer.hpp:220`–`:222` — `RtxRenderer` asks the world for the root it made in `createSceneRoot`, while `GlRenderer` keeps its own copy until `detachWorld`, though its comment says "until `attachWorld`". Fix: hand the root to `GlWorld` at attach, and let `RtxRenderer` keep what it made.
- [ ] **low** — `apps/openmw/mwbase/windowmanager.hpp:388`, `apps/openmw/mwgui/windowmanagerimp.cpp:1441`, `apps/openmw/mwrender/renderingmanager.cpp:195` — `WindowManager::setCullMask` is a one-line forwarder to `Renderer::setViewMask`, and its only caller already holds the renderer. Fix: call `mRenderer.setViewMask(mask)` there, and remove the interface method.
- [ ] **low** — `apps/rtxtool/instruments/drivercache.cpp:11` — The driver cache, `framehashes` and `stopwriter` include the scene digest header for `spellHash` alone. Fix: move `Digest` and `spellHash` into a header of their own.

## 16. Bookkeeping repeated per table or per verb

- [ ] **low** — `components/rtx/scene/texturetable.hpp:105`–`:115`, `components/rtx/scene/deformertable.hpp:140`, `:170`–`:175` — `MeshTable` and `MaterialTable` inherit their readers from `HeldRows`, whose comment rejects per-table forwarders, and `TextureTable` and `DeformerTable` still write their own under other names (`isFree`, `getDeformers`). Fix: derive both from `HeldRows`.
- [ ] **low** — `components/rtx/scene/scenedesc.cpp:316`–`:325`, `components/rtx/scene/placementtable.cpp:193`–`:199` — A placement's world box is computed twice, the same way. Fix: one function on the placement row.
- [ ] **low** — `components/rtx/preprocess/texture/solidreach.hpp:18`–`:31`, `components/rtx/preprocess/texture/texelmean.hpp:19`–`:31` — The two texture passes are the same shell with another `run`, and both `run`s ignore their `Input` and read what `digest` left. Fix: one template over a function, or pass the described `TextureData` to `run`.
- [ ] **low** — `apps/rtxtool/main.cpp:683`, `:816`, `:844`, `:903`, `:1090`, `apps/rtxtool/options.cpp:420` — Each verb spells its own default `--out`, and the help line lists all five again. Fix: one helper that defaults to `verbName(command.mVerb)`.
- [ ] **low** — `apps/rtxtool/main.cpp:375`, `:534`, `:567`, `:577`, `:600` — `views.cfg` and `benches.cfg` paths are spelled at each use, and the two unknown-name errors differ. Fix: one function per path, and one refusal.
- [ ] **low** — `apps/openmw/mwgui/settingswindow.cpp:299`, `:347`, `apps/openmw/mwgui/settingswindow.hpp:71` — The constructor fetches `RayTracingDistantLandSlider` twice, and six of the seven new `mRayTracing*` members are used only in the constructor. Fix: six locals, one fetch.
- [ ] **low** — `tools/omw/game.py:42`–`:43`, `tools/omw/presets.py:29`–`:30` — `setup` sets `BUILD_MWINIIMPORTER=ON` in the debug cache and leaves the stamp, which then claims the directory is what the preset digest gives. Fix: build the importer through a preset or a target, or clear the stamp.

## 17. Dead code and members nothing reads

- [ ] **low** — `components/rtx/preprocess/meantexels.hpp:36`–`:38`, `:46`–`:47`, `components/rtx/preprocess/meantexels.cpp:13`–`:17`, `components/rtx/mirror/materialresolver.cpp:349`–`:367` — The unnamed-image path cannot be reached: `takeTexture` refuses an image with no file name, so `diffuseMeanOf` returns before its unnamed branch. Fix: delete `MeanTexels::mUnnamed`, its contract and the branch, and assert a named image in `MeanTexels::of`.
- [ ] **low** — `components/rtx/mirror/materialresolver.hpp:165`–`:167`, `:223`–`:224` — `HeldTexture` and `Animated` inherit `Known` only for `mReach`, and their comments say `mIndex` names nothing. Fix: a `Reach mReach` member, as `KnownMesh` has.
- [ ] **low** — `components/rtx/mirror/mirroridentity.hpp:107`–`:115` — `Kept::stamp(Value&)` is public with no caller but `stamp(Entry)`. Fix: fold it in, or make it private.
- [ ] **low** — `apps/openmw/mwrender/sky.hpp:55`, `:59`, `:72`, `:84`, `:90`, `:140` — `getMasserPhase`, `getSecundaPhase`, `isEnabled`, `getBaseWindSpeed` and `getSkyColor` have no callers after the split, and `mBaseWindSpeed` is read only by its dead getter. Fix: remove them.
- [ ] **low** — `apps/openmw/mwrender/ground.hpp:69` — Nothing reads the bool `Ground::blacklistReference` returns. Fix: return `void`.
- [ ] **low** — `apps/openmw/mwrender/rtx/rtxsettings.hpp:27` — `derive` ignores `RtxSettingValues::mGroundcover`, which exists so the constructor can read the registry a second time (`rtxrenderer.cpp:225`), also in a harness run. Fix: read the setting at the warning, or carry it in `RunSetup`, and drop the field.
- [ ] **low** — `apps/openmw/mwrender/rtx/rtxrenderer.cpp:618` — `setUpscale` is a private one-line assignment with one caller. Fix: inline it.
- [ ] **low** — `apps/openmw/mwlua/postprocessingbindings.cpp:143` — After `if (!self.mShader) return;`, the next line tests `self.mShader` again. Fix: drop the repeated test.
- [ ] **low** — `apps/rtxtool/model/benchrecord.cpp:329`–`:331`, `apps/rtxtool/model/runrecord.cpp:44` — `describeTotal`'s `stopped` is always false. Fix: drop the parameter and its branch.
- [ ] **low** — `apps/rtxtool/stopwriter.cpp:749`–`:754`, `apps/rtxtool/model/benchrun.cpp:49`–`:52` — `CameraStands` answers yes for a stop with no eye, but `canAsk` never asks it of one. Fix: remove the branch, or assert the eye.
- [ ] **low** — `apps/rtxtool/.gitattributes:1` — It names `rtx.cmd`, which does not exist, and the root `.gitattributes` already covers every `*.cmd`. Fix: delete the file.
- [ ] **low** — `components/rtx/scene/meshtable.cpp:194`, `components/rtx/scene/materialtable.cpp:105` — Both guard against releasing an empty run, which `RunAllocator::release` already ignores. Fix: drop the guards.

## 18. Comments that describe code or designs that are gone

- [ ] **low** — `components/rtxvulkan/pipeline/tracepipeline.hpp:31`–`:33`, `:47`, `:93`, `components/rtxvulkan/pipeline/tracepipeline.cpp:139`–`:142`, `components/rtxvulkan/shaders/lib/variants.glsl:50`–`:51`, `components/rtxvulkan/shaders/lib/counts.glsl:37` — They describe hit objects and threads sorted between stages, which is Shader Execution Reordering, and it was declined. `tracepipeline.cpp:141` says nothing calls `traceRayEXT`, and `visibility.rgen:163` does. The recursion depth of one is still right. Fix: rewrite them for `traceRayEXT` from ray generation, and restate why a subgroup operation is avoided there.
- [ ] **low** — `.claude/skills/shader-review/SKILL.md:65`–`:67` — States `reorderThreadEXT` "in `RTX_SHADE`, under `REORDER`" (neither exists), that `visibility.rgen` holds hit objects (it does not), and an eleven-word payload (`lib/payload.glsl` packs nineteen). Fix: correct the three facts.
- [ ] **low** — `components/rtxvulkan/shaders/lib/bindings.glsl:138`–`:141`, `:147`–`:148` — Says one atomic per hit "costs nothing" because few rays hit, and `variants.glsl:45`–`:51` measured the opposite. Fix: point both counters' comments at `FrameCounts::mMisses`.
- [ ] **low** — `components/rtxvulkan/vulkanrenderer.hpp:143`–`:146` — Says `setUpscale` raises a runtime, and it raises nothing: the upscaler is built in the constructor. Fix: say the pipelines are always built, and `createTargets` releases the images when the mode is off.
- [ ] **low** — `components/rtxvulkan/device/instance.hpp:50`–`:51` — "the upscaler's are its runtime's": no upscaler adds instance extensions now. Fix: drop the clause.
- [ ] **low** — `components/rtxvulkan/device/physicaldevice.hpp:72` — "an M12 question" is milestone narration. Fix: drop it.
- [ ] **low** — `components/rtxvulkan/present/presenttargets.hpp:77`–`:79` — "numbered rather than named", and the members are `mTarget` and `mSpare`. It goes with the present-target finding.
- [ ] **low** — `components/rtxvulkan/display/tonepass.hpp:17` — Says four fields are an `Image`, and there are five. Fix: correct the count.
- [ ] **low** — `components/rtxvulkan/CMakeLists.txt:172` — "the trace's five shaders": `shaders/lib/` is included by shaders in every folder. Fix: correct the reason.
- [ ] **low** — `components/rtx/view/offscreentrace.cpp:198` — "The placements are the one thing a redraw throws away": `clearPlacement` keeps them and clears the per-frame lists. Fix: correct the comment.
- [ ] **low** — `components/rtx/scene/deformertable.hpp:162`–`:163` — `compact` is "called where a sweep ends", and `MeshTable::drop` calls it on every drop. Fix: correct the comment.
- [ ] **low** — `components/rtx/scene/scenedesc.hpp:122`–`:124` — Says a hold of `sNoIndex` is empty. Only `holdTexture` accepts it, and `holdMesh` and `holdMaterial` assert. Fix: limit the sentence to textures.
- [ ] **low** — `components/rtx/mirror/mirrorpass.hpp:17` — "the extractor's anchor is a member set per walk": no such member exists. Fix: state the real reason.
- [ ] **low** — `components/rtx/mirror/extractionstats.hpp:32`–`:34` — `mRestood` explains restands by hashed node addresses, and the identity is now structural. Fix: restate what a restand now means.
- [ ] **low** — `components/rtx/mirror/cells/cellring.hpp:50` — Names `Known::mHolds`, and the field is `Reach::mHolds`.
- [ ] **low** — `components/rtx/mirror/materialresolver.cpp:65` — Names `findUpdater`, and the function is `findUpdaters`.
- [ ] **low** — `components/rtx/mirror/sceneextractor.hpp:338`, `components/rtx/mirror/emitterresolver.hpp:124`, `components/rtx/mirror/materialresolver.hpp:181` — "for the process" and "the process's cache", while `MeanTexels` lives with one extractor. Fix: correct them with the preprocessor finding in group 8.
- [ ] **low** — `apps/openmw/mwrender/rtx/rippleemitters.hpp:26`–`:28` — Says the rule cannot be lifted without editing upstream, and it was lifted to `ripplerules.hpp`. Fix: rewrite the paragraph.
- [ ] **low** — `components/rtx/environment/skybuilder.hpp:149`, `:155` — Cite `MWRender::WorldState::mStarRoll`, which does not exist. The field is `Rtx::WorldReading::mStarRoll`.
- [ ] **low** — `components/rtx/environment/moonbuilder.cpp:190` — Cites `skyutil.cpp:900`, and `Moon::setState` is at line 871. Fix: cite the function, not the line.
- [ ] **low** — `components/rtx/environment/frameworld.hpp:166` — "One call and not twenty assignments per host": `SkyReader` is the only producer. Fix: give the real reason.
- [ ] **low** — `apps/openmw/mwrender/pixels.cpp:87` against `pixels.hpp:19` — The header promises the map looks exactly as composited, but the alpha is truncated where GL rounds (200 × 200 gives 156, GL writes 157). Fix: `(sampled * mask + 127) / 255`.
- [ ] **low** — `components/myguirtx/texture.hpp:27` — Argues from video frames arriving through here, and videos now go through `shareTexture`. Fix: restate the reason.
- [ ] **low** — `apps/launcher/ui/graphicspage.ui:241` — The upscaling tooltip says the upscaler reconstructs the frame "whichever mode is chosen", and `off` has no upscaler. Fix: say what `off` does, and regenerate the `.ts` sources.
- [ ] **low** — `docs/rtx/architecture.md:15`, `README.md:38` — "The rasterizer is not modified", while `AGENTS.md` names three changes that move its picture. Fix: name the three, or point to `AGENTS.md`.
- [ ] **low** — `AGENTS.md:81`, `tools/omw/main.py:139` — `AGENTS.md` says `profile` runs in release "whatever is named", and the driver refuses another flavour named before `profile`. Fix: make the two agree.
- [ ] **low** — `apps/rtxtool/stager.hpp:51`, `apps/rtxtool/main.cpp:784`, `apps/rtxtool/stager.cpp:56` — Two comments say a million gold, and the constant is ten million.
- [ ] **low** — `apps/rtxtool/model/benchrecord.hpp:161`–`:162` — `BenchPlace::mClock` says "as this place ended", and `CardWatch` samples it through the measured frames. Fix: say range and mean over the measured frames.
- [ ] **low** — `components/rtx/common/slots.cpp:32` — `SlotChanges::note` compacts on every call, which defeats the deferred compaction `SlotSet::remove` argues for (`slots.hpp:107`–`:109`). Fix: compact once where the changes are read.

## Not a finding

- The flat identity maps: no caller of `Kept` keeps an iterator or a reference into a map across an insert into the same map.
- The crash catcher's note table, shared page, packaging and hang watch hold up, except the heartbeat sites in group 4.
- `holdWeather`'s crossing arithmetic, `PaintedTexture`, the optimizer's merge order, and the settings menus (checked against `Rtx::sUpscaleMenu` at compile time).
- `components/rtxvulkan/vulkanrenderer.cpp:140`: only a test calls `setSea`, so the game always runs `SeaState{}`. That is an observation, not a defect.
- `components/shader/automaps.cpp:269` against `components/shader/shadervisitor.cpp:595`: the second copy is upstream's rasterizer path, and the two differ only for a mesh whose texture coordinates stand past the tangent unit and nowhere before it. One helper would change the rasterizer's choice there, which the rules forbid, or keep two rules under one name.
- `apps/openmw/mwgui/mapwindow.cpp:622`: a map view can arrive after the first ask, when a later request makes its segment, so asking until it comes is the rule. One "asked" flag would leave such a tile blank.
- `components/rtxvulkan/shaders/lib/starfield.glsl:74` and `components/rtxvulkan/shaders/trace/spriteemitters.rgen:42`–`:47`: the side a texture stands at is the device's choice when it arrives (`TextureArray::chooseSide` holds an arrival to what the room fits), so the host cannot state the star sheet's extent or an emitter texture's levels at load. The queries read the device's own answer, once a pixel with stars and once an emitter a frame.

