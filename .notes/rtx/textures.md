# Three texture paths

A plan for the three open entries in `.notes/ISSUES.md`. They look unrelated — one is the OpenGL
GUI, one is the scene mirror, one is Vulkan setup — and they are the same mistake three times: **a
texture is treated as a thing that is made, rather than a thing that is kept.** Every write
reallocates it, every slot that stopped naming a file is rebuilt as a grey one, and every one that
arrives asks the queue for itself alone.

None of the three is a hard failure and none of them is visible in a still. What they cost is
allocation on the frame path, bandwidth on a picture that changed in one corner, and a queue round
trip per texture on a load — all three of which this fork has already declared it does not pay
(`CLAUDE.md`, "Conventions").

Two more came out of doing the work and are §5 and §6. They are the same mistake from the other
side: **a texture is owned by something that has to remember it**, rather than by whatever names it
— a slot swept by a walk that is told when to look, and a layout invariant kept by hand at a call
site rather than by the class that states it.

## 1. What was found

### 1.1 `MyGUIPlatform::OSGTexture` rebuilds itself on every write

`lock` allocates an `osg::Image` (`myguitexture.cpp:126`) and `unlock` allocates an
`osg::Texture2D` and hands it that image (`myguitexture.cpp:141`). Nothing is reused, and OSG
re-uploads the whole surface because the image it is given is new.

For a picture written once a frame that is **two allocations and a full upload per frame**: the
video widget is `Picture::set` per decoded frame (`videowidget.cpp:84`), and a 512×512 frame is a
megabyte of `allocateImage` plus a megabyte of `glTexImage2D`, every frame, plus a `Texture2D`
allocation and the GL object teardown behind it.

It also does not implement `MyGUIPlatform::RegionTexture`, so `Picture::setRegion` takes its
documented fallback and writes the whole surface. The one caller is the global map overlay
(`globalmap.cpp:349`), which repaints a cell-sized square when a cell is entered: **eighteen pixels
square, sent as two megabytes.** That is the exact cost `RegionTexture` was introduced to remove,
and the RTX backend already removes it (`myguirtx/texture.cpp`, `writeRegion`).

**Why the reallocation is there.** It is not an oversight: `RenderManager::doRender` hands the
drawable a raw `osg::Texture2D*` in a batch (`myguirendermanager.cpp:448`), the batch vectors are
four deep, and `Drawable::drawImplementation` says in as many words that it "may run in parallel
with the update traversal of the next frame" (`myguirendermanager.cpp:85`). Writing an image in
place while the draw thread uploads it is a data race. Making a new texture every time is the crude
way out of it, and the `ref_ptr` in the batch keeps the old one alive until the draw is done.

**The mechanism for doing better is already wired up and unused.** `doRender` reads the texture's
data variance and marks the drawable `DYNAMIC` for that frame (`myguirendermanager.cpp:449`).
`DYNAMIC` is what makes `osgViewer` hold the next update traversal until the object has been drawn,
which is precisely the guarantee an in-place write needs. Nothing in the tree ever sets it on a
MyGUI texture, so the branch is dead.

### 1.2 A freed scene texture slot is described as a texture that failed to load

`SceneDesc::addTexture` is called from two places and both refuse an empty path
(`sceneextractor.cpp:1056`, `:782`), so **the only thing that can put an empty path in the table is
`SceneDesc::release`** (`scenedesc.cpp:499`), which blanks a slot nothing names any more and leaves
it in the table for the next arrival to take over. The table never shrinks, by design — a slot index
is a material's texture index and a hit's custom index, and renumbering is what a cell boundary used
to cost a fifth of a second.

So an empty path means *free slot*, not *missing texture*. `SceneTextures::describe` cannot tell the
two apart: it walks the table by position, finds no file to ask for, and builds the grey stand-in —
deliberately, and with a test that pins it
(`texturebuilder.cpp`, `aFreedSlotTakesTheStandInWithoutCountingAsUnreadable`).

