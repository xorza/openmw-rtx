# Review of the fork's diff against upstream

> **When you address an item, delete it.** Do not mark it done. The file lists open findings only.

Scope: every production file the fork adds or changes against upstream's merge base `46bd4599`, about 150k lines in 891 files. Tests and the APIs they use are out of scope. Seven reviewers read one area each. Every high finding, and the present-target finding, was then checked against the code a second time.

Severity: **high** is a wrong value, a crash, undefined behaviour, or a check that passes while broken. **medium** is an ownership, design or duplication problem with a real cost. **low** is a simplification, dead code, a name, or a comment that breaks the guide.

## Not a finding

- The flat identity maps: no caller of `Kept` keeps an iterator or a reference into a map across an insert into the same map.
- The crash catcher's note table, shared page, packaging and hang watch hold up.
- `holdWeather`'s crossing arithmetic, `PaintedTexture`, the optimizer's merge order, and the settings menus (checked against `Rtx::sUpscaleMenu` at compile time).
- `components/rtxvulkan/vulkanrenderer.cpp:140`: only a test calls `setSea`, so the game always runs `SeaState{}`. That is an observation, not a defect.
- `components/shader/automaps.cpp:269` against `components/shader/shadervisitor.cpp:595`: the second copy is upstream's rasterizer path, and the two differ only for a mesh whose texture coordinates stand past the tangent unit and nowhere before it. One helper would change the rasterizer's choice there, which the rules forbid, or keep two rules under one name.
- `apps/openmw/mwgui/mapwindow.cpp:622`: a map view can arrive after the first ask, when a later request makes its segment, so asking until it comes is the rule. One "asked" flag would leave such a tile blank.
- `components/rtxvulkan/shaders/lib/starfield.glsl:74` and `components/rtxvulkan/shaders/trace/spriteemitters.rgen:42`–`:47`: the side a texture stands at is the device's choice when it arrives (`TextureArray::chooseSide` holds an arrival to what the room fits), so the host cannot state the star sheet's extent or an emitter texture's levels at load. The queries read the device's own answer, once a pixel with stars and once an emitter a frame.
- `components/rtx/common/slots.cpp:32`: `SlotChanges::note` compacts only where the slot stood in the other set, and `compact` does nothing otherwise, so the deferred compaction `SlotSet::remove` argues for holds for every note a frame standing still makes. The reader holds the table const and reads the lists at once, which is why the compaction is not left for it.
- `components/rtx/scene/scenedesc.cpp:274`–`:277`, `components/rtx/scene/lightgrid.cpp:92`: a stable key for the light grid's order. The sort exists for determinism, so the key must come from every light's source — the walk's path identity, the ring's reference and the effect's root — a change to three producers, to save one grid fill when a carried torch passes another lamp's x. Measure the fill first, and take it only if it shows in a frame's tail.
- `components/sdlutil/sdlinputwrapper.cpp:161`, `:241`: the SDL logging fixes belong upstream, and sending them there is the owner's action. They leave this tree when upstream has them.
- `components/rtx/preprocess/texture/texturepass.cpp`: skipping `SolidReach`'s key while the cache is empty. The key exists for the cache and costs a hash on the first meeting of a texture only; a mode switch would add a state to remove when the cache arrives. Decide it with the cache.
- `apps/openmw/mwlua/postprocessingbindings.cpp:143`: the second test of `self.mShader` is upstream's own line, and the early return above it is the fork's. Dropping upstream's test widens the diff for no behaviour.
- `apps/openmw/mwrender/rtx/skyreader.hpp:85`: `SkyReader::read` takes the weather's three records and the clock, not a `SceneFrame`. A whole frame would tie the reader to the scene graph, the terrain and the object storage it never reads, and every test of it to building them.
