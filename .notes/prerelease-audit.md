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

### 20. Stop the window size drift on a mixed-scale Wayland desktop
- **Skipped**: see `prerelease-audit_QUESTIONS.md`. The fault is confirmed in SDL's source, and no fix can be tested on one display.
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

## P3: structure

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