**The inventory doll is where this shows because the doll releases constantly.**
`TracedView::rebuildSubject` walks the subject, retires it and hands it over on *every* redraw
(`tracedview.cpp:161`), and a redraw is exactly the moment the figure changed — a part swapped, a
weapon shown, `NpcAnimation::updateParts` having replaced a body part. The world retires every frame
too, but only loses a texture at a cell boundary. One garment coming off is one blanked slot.

**It is waste, not corruption.** `release` marks every live material's diffuse, normal, emissive and
layer textures kept before it frees anything (`scenedesc.cpp:479-495`), so no live material can name
a freed slot. What it costs is a stand-in image, 1024 floats of neutral shading and a descriptor
write for a slot nothing samples — paid whenever a full describe runs — and, between full describes,
a descriptor left naming a destroyed image, which is legal only because nothing reads it.

**There is a trap under the obvious fix.** `TextureArray`'s constructor overwrites every
description's slot with its position in the span (`texture.cpp:326-330`). That is correct *only*
because the full describe emits every slot in table order. Skipping free slots without touching that
loop silently renumbers every texture in the scene, and every material index with it.

### 1.3 Every GUI texture created costs a queue round trip

`GuiTextures::add` clears a new image through `mPool.submitAndWait` (`guitextures.cpp:32`) and
`GuiTextures::write` copies through another (`guitextures.cpp:83`). Each is a submit plus a wait on
the whole queue, per texture, per write. `CommandPool`'s own documentation says what that is for and
what it is not: "Anything that happens once per resource wants a `Batch` instead, or the queue is
asked to do one thing three hundred times" (`commands.hpp:32`).

The interface creates one texture per picture widget, per font atlas and per traced view, and every
one of them pays a clear-and-wait before it holds anything. A video frame then pays a second one per
frame for its write.

`GuiTextures::write` cannot simply be batched as it stands: it keeps one `mStaging` buffer grown to
the largest region ever written, so a second queued write would overwrite the first's staging before
the copy ran.

### 1.4 A texture nothing names is only noticed when a mesh or a material dies

`SceneDesc::release` answers the ordinary frame with two comparisons — the live mesh count and the
live material count against the keep sets it was handed — and returns before it looks at a texture
at all (`scenedesc.cpp:395`). That is a proxy for "did anything die", and a texture is not the only
thing that can.

Two ways one stops being named while every mesh and material survives:

- **An emitter's sprite.** The extractor keys `mEmitterTextures` on the particle system and sweeps
  it (`sceneextractor.cpp:472`), but a particle system is a placement rather than a mesh or a
  material: losing one moves neither count.
- **An animated material.** `SceneExtractor::resolveMaterial` re-reads a material whose state set a
  controller rewrote and hands the result to `setMaterial`. The material keeps its slot — that is
  the point of it — and the texture it stopped naming keeps its.

The slot and its uploaded image then last the session. It is bounded rather than unbounded, because
`addTexture` keys on the path and a flame cycling through four frames settles at four slots; what it
is, is memory nothing can reach behind a descriptor nothing samples.

**The early-out cannot simply count textures as well.** The keep set the caller passes has duplicates
in it — several emitters share one sprite — so its size is not a live count, which is what
`release`'s own comment says. Deduplicating it is a sort or a set per frame, on the frame path.

### 1.5 The GUI texture's layout invariant is kept by hand at its one call site

`VulkanRenderer::traceGuiTexture` writes a GUI texture with transfer commands: it transitions the
texture in, clears it whole *if the picture will not cover it*, copies the traced picture over it,
and transitions it back (`vulkanrenderer.cpp:825-857`).

The transition in names `VK_PIPELINE_STAGE_2_CLEAR_BIT` as its destination scope — the stage of the
clear, which is the optional half. Where the picture covers the whole texture the clear is skipped,
and then nothing orders that transition's own write against the copy, which runs at
`VK_PIPELINE_STAGE_2_COPY_BIT`. Synchronization validation says exactly that: *the current
synchronization allows `TRANSFER_WRITE` at `CLEAR`, but to prevent this hazard it must allow these
accesses at `COPY`.*

