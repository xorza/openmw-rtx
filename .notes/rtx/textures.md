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

## 5. Order

§4 first: it is self-contained, it is the smallest, and it is the one whose test fixture already
exists. §3 second, because the trap in step 2 wants to be fixed while the reasoning is fresh and
nothing else depends on it. §2 last and on its own — it is the largest, it is the only one that
touches concurrency, and it is the only one whose failure mode is a race rather than a number.

Verification for each, per `CLAUDE.md`: build the targets touched, run the covering test binary with
a filter, then `clang-format`. §2 and §4 both reach the game rather than the harness, so each ends
with `openmw-rtxtool view --frames N` under the validation layers, and §2 with a video played in the
real binary — a race does not show in a shot.
