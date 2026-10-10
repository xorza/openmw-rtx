# Pre-release audit: plan

The audit read the fork's whole diff against `upstream/master` (merge base `56670e4e95`, 1873
commits, 1663 files). Nine read-only passes covered the Vulkan device layer, the frame passes, the
shaders, the scene mirror, the core modules and MyGUI backend, the renderer seam and the engine, the
fork's hunks in upstream game code, the platform, crash catcher, build and CI, and the harness.
`extern/fidelityfx` is vendored and unchanged, so it was not read.

The audit found 52 faults. Faults with one cause or one fix share an item, so the plan has 32 items.
These areas were traced and held: the barriers, slot stamps, acceleration structures, texture
upload, the upscaler, the display chain, the shared struct layouts, the bindless indexing, the SDL3
return conventions, the presentation math, the visibility gates' threading and the warning-clean
hunks.

## How to read an item

- **Verdict**: CONFIRMED means the failing path was traced in the code. PLAUSIBLE means one point is
  open, and the item's **Close first** line says how to close it before the fix. When the point
  closes the other way, delete the item.
- **Upstream** marks an item that changes an upstream file. [Decision 1](#decisions) says which
  AGENTS.md line each one gets.
- The audit IDs (`SEAM-1` and the like) name the auditors' notes. They are not in the code.
- **Verify** each item as AGENTS.md says: build the touched targets, then `./omw test <binary>
  --gtest_filter=...`. For a change that a frame reads, also `./omw repeat --pairs=10` and `./omw shot
  --views=all --map --upscale=off --against=<baseline>`. For a shader change, also `./omw kernels
  --against`. Run `./omw gate` once at the end.
- A fix that this plan leaves for later goes to `.notes/ISSUES.md` in the same commit, as the
  issue only.

## Order

The items are in priority order, and they can be done in that order. Four links between them:

- Item 6 refuses a frame past the device's limit with an error that `renderFrame` throws. Item 3
  makes that throw reach the error box, so do item 3 first.
- Items 6 and 7 read one device limit. Expose it once, in item 6.
- Item 14 moves the GUI's texture reader onto the core's. Item 13 changes the core's reader. Do item
  13 first, and item 14 then carries the X8 rule into the GUI with no code of its own.
- Items 8 and 9 change the same reader and kernel. Do item 9 first: it refuses the skins that item
  8's test must not meet.

## Decisions

The user took these calls on 2026-10-11. The items carry them.

1. **Upstream files.** Five items change an upstream file as an improvement to the ray tracer's
   integration. Each one gets its line in AGENTS.md's Accepted diff in the same commit as the code:
   - item 3, the order in `~Engine`: a new line;
   - item 4, `EngineHost::getSavesFolder`: added to the existing `OMW::EngineHost` line;
   - item 15, the stamp on an embedded texture in `NifOsg::Loader`: a new line;
   - item 31, F2: added to the existing `Renderer::support` line;
   - item 32, the `StableIdentity` stamps that `Objects` puts on cell roots and references: a new
     line, which also covers the stamps already in the tree.
2. **The MSVC runtime (item 2):** app-local DLLs from the build's own toolset.
3. **Embedded NIF textures (item 15):** traced under a key of their own.
4. **Large textures with no mips (item 21):** above vanilla's largest side, the completed chain is
   encoded to BC7.
5. **A failed start of the ray tracer (item 22):** a better message only. A probe when the box is
   ticked would start the renderer not chosen, which AGENTS.md forbids.

## P0: release blockers

## P1: crashes and wrong pictures

### 8. Make the skin kernel compute what `RigGeometry::cull` computes
- **Audit**: MIRROR-1. CONFIRMED: both sides were read against each other.
- **Where**: `components/rtx/mirror/meshresolver.cpp:243-265` (`pose`), `:315-345` (`readRig`),
  `components/rtxvulkan/shaders/scene/skin.comp:44-58`. The reference is
  `components/sceneutil/riggeometry.cpp:170-208`.
- **Problem**: The rasterizer blends the bones that the skeleton has, keeps the blend's homogeneous
  column at (0,0,0,1), and then applies the skin transform `T` once. The kernel blends rows that
  already contain `T`, so `T`'s translation is scaled by the sum of the weights. Thus:
  - a vertex with no weights lands at the origin, but the rasterizer keeps its bind position (the
    kernel's comment says the opposite);
  - a vertex whose bones the skeleton does not have lands at 0, not at `T`'s translation;
  - weights that do not sum to 1 move the vertex.
  Partially or badly skinned mod meshes stretch to the origin.
- **Fix**: Pack the bones without `T`. Each pose gets one extra row set for `T`, and the kernel
  applies it after the blend. A run of zero influences writes the bind position and normal
  unchanged. Correct the kernel's comment.
- **Test**: `components_tests` `rtxvulkan/scene/skinpass.cpp`: a rig with `T` = translate(10,0,0)
  and three vertices: no group, weight 0.5 on one bone, and one bone the skeleton lacks. They land at
  bind, `0.5·(p·B)·R_T + t_T` and `t_T`, as `RigGeometry::cull` computes them.

### 9. Validate a skin where it is read
- **Audit**: MIRROR-3 (CONFIRMED), MIRROR-7, MIRROR-8 (PLAUSIBLE).
- **Close first**: MIRROR-7: find a loader path that gives an index past the bone count (a
  `BSTriShape` skin). MIRROR-8: find a caller that grows a skin's bones under one geometry. The
  checks are cheap, so add them even if no path is found, but say so in the commit.
- **Where**: `components/rtx/mirror/meshresolver.cpp:327-331` (`readRig`), `:162-169` (`fits`),
  `components/rtx/scene/deformertable.cpp:73-75`, `:99-118`.
- **Problem**:
  - A NaN or inf weight from `NiSkinData` reaches `GpuInfluence::mWeight`, and the kernel writes
    non-finite vertices. Every other deform input is checked.
  - A debug assert is the only check of a bone index against the bone count. In release, the kernel
    reads another mesh's bones.
  - `fits` does not compare a rig's bone count, so a skin with more bones writes past its pose run.
- **Fix**: `readRig` refuses a skin with a weight that is not finite or a bone index out of range.
  `fits` compares the held rows with the skin's bone count.
- **Test**: `rtx/mirror/extractor/skinning.cpp`: a NaN weight, and a bone index equal to the count,
  each give a refusal and no mesh. A rig that grew bones gets a new deformer.

### 10. Refuse non-finite and singular transforms in the cell ring and the motion record
- **Audit**: MIRROR-4 (CONFIRMED), MIRROR-6 (PLAUSIBLE).
- **Close first**: MIRROR-6: read `osg::Matrixf::inverse` (`invert_4x3`) and confirm that it divides
  by a zero determinant with no check.
- **Where**: `components/rtx/mirror/cells/cellreader.cpp:45-56`, `:281-300`, `:323-350`,
  `components/rtx/mirror/cells/cellplacer.cpp:357-369`, `components/rtx/scene/instancerecord.cpp:154`.
- **Problem**: The ring reads a `CellRef`'s position and rotation as they are, and `std::clamp` lets
  a NaN scale through. A distant reference with a NaN transform becomes a top-level instance. The walk
  refuses that case, but the ring does not. Also, a placement that shrinks to scale 0 is finite, but
  `moveRecord` inverts it, and inf or NaN motion reaches the reprojection and the denoiser.
- **Fix**: `readStatic` and `readGrass` refuse a reference whose transform is not finite, and record
  a `Refusal`. `moveRecord` writes `sStillTransform` when the product is not finite: a part that
  vanishes has no motion to report.
- **Test**: `rtx/mirror/cells/cellring.cpp`: a reference at a NaN position stands no placement and
  counts a refusal. A test of `updateInstanceRecords`: identity to `scale(0)` gives finite motion.

### 11. Give the fog layer a defined height when the weather has no fog
- **Audit**: SHADER-1. CONFIRMED.
- **Where**: `components/rtx/world/fogbuilder.cpp:291-313` (`fogExtinction`, `fogLift`),
  `components/rtx/world/frameworld.cpp:158`, `components/rtxvulkan/shaders/lib/fog.glsl:161`,
  `:239`, `:529-590`.
- **Problem**: `fogExtinction` reads a depth that is not above 0 (0, negative or NaN) as no fog,
  but `fogLift` uses the same depth as it is. A depth of 0 (a "no fog" mod, a Lua override) gives
  `mFogLift = 0`, and `fogHeightAt`, `fogColumnOver` and `fogBeamDepth` compute 0/0 at the fog base,
  which is the water level outdoors. A negative depth or wind gives a negative lift, and `exp`
  overflows, and `0 * inf` is NaN. The NaN enters the froxel history and spreads through its filters.
- **Fix**: `fogLift` reads the depth by `fogExtinction`'s rule. A depth that is not above 0 gives the
  clear weather's lift of 1, because a layer with no density has no shape to keep. A wind that is not
  above 0 adds nothing. Every lift is then positive, with no tolerance.
- **Test**: `rtx/world/fogbuilder.cpp`: depths 0, -1 and NaN give a lift of exactly 1, and a wind of
  -5 gives the lift of a wind of 0.

### 12. Refuse a field of view outside (0°, 180°) in the camera builder
- **Audit**: CORE-4. CONFIRMED that NaN reaches the frame constants.
- **Where**: `components/rtx/frame/camera.cpp:32-41`, `:159-171`,
  `components/rtx/frame/framesampling.cpp:22-26`, `:71-72`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp:904-920`.
- **Problem**: Lua's `camera.setFieldOfView` and the fallback `General_Werewolf_FOV` reach the camera
  with no range check. At 0°, the basis is zero, and `mUnitRight`, `mUnitUp` and `mArmsSpread` are
  NaN. Past 180°, the picture is mirrored. The rasterizer recovers on the next frame, but the ray
  tracer's histories keep the NaN.
- **Fix**: `makeCameraFromView` and `cameraAtFieldOfView` give no camera for a field of view outside
  (0, 180), as they do for a collapsed basis. The frame is skipped, because no correct picture
  exists, and a clamp would draw a wrong one. The renderer's warning names which reason skipped the
  frame. Upstream's bindings do not change.
- **Test**: `RtxCameraTest`: 0° and 180° give `nullopt`, and `cameraAtFieldOfView` at 0° and 200°
  gives no camera.

### 13. Read an X8 DDS as opaque
- **Audit**: CORE-2. CONFIRMED: the table in `osgdb_dds.so` was read.
- **Where**: `components/rtx/image/textureformat.cpp:81-101` (`readFormat`),
  `components/rtx/image/alphaimage.cpp:109-129`, `components/rtx/image/texels.cpp:114`.
- **Problem**: OSG loads `X8R8G8B8` as `GL_BGRA` and `X8B8G8R8` as `GL_RGBA`, each with an internal
  format of `GL_RGB`, and GL samples alpha as 1. `readFormat` reads the internal format for the
  16-bit X formats but not for these, so the spare byte (often 0) becomes alpha. The surface is
  traced as a hole or as never solid.
- **Fix**: `readFormat` reads `GL_BGRA` or `GL_RGBA` with `GL_UNSIGNED_BYTE` and an internal
  `GL_RGB` as an opaque format, by the rule the 16-bit X formats already follow.
- **Test**: The `readFormat` table in `apps/components_tests/rtx/image/texels.cpp:290-332` gets both
  rows. `reachesSolid` of a 4x4 image of that format with byte 3 at 0 is true.

### 14. Read every GUI texture through the core's reader
- **Audit**: CORE-3 (PLAUSIBLE), and the GUI half of CORE-2.
- **Close first**: Read `osg::Image::getColor` in OSG 3.6.5 and confirm that it returns white for
  the packed 16-bit types and for `GL_HALF_FLOAT`.
- **Where**: `components/myguirtx/slottexture.cpp:33-54`, `:93-127` (`byteLayoutOf`, `sendImage`).
- **Problem**: `sendImage` has a byte reader of its own and sends the other formats through
  `getColor`. Its reader copies an X8 file's spare byte as alpha, so the image does not show. For
  `GL_UNSIGNED_SHORT_5_6_5`, `_1_5_5_5_REV` and `_4_4_4_4_REV`, `getColor` gives opaque white, so old
  mods' icons and book images show as white boxes.
- **Fix**: `sendImage` names the format with `Rtx::readFormat`. A format that the core widens goes
  through the core's widening (`describeFinestLevel` or `layImage`), and its RGBA8 bytes are copied.
  `getColor` stays for S3TC only. With item 13 done, an X8 file is then opaque here as well.
- **Test**: `RtxSharedTextureTest.aPictureIsSentAsItsColoursReadInEveryFormat`: add X8 with byte 3 at
  0 (sent with alpha 255), and the three packed formats with known words, such as `0xF800` R5G6B5 to
  `(255, 0, 0, 255)`.

### 15. Trace a NIF's embedded texture
- **Audit**: MIRROR-5. CONFIRMED. **Upstream** (decision 1).
- **Where**: `components/rtx/mirror/materialresolver.cpp:353-357` (`takeTexture`),
  `components/nifosg/nifloader.cpp:2024` (`handleInternalTexture`), and the texture table's key.
- **Problem**: A NIF that carries its texture as `NiPixelData` gives an image with no file name.
  `takeTexture` returns `sNoIndex`, and the surface is traced untextured with no word in the log.
  Vanilla has one such mesh, `meshes\i\tx_crystal_02.nif` (an ingredient), so a vanilla object
  draws wrong.
- **Fix**: `handleInternalTexture` stamps the image with the model's path and the `NiPixelData`
  record's index, in user data and not as a file name, which OSG or the image cache could try to load.
  The texture table keys a stamped image by that pair, and the cache and the upload hold it as they
  hold a file. An image with neither a file name nor a stamp is refused with `Refused::Texture` and
  the model's name, as `emitterresolver.cpp:76` refuses a sprite.
- **Test**: `rtx/mirror/extractor/materials.cpp`: a stamped nameless image takes a texture slot, two
  drawables with one stamp share it, and an unstamped nameless image gives one `Refused::Texture`.
  Then `./omw shot` at a place with the crystal.

### 16. Wake a frozen root when the game adds a controller under it
- **Audit**: MIRROR-2. PLAUSIBLE.
- **Close first**: Find an object that hits the path: an activator, door or container with
  animation sources and no idle group, which a script plays (`PlayGroup`). Without one, the fix
  stays, because the check costs one load, but the commit says that no case was found.
- **Where**: `components/rtx/mirror/frozenroots.hpp:96-106` (`Face`), `frozenroots.cpp:36-63`,
  `apps/openmw/mwrender/animation.cpp:1170-1180`.
- **Problem**: A root with no update callbacks freezes. A later `PlayGroup`, or a container that opens
  with an animation, adds `KeyframeController`s deep inside it. `Face` does not change, so the ray
  tracer shows the frozen pose.
- **Fix**: Add `getNumChildrenRequiringUpdateTraversal()` and the root's own update callback to
  `Face`. OSG raises the count along the parent chain.
- **Test**: `rtx/mirror/extractor/` (frozen roots): freeze a root, add a moving callback on a
  grandchild, and the next walk moves the placement.

### 17. Drop a queued map tile after a cut
- **Audit**: SEAM-3. CONFIRMED.
- **Where**: `apps/openmw/mwrender/rtx/viewqueue.cpp:32-55`, `tracedview.cpp:114-148`,
  `pendingpaints.hpp:30-60`, `components/rtx/mirror/cells/cellring.cpp:149-169`.
- **Problem**: A tile for a cell that the player just explored waits in `ViewQueue` only while the
  ring will stand ground under it. After a door, a teleport or a worldspace change, the tile is drawn
  against the new scene, and the world map shows that cell black until the player returns.
- **Fix**: A world view keeps the cut count it was asked under. `ViewQueue::draw` drops a deferred
  view that was asked before the last cut, and `PendingPaints::finish` drops its paint.
- **Test**: GPU binary: ask a world view over an exterior cell, then cut and move the eye out of
  reach. The tile's `getCopy()` stays null and the overlay does not change.

### 18. Size the cursor by the factor the video driver scales it by
- **Audit**: GAME-2. CONFIRMED in SDL 3.4.16's source. The SDL3 port is in the Accepted diff.
- **Where**: `components/sdlutil/sdlcursormanager.cpp:38`, `:131-151`,
  `apps/openmw/mwgui/windowmanagerimp.cpp:133-140`, `:2519-2531`.
- **Problem**: `createCursor` makes the base image `size / displayScale` and adds the full image as an
  alternate. X11 uses the base image as it is. On an X11 desktop at 150 %, the GUI is at 1.5 and the
  cursor is at 1/1.5 of its size.
- **Fix**: One `SDLUtil` function gives the divisor: the pixel density on Wayland and Cocoa, the
  content scale on Windows (the code sets `SDL_HINT_MOUSE_DPI_SCALE_CURSORS`), and 1 on X11. The
  platform choice stays inside that function.
- **Test**: `components_tests` on that function: X11 at content scale 1.5 gives 1, Wayland at
  density 1.5 gives 1.5, and Windows at content scale 1.5 gives 1.5.

### 19. Migrate a settings file that states only `resolution y`
- **Audit**: GAME-3. CONFIRMED.
- **Where**: `components/settings/migration.cpp:11-24`.
- **Problem**: The migration starts only on `resolution x`. Upstream writes a value only when it
  differs from 800 × 600, so an upstream 800 × 480 window is in the file as `resolution y = 480`
  alone. The window then opens at 800 × 600.
- **Fix**: Either key starts the migration. Each key that is present moves to its window counterpart,
  and both resolution keys become 0.
- **Test**: `SettingsMigrationTest.anUpstreamFileKeepsTheWindowItHad`: a map with only
  `resolution y = 480` gives `window height = 480`, both resolutions at 0, the marker, and no
  `window width`.

### 20. Stop the window size drift on a mixed-scale Wayland desktop
- **Audit**: GAME-1. PLAUSIBLE: traced through SDL's Wayland backend, not run.
- **Close first**: Run the game windowed on two outputs at scales 1.0 and 2.0, and start it twice.
  The size in `settings.cfg` must change between the starts.
- **Where**: `apps/openmw/mwrender/renderer.cpp:395-409` (`openWindow`),
  `components/sdlutil/sdlvideowrapper.cpp:79-82`, `apps/openmw/mwgui/windowmanagerimp.cpp:1306-1316`.
- **Problem**: `openWindow` divides the stored pixel size by the density of the hidden window. On
  Wayland, SDL gives an unmapped window the largest scale of any output. The window opens at half
  size on the 1.0 output, `windowResized` stores the half, and each start halves it again.
- **Fix**: Store no window size until the first density change after the window is mapped.
- **Test**: A pure helper that takes the density at creation and after mapping: a stored size goes
  through unchanged.

## P2: hardening

### 21. Hold the memory of a large texture with no mips
- **Audit**: CORE-5. CONFIRMED arithmetic. How much a load order loses depends on its mods.
- **Where**: `components/rtx/image/texturedata.hpp:214-229`, `texturedata.cpp:74-77`,
  `components/rtxvulkan/texture/texturecost.cpp:57`, `:73`, `texture.cpp:163`, `:195-215`.
- **Problem**: Every single-level file larger than 1x1 gets an RGBA8 chain on the device. A replacer's
  4096² BC1 with no mips (8 MB on disk) costs about 89 MB, and `standFile` cannot stand it from a
  coarser level, so the budget must take it whole or refuse it.
- **Fix**: A file above vanilla's largest single-level side (512, a named constant with the
  comment's census as its reason) has its completed chain encoded through `Bc7EncodePass`, every level
  included: 1 byte a texel, about 22 MB for 4096². A file of 512 or less keeps the loose chain, so
  no vanilla picture moves. Update the comment on `wantsCompletedChain`, which says why the chain is
  loose. The replacer's own top level is encoded again from its BC1 or BC3, and BC7's mode 6 can move
  a channel's low bit.
- **Test**: `RtxTextureDataTest`: a one-level 512² file is completed loose, and a 4096² one is
  completed in BC7. `texturecost`: the 4096² file is priced at its BC7 chain. A GPU test: the
  encoded chain of a 1024² file has every level, and its 1x1 level is the file's mean within what
  BC7 keeps.

### 22. Tell the player how to recover when the ray tracer cannot start
- **Audit**: SEAM-4. CONFIRMED.
- **Where**: `apps/openmw/engine.cpp:657-667`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp:204`,
  `components/debug/debugging.cpp:524-535`.
- **Problem**: The error box shows only the backend's reason. It does not say that `[RTX] enabled`
  chose the renderer, or that the launcher turns it off. A player who ticked the box in the game
  sees the box at every start.
- **Fix**: `Engine::go` catches the renderer's construction failure and throws it again with the
  backend's reason, then that `[RTX] enabled` chose the ray tracer, and that the launcher's
  Graphics page or `settings.cfg` turns it off. Nothing else changes.
- **Test**: A unit test of the message, which is a function of its own.

### 23. Reject a device, not the whole selection, on a failed query
- **Audit**: VKDEV-1. PLAUSIBLE.
- **Close first**: Confirm that `examine` runs for every device before `profileOf` reads the
  version (`physicaldevice.cpp:289-321`). The audit read it so, and the failing driver is the open
  point.
- **Where**: `components/rtxvulkan/device/physicaldevice.cpp:164-198` (`examine`), `:289-321` (`select`).
- **Problem**: One device that answers a query with an error throws `DeviceError` out of `select`, so
  no device is chosen. `examine` also asks a device below 1.4 about image flags that 1.0 does not
  define, which a validation run reports.
- **Fix**: Read the API version first, and skip the other queries below `sApiVersion`. A failed query
  becomes that candidate's `mObstacle`.
- **Test**: A unit test in the style of `profileOf`: a candidate whose query fails is rejected, and
  the next one is chosen.

### 24. Take the timeline value only after the submit succeeds
- **Audit**: VKDEV-2. CONFIRMED.
- **Where**: `components/rtxvulkan/device/commands.cpp:62-83`, `device/timeline.hpp:85`.
- **Problem**: `timeline.next()` counts the submit before `vkQueueSubmit2`. An out-of-memory result
  throws, but the clock then waits for a value that no submit signals. A later wait times out and
  blames the device.
- **Fix**: Signal `getNext()`, and commit it with `next()` only after the submit returns success.
  Only the device's thread submits, so no other submit can take the value in between.
- **Test**: None practical: it needs a layer that injects faults.

### 25. Keep the player's name out of the prefilled crash issue
- **Audit**: BUILD-3. CONFIRMED.
- **Where**: `components/crashcatcher/crashsummary.cpp:150`, `crashpackage.cpp:448-475` (`newIssueUrl`).
- **Problem**: The summary's last line is the dump's absolute path, which holds the account name. The
  public issue's body carries it.
- **Fix**: `newIssueUrl` leaves out the dump line. The log and the package keep the full path.
- **Test**: `CrashPackage`: `newIssueUrl` over a summary with `/home/someone/...` has no `someone`.

### 26. Make the crash catcher robust on each platform
- **Audit**: BUILD-4 (PLAUSIBLE), BUILD-5, BUILD-6 (CONFIRMED), BUILD-7 (PLAUSIBLE).
- **Close first**: BUILD-4: on Windows, open a 6000-character URL with `SDL_OpenURL`. BUILD-7 is a
  fault of the design (a predictable name opened without `O_EXCL`) whatever the system's settings,
  so it needs no close.
- **Where**: `components/crashcatcher/crashpackage.cpp:41-43`, `crashpadmonitor.cpp:698-700`,
  `components/platform/processwin32.cpp:127-145`, `components/crashcatcher/crashpadclient.cpp:131-143`,
  `components/platform/sharedmemoryposix.cpp:22-44`.
- **Problem**:
  - On Windows, a new-issue URL past about 2083 characters may not open, and the result of
    `SDL_OpenURL` is ignored.
  - On Windows, `runShell` gives UTF-8 to the ANSI `std::system`, so a non-ASCII path arrives
    corrupted. `shellWord` lets a trailing backslash escape its closing quote.
  - On Linux, `StartHandler` succeeds once the fork succeeds. A monitor that dies at a read-only
    report folder leaves the game with no catcher, a log that says otherwise, and a page in `/dev/shm`
    until reboot.
  - The shared page's name is predictable, and `create` opens a page that another user made.
- **Fix**: Check `SDL_OpenURL`, and on failure open the bare `/issues/new?title=` URL. On Windows,
  widen to UTF-16 and use `_wsystem`, and double a trailing run of backslashes. After `StartHandler`,
  wait for a bounded time for a ready word in the page, and report a monitor that did not start; the
  game then unlinks the page. Create the page with `O_EXCL`: unlink and retry a stale page of the
  player's own, and refuse any other.
- **Test**: `ProcessWin32` with a word that holds `ö` and a word that ends in `\`. The crash matrix
  with a read-only `OPENMW_CRASH_REPORTS`: `install` fails and no page stays. `SharedMemory`: a name
  made first with another mode is not mapped.

### 27. Make a failed or cut-short harness run fail
- **Audit**: HARNESS-2, HARNESS-4, HARNESS-5, HARNESS-7. CONFIRMED.
- **Where**: `apps/rtxtool/hosted.cpp:207-224`, `session.cpp:241-251`, `:469-490`,
  `stopwriter.cpp:503-521`, `measurer.cpp:85-86`, `instruments/frametimes.cpp:283-305`,
  `main.cpp:786`, `model/runrecord.cpp:47-88`.
- **Problem**:
  - A `bench` whose window closes before the last stop exits 0 with no `--json`, no hashes and no
    `--against` comparison.
  - `--doll` with an id that is not an NPC throws out of `Session::frame` and ends the run with no
    report.
  - `Engine::frame` catches an exception from `beginStop`, and the stop begins again every frame.
    `--perf-control` with no reader waits 30 s in the frame each time, with no end.
  - `hashes.csv` and `--json` from the last run stay until this run finishes, so a run that dies
    leaves another build's hashes beside its pictures.
- **Fix**: A session that ends with stops left calls `RunRecord::abandon`. `--doll` looks the id up
  in `ESM::NPC` first, and `fail()`s when it is not there. `Session::beforeFrame` catches and
  abandons. `PerfControl::open` runs before the engine starts. Remove `hashes.csv` and the `--json`
  path next to `clearPictures`.
- **Test**: `components_tests/rtxtool/runrecord.cpp`: a three-stop record with one place, described
  without `finish`, has a non-zero status. The `clearPictures` test also removes `hashes.csv`.

### 28. Make the harness's names and numbers safe
- **Audit**: HARNESS-3, HARNESS-6, HARNESS-9. CONFIRMED.
- **Where**: `apps/rtxtool/run.cpp:389-390`, `instruments/framehashes.cpp:212-213`, `:277-287`,
  `main.cpp:562-579`, `options.cpp:56`, `:211-218`, `model/benchspec.cpp:59-71`.
- **Problem**:
  - A stop named after its cell (`-2,-9`, `Balmora, Guild of Mages`) puts commas in `hashes.csv`, and
    the next `--against` cannot read it. A `:` in a picture's name writes an NTFS stream.
  - With `--views`, the options `--cell`, `--view`, `--pos` and `--look` are ignored with no word.
  - Frame counts from seconds wrap past `uint32`, and `--frames=4294967295` equals the "until closed"
    sentinel.
- **Fix**: Name a stop that has no view by `slugOf`, and have `FrameHashes::write` refuse a `,`.
  Refuse `--views` beside a place option. Bound `--frames` below `sUntilClosed`, and refuse a span
  whose count does not fit.
- **Test**: `rtxtool/run.cpp`: `stopFor` on cell `-2,-9` gives a name with no `,`, `:` or `/`.
  `framehashes.cpp`: a round trip. `options.cpp`: `shot --views=a --pos=1,2,3` and
  `--frames=4294967295` are refused. `benchspec.cpp`: 1e8 s at 1/60 is refused.

### 29. Watch the card the renderer runs on, check the harness's writes, and keep its keys out of text
- **Audit**: HARNESS-8, HARNESS-11 (PLAUSIBLE), HARNESS-12 (CONFIRMED).
- **Close first**: HARNESS-8 needs a machine with two cards, so the fix stands on the code alone.
  HARNESS-11: write a PNG to `/dev/full` and see whether the run says "wrote".
- **Where**: `apps/rtxtool/instruments/nvml.cpp:76`, `instruments/amdgpu.cpp:57-90`,
  `components/rtx/renderer/png.cpp:62-90`, `instruments/framehashes.cpp:227-230`,
  `model/benchrecord.cpp:557-560`, `noise.cpp:157-160`, `skykeys.cpp:9-23`, `homekey.cpp:45-55`,
  `memorykey.cpp:41-47`.
- **Problem**:
  - The card watch reads NVML index 0 or the first AMD card in `/sys`, not the Vulkan device. On a
    laptop with an APU and a discrete card, it can read the APU.
  - The PNG writer and the text writers do not check the close. A full disk or a CIFS mount reports
    "wrote" over a truncated file.
  - The sky, Home and End keys read the raw key state, so text typed in the console turns the sky.
- **Fix**: Pick the card by the renderer's PCI address (`VK_EXT_pci_bus_info`), and say "card not
  watched" when no card matches. Encode the PNG in memory, and write it with a checked `write` and
  `close`. Check the text writers after an explicit `close`. Skip the three key listeners while the
  console or text input is active.
- **Test**: `rtxtool/cardwatch.cpp`: `AmdGpu::find` over a fake sysfs with two AMD cards picks the
  asked PCI address. A write to `/dev/full` gives an error.

## P3: structure

### 30. Let the required formats table give its own length
- **Audit**: VKDEV-3. CONFIRMED.
- **Where**: `components/rtxvulkan/device/requirements.cpp:262`.
- **Problem**: `std::array<RequiredFormat, 4>` has three entries. The fourth asks every device about
  `VK_FORMAT_UNDEFINED`, with an empty reason.
- **Fix**: `constexpr std::array sRequiredFormats{ ... }`, as the other tables are.
- **Test**: No entry of `getRequiredFormats()` is `VK_FORMAT_UNDEFINED` or has an empty `mFor`.

### 31. Answer F2, F3 and F4 under the ray tracer through `Renderer::support`
- **Audit**: SEAM-5, SEAM-6. CONFIRMED. **Upstream** for F2 (decision 1, the `Renderer::support` line).
- **Where**: `apps/openmw/mwgui/windowmanagerimp.cpp:2410-2418`,
  `apps/openmw/mwrender/rtx/rtxsupport.cpp:29-31`, `:108`, `apps/openmw/mwrender/rtx/rtxrenderer.hpp`.
- **Problem**: F2 says that post-processing "is not enabled" when the setting is on and the ray
  tracer declines it. F3 and F4 do nothing, and `RenderSupport` does not declare the stats overlay.
- **Fix**: F2 asks `support().declinedSetting("Post Processing", "enabled")` and shows
  `MWRender::notAvailable` with that reason. `rtxSupport()` declares the stats overlay, and
  `RtxRenderer::functionKey` reports it once.
- **Test**: `RtxSupportTest`: the declaration names the overlay.

### 32. Make one helper the only maker of a cell root
- **Audit**: SEAM-8. CONFIRMED that the stamp is missing. No wrong picture was shown.
- **Where**: `apps/openmw/mwrender/objects.cpp:49-55`, `:209-221`. **Upstream** (decision 1, new line).
- **Problem**: `updatePtr` makes a cell root without the `StableIdentity` stamp and the "Cell Root"
  name, and `insertBegin` then uses it again. The mirror tells roots apart by the stamp.
- **Fix**: One private helper names and stamps a cell root, and `insertBegin` and `updatePtr` call it.
- **Test**: Move a Ptr into a cell with no node, and the new root carries a `StableIdentity`.