The failure set matches to the test: `aPictureShorterThanItsTextureLeavesTheRestAtTheClearColour` —
the one case where the clear runs and its trailing barrier happens to cover the copy — passes, and
the three where the picture fills its texture fail.

**The transition in also starts from `VK_IMAGE_LAYOUT_UNDEFINED`**, while `GuiTextures` documents the
resting layout as `SHADER_READ_ONLY_OPTIMAL` and says that is "where anything that borrows it has to
put it back". Discarding is legal and deliberate here — the texture is fully overwritten either way
— but it is also what makes the invariant unenforceable: the class states a contract that its only
borrower contradicts, in a barrier whose scope has to agree with commands recorded forty lines
further down.

**The suite is green because the harness does not ask.** `Testing::buildRenderer` loads the
validation layers without `mValidation.mSynchronization`. Turned on, the whole 242-test suite has
these three failures and no others, and the run time does not move — 6.0s against 6.1s.

## 2. `OSGTexture`: persistent storage, a dirty rectangle, one promotion

The shape to reach: **a texture is allocated once and written in place, and what goes to the GPU is
the rectangle that changed.**

Storage. `createManual` allocates the `osg::Image` and the `osg::Texture2D` once and keeps both.
`lock` returns `mImage->data()` and allocates nothing; because the buffer persists, the previous
contents are still in it, which is what makes writing part of a picture possible at all. `destroy`
releases both.

Concurrency, stated as a rule rather than a hope:

- **A texture that has never been drawn cannot be in flight**, so it is written in place. Font
  atlases, skins and every one-shot picture end here and never leave: they are filled before the
  interface has drawn them once.
- **The first write after it has been drawn** swaps in a fresh `osg::Texture2D` over the same image
  — today's copy-on-write, kept, but paid once in a texture's life instead of once per write — and
  marks it `osg::Object::DYNAMIC`.
- **Every write after that is in place**, and `DYNAMIC` is what makes it safe: `doRender` already
  propagates it to the drawable, and `osgViewer` will not start the next update until that drawable
  has been drawn.

`RenderManager::doRender` gains one call — `osgtexture->markDrawn()` — which is what "has been
drawn" means. It is exact rather than a heuristic: a texture that MyGUI has never batched cannot be
in a batch vector.

Uploads. The texture carries an `osg::Texture2D::SubloadCallback` holding the union of the
rectangles written since the last apply. `load` does the first whole-surface `glTexImage2D`;
`subload` does `glTexSubImage2D` of the union and empties it, and does nothing when it is empty.
`unlock` marks the whole surface; `writeRegion` marks its rectangle. The union is per texture and
this build has one GL context; a second context would want it per context, and that is a comment
rather than a mechanism until there is one.

`RegionTexture`. `OSGTexture` implements it, writing the caller's tightly packed rows into the image
row by row and marking the rectangle. With both backends implementing it, `Picture::setRegion`'s
`dynamic_cast` per write becomes a lookup done once in `set` and cached beside `mTexture`; the
whole-image fallback stays, because it is still the answer for a picture MyGUI took at three
channels.

Steps:

1. `OSGTexture` holds a persistent `mImage` and `mTexture`; `lock`/`unlock` stop allocating.
   `mLockedImage` becomes an `mLocked` flag.
2. Add the subload callback and the dirty rectangle; `unlock` marks the whole surface.
3. Add `markDrawn`, the promotion, and the `DYNAMIC` mark. `doRender` calls `markDrawn`.
4. Implement `RegionTexture` on `OSGTexture`.
5. `Picture` caches the `RegionTexture*` in `set` and stops casting in `setRegion`.

