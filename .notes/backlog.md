# Proposal: the backlog — vertices packed

Status: proposed, no decision taken. Each item below has its research, its options and a recommendation; every
choice that changes upstream's code, moves a picture or adds a dependency waits for the user's answer, asked one at a
time. When an item lands, its lasting decisions fold into `docs/rtx/architecture.md` and the item is deleted here.

The open issue in `.notes/ISSUES.md` (the streaming frames on the Fullrest Repack) is investigated in its own section
at the end.

## Already done

- **A NIF's own texture** (`NiPixelData`) is traced since `90d03862aa` ("Trace a texture a NIF carries inside it"),
  through the stamp `SceneUtil::EmbeddedImage` that AGENTS.md lists. The entry in `.notes/todo.txt` is stale.

## 3. Vertices packed for the device

**The problem.** A vertex's colour is three floats and its normal three floats: 22 of the 125 MB of vertex and index
bytes at `seyda-neen-ship`, read on every hit.

**Decisions.**

- **The normal in the tangent's word**, by the user's decision: one 32-bit word, the direction on Cigolle et al.'s
  octahedral map (_A Survey of Efficient Representations for Independent Unit Vectors_, JCGT 2014), each coordinate
  in fifteen bits and a bit for "present", packed and unpacked by `Shaders::packTangent`'s integer steps on both
  sides. One word format for both directions a vertex carries, and a zero normal, "use the triangle's plane", stays
  representable as the word nought. The skin pass packs a posed normal as it packs a posed tangent. Refused: two
  16-bit `SNORM` coordinates, a second format with no room for "none" and a decode `GLSLstd450UnpackSnorm2x16`
  leaves to each driver's rounding.
- **The colour in half floats, two words**, by the user's decision: red and green in one `packHalf2x16` word, blue in
  the other, unpacked with `unpackHalf2x16`, an exact conversion; every finite colour kept to eleven bits of mantissa,
  a relative error under 0.05 %, and none clamped, since a NIF's float colours may sit outside [0, 1]. Refused:
  16-bit `UNORM`, which clamps such a colour; and sRGB bytes as Unreal (`FColor`) and Unity (`Color32`) store them,
  which halve the bytes again but round a float colour to a byte and decode nine sRGB values at every hit.
- **Positions and texture coordinates stay floats.** A position is what the bottom level is built from, and Vulkan's
  mandatory acceleration-structure vertex formats (`R32G32_SFLOAT`, `R32G32B32_SFLOAT`, `R16G16_SFLOAT`,
  `R16G16B16A16_SFLOAT`, `R16G16_SNORM`, `R16G16B16A16_SNORM`,
  [Vulkan spec, required format support](https://docs.vulkan.org/spec/latest/chapters/formats.html)) would quantize a
  large mesh's positions and move every hit.
- **Packed once, where the mesh table is filled** (`MeshTable::writeVertices`), never per frame; the shaders decode at
  each hit, which the tangent shows costs less than the bytes it saves.

**Plan.** (a) the mesh table's normals as words and its colours as two words, with tests of the worst normal error
over a dense sphere and of a colour's round trip at the dark end and past one; (b) the device's blocks, the skin
pass and every reader; (c) `./omw kernels`, `shot --against`, and a `bench` A/B on the three hosts. The pictures move
by the normal's rounding: the measurement goes to the user before the item lands.

## The streaming frames on the Fullrest Repack (`.notes/ISSUES.md`)

**The issue as logged no longer holds.** At `6610d328e6`, `bench --suite=streaming` on the Fullrest Repack's profile
(a warm store, two runs) draws its worst frames at 133.6 and 54.3 ms, and every slow frame closes: the frame less its
rows is under 1 ms, where the logged frames of 396 to 451 ms left some 370 ms in no row. The work no row named left the
frame with the content store: chains, shapes and texture facts are now made on the loading screen and on workers.
The issue is deleted from the log.

**What the slow frames are now: the game's update, at each cell crossing.** Eleven of the 597 frames pass 25 ms, one
about every 48 frames, the run's crossing rate; each is the `update` row (28.9 to 128.5 ms), with the walk, the
placement and the wait under 3 ms. `./omw profile` over the same run puts the main thread's share of it in
`MWWorld::Scene::changeCellGrid` → `loadCell` → `insertCell` → `addObject`, building Bullet's triangle-mesh shapes
(`Resource::BulletShapeManager::getShape`, `TriangleMeshShape`) for references the cell preloader
(`MWWorld::PreloadItem`) had not reached, and in `World::getFocusObject`. The suite flies at 12 000 units a second,
some 40 times a run, so the preloader is outrun; it is upstream's code path, the same under either renderer.

**Nothing to change in the renderer.** A fault the rasterizer has too is not proposed (AGENTS.md). Logged as an
issue of its own, so the streaming suite's p99 is read as the game's.