Tests, in a new `apps/components_tests/myguiplatform/texture.cpp` beside the existing `pixels.cpp`.
None of this needs a GL context — the callback's arithmetic is the part worth asserting, not
`glTexSubImage2D`:

- `lock` returns the same pointer across writes, and the second `lock` still holds what the first
  wrote.
- A region write lands on exactly the pixels of the rectangle and changes nothing outside it, with
  the expected bytes computed by hand.
- Two region writes leave a union rectangle that is their bounding box; a whole-surface write leaves
  the whole surface; a consumed rectangle leaves nothing.
- A texture written before it is drawn keeps its `osg::Texture2D`; the first write after `markDrawn`
  replaces it exactly once and marks it `DYNAMIC`; the write after that keeps it.

To measure when it lands: allocations per frame with a video playing, and the bytes uploaded when a
cell is entered on the global map.

## 3. Free slots stop being textures

The shape to reach: **the describe path never sees a free slot, and a slot is carried explicitly
from end to end instead of being inferred from its position.**

1. `SceneDesc` says so directly — `isTextureFree(Index)` reading the same state `release` writes,
   rather than every reader testing a path for emptiness. `SceneTextures` skips free slots in both
   constructors, so no description is ever built for one and `mDescribed` counts textures again.
2. `TextureArray`'s constructor stops renumbering. It takes the descriptions with the slots they
   already carry, which is what `write` has always honoured, and the loop at `texture.cpp:326-330`
   goes.
3. `TextureArray` is told the scene's table length and sizes `mTextures` and `mShadingValues` to it,
   rather than to the highest slot it happened to be handed. This is load-bearing in a way that is
   easy to miss: `SceneUploader::recognises` compares `getTextureCount()` against what it last
   uploaded, so an array that stopped short of a trailing free slot would fail to recognise its own
   scene on the next frame and rebuild the world from nothing.
4. `reserveSlot` grows to a slot rather than asserting the slot is the next one, since arrivals can
   now leave gaps behind them.
5. The stand-in stays exactly where it is, for a texture that named a file and could not be read.
   That is the case it was written for, and `getUnreadable` goes back to counting only it.

Tests — extend `apps/components_tests/rtxbridge/texturebuilder.cpp`, which already builds the
fixture. `aFreedSlotTakesTheStandInWithoutCountingAsUnreadable` inverts: the freed slot yields no
description at all, the named-but-missing one still yields the stand-in and still counts as
unreadable. Add the case the renumbering trap hides — describe a table whose *first* slot is free and
assert each description's `mSlot` is its own slot, not its position — and, in
`apps/components_tests/rtx/`, that an array built from a table with a trailing free slot reports a
count equal to the table's length.

To measure when it lands: descriptions built and bytes uploaded on the frame a doll is rebuilt, and
the same on a cell boundary crossing.

**A related finding, not part of this.** `SceneDesc::release` returns early when the mesh and
material counts match the live ones (`scenedesc.cpp:395`), so a texture that stops being named while
every mesh and material survives — an emitter's sprite, an animated material switching frames — is
never freed. It is a real hole and it is not what any of the three issues is about; it goes in
`.notes/ISSUES.md` rather than here.

## 4. `GuiTextures`: one batch, one submit

The shape to reach: **the GUI's texture work accumulates into a batch and is submitted once, at the
moment something is about to read it.**

1. `GuiTextures` owns a `Batch` and records into it instead of calling `submitAndWait`. `add`
   records the clear and the transition; `write` records the copy.
2. `mStaging` becomes a bump arena over one persistent `HostBuffer`: `write` takes the next aligned
   run, `writeAt`s into it and records a copy from that offset; the flush resets the offset to zero.
   A run that does not fit submits what is pending and starts the buffer again, so the buffer is
   still only as large as the largest single region ever written — the same amortisation `mStaging`
   has today and the reason a video frame still allocates nothing. Sizing it to the largest *frame*
   instead would buy a few submits with a load's worth of host-visible video memory held for the
   session.
3. Flushing is the accessors' job, so it cannot be forgotten: `getView`, `getImage` and `read` flush
   first, and each is a no-op when nothing is pending. `getView` and `getImage` stop being `const`,
   which is honest — they are what makes the work happen.
4. `drop` is not one of them. A texture given back is put aside rather than destroyed and let go at
   the next flush, because a clear or a copy recorded against it has not run; flushing there instead
   would put a round trip on every window that closes, and a load closes a great many.
5. `VulkanRenderer` needs no new call: `drawGui` reaches `getView`, `traceGuiTexture` reaches
   `getImage`, `readGuiTexture` reaches `read`. Ordering inside the batch is recording order, and
   the barriers each step already carries are unchanged — the transitions around a copy are also
   what order two writes to one texture, which they had not needed to be while every write was its
   own submit.

**Measured, and it is the whole of what this was for.** From launch to standing in the Census and
Excise Office and a minute of play: 153 textures made and 154 written, which was **307 queue round
trips, and is now 15.**

That number also settles the refinement this could have had — dropping a queued clear that a
whole-surface write reaches before the flush, which is most of the textures MyGUI makes. Fifteen
submits is not where a load's time is, and the state it would take is not worth it.

Tests — extend `apps/components_tests/rtx/guitextures.cpp`, which already covers slots, regions and
the doll-versus-world separation:

- A texture created and never written reads back as fully cleared, which is the guarantee the
  batching must not lose.
- Several textures created and written before anything reads one come back holding their own pixels
  — the arena handing out distinct runs is what this is really asserting.
- A write to a slot, then a read, then another write and another read: the flush at the accessor
  puts them in order.
- A texture dropped with a write still pending destroys cleanly.

To measure when it lands: submits per interface load, and submits per frame with a video playing.

## 5. A texture is freed when it stops being named

The shape to reach: **ownership is counted where it changes, rather than recomputed by a sweep that
has to be told when to run.**

A texture is named by a material — three roles, plus a diffuse per layer — and by an emitter for as
long as the walk keeps meeting it. `SceneDesc` owns the first outright. The second is why `release`
takes a texture keep set at all: the reference lives in the extractor's map, so the scene cannot see
it and has to be handed a list of it every sweep. That is the whole of the problem, and counting is
what removes it.

1. `SceneDesc` counts references per texture slot. `addMaterial` claims what the material names,
   releasing a material gives those back, and `setMaterial` claims the new set **before** it gives
   back the old.
2. The emitter's reference becomes explicit — `holdTexture` and `dropTexture`, called by the
   extractor when it first meets an emitter and when its sweep loses one. The `textures` argument to
   `release` goes, and the duplicates that made it uncountable go with it.
3. A slot is freed the moment its count reaches zero, wherever that happens: mid-walk out of
   `setMaterial`, or in the sweep out of a material release. `SceneDesc::note` already handles a slot
   freed and taken over again inside one window, which stops being a theoretical path.
4. `release` keeps its early-out, which is what it was measured for, and no longer has a texture
   sweep to skip.
5. §3's free slot stays "empty path", and the count is what empties it. **They are not the same
   statement**, which is the one thing the design got wrong on paper: `addTexture` hands back a slot
   before anything names it, so there is a window — the rest of one `readMaterial` — where the count
   is zero and the slot is emphatically not free. The path is written the moment the slot exists and
   cleared the moment the last name goes, so it is the test a reader should ask.

**The order in step 1 is the trap.** Decrement first and an animated material rewritten with a
texture it already had frees the slot and takes it again — a slot that changed identity under every
material naming it, on a frame where nothing was supposed to move.

Tests — extend `apps/components_tests/rtx/scenedesc.cpp`, which already builds scenes and releases
them. Each of these fails today:

- An emitter's sprite is freed by the sweep that loses the emitter, while every mesh and material
  survives — which is the case the early-out returns before reaching.
- A material rewritten to another texture frees the one it stopped naming and keeps the one it still
  names.
- A texture two materials name survives one of them going, and goes with the second.
- A material rewritten with the texture it already had keeps it, at the same slot.

**Measured, and it is zero.** `bench --frames 1200` — twenty seconds of world at each of the two
places — holds at 375 textures and 6.5 MiB outside Balmora and 237 and 2.8 MiB in the Mages Guild,
which is what it held before.

That is the honest answer rather than a disappointing one, and it says what the hole actually was.
Both cases the sweep could not see need something to stop being named while its cell stays: an
emitter that ends, or a material that switches image rather than texture matrix. Balmora's animated
surfaces are `NifOsg::UVController` scrolls, which rewrite `mTextureTransform` and name the same
image throughout, and its candles burn for as long as you stand there. What the fix removes is a
class of leak the tests can now show and these two viewpoints cannot; a session long enough to
measure it is not a bench.

## 6. The borrow belongs to `GuiTextures`

The shape to reach: **the class that states the layout invariant is the one that keeps it.**

1. `GuiTextures` gains one templated call that lends a texture to a caller writing it with transfer
   commands: it flushes, as every accessor does, transitions out of the resting layout, runs what the
   caller records, and transitions back. Its destination scope is `VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT`
   with `VK_ACCESS_2_TRANSFER_WRITE_BIT` — what *a caller writing with transfer commands* means,
   rather than what one particular caller happens to record. That substitution is the fix: the scope
   stops having to agree with code somewhere else, which is the agreement that was missing.
2. It lends from `SHADER_READ_ONLY_OPTIMAL` and not from `UNDEFINED`. What discarding bought — not
   decompressing contents about to be overwritten — is a doll redrawn when the player changes clothes
   and a map tile per cell; what it cost was an invariant no one could state.
3. `traceGuiTexture` names no layout for the GUI texture and keeps only the barrier between its own
   clear and its own copy, which is its business: two transfer writes to one image, and nothing else
   orders those.
4. **Synchronization validation goes on in `Testing::buildRenderer`.** This is the part that keeps it
   fixed rather than fixes it, and it is why this section is small enough to do first: three tests
   already cover the broken path and fail on it the moment the layers are asked.

`getImage` goes with it. Everything the trace needed it for either moves inside what it records — the
extent, and so the decision to clear — or becomes `holds`, a predicate that hands nothing out. After
this, nothing outside `GuiTextures` can name a GUI texture's layout.

Tests: the three that fail are the test, once the layers are asked. Add the one case still hidden
after that — a traced view redrawn twice at the full extent, so the second borrow starts from a
texture the first left in the resting layout rather than from a fresh one.

**Confirmed, both ways.** The whole suite passes with synchronization validation on, at 6.3s against
6.0s. Narrowing the lend's scope back to `CLEAR` alone — the shape of the original bug, in its new
home — fails exactly the three traced-view tests again, so the check is live rather than merely on.

## 7. Order

§4 was first: self-contained, smallest, and its test fixture already existed.

**§6 next**, and next because of its step 4 rather than its step 1. Turning synchronization
validation on in the harness is a check every section after it runs under, and the section that
turns it on is the one that has to leave the suite green — doing it later means fixing this hazard
and whatever else has accumulated in one go.

**§5 and §3 together**, in that order. They are the same area, and §3's free slot is §5's reference
count seen from the outside: doing §3 first means writing a test for "the path is empty" that §5 then
rewrites.

**§2 last and on its own** — it is the largest, it is the only one that touches concurrency, and it
is the only one whose failure mode is a race rather than a number.

Verification for each, per `CLAUDE.md`: build the targets touched, run the covering test binary with
a filter, then `clang-format`. §2 and §4 both reach the game rather than the harness, so each ends
with `openmw-rtxtool view --frames N` under the validation layers, and §2 with a video played in the
real binary — a race does not show in a shot.
