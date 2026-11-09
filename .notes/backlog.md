# Proposal: the backlog — files written whole, vertices packed

Status: proposed, no decision taken. Each item below has its research, its options and a recommendation; every
choice that changes upstream's code, moves a picture or adds a dependency waits for the user's answer, asked one at a
time. When an item lands, its lasting decisions fold into `docs/rtx/architecture.md` and the item is deleted here.

The open issue in `.notes/ISSUES.md` (the streaming frames on the Fullrest Repack) is investigated in its own section
at the end.

## Already done

- **A NIF's own texture** (`NiPixelData`) is traced since `90d03862aa` ("Trace a texture a NIF carries inside it"),
  through the stamp `SceneUtil::EmbeddedImage` that AGENTS.md lists. The entry in `.notes/todo.txt` is stale.

## 2. Files written whole

**The problem.** Six writers in the fork write a temporary file and rename it, each with its own code
(`ContentStore`, `PreparationManifest`, `PipelineCache`, `crashpackage.cpp`, `spirvpintool.cpp`, `tablesource.cpp`),
and the rest write in place: upstream's `settings.cfg` (`SettingsFileParser::saveSettingsFile`) and the save games
(`StateManager::saveGame`), the harness's records and pictures, and the pipeline code text. A crash or a full disk
during an in-place write leaves a truncated settings file or save.

**Established answer.** Write a temporary file in the target's folder, flush it, and rename it over the target; on
POSIX `rename` is atomic, and a sync of the file before and the folder after makes it durable; on Windows
`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH` is what the `tempfile` crate and Python's
`atomicwrites` use, and MSVC's `std::filesystem::rename` replaces the target. A sharing violation on Windows (an
antivirus or an indexer holding the target) is retried.
Sources: [Atomic file writes on Windows](https://antonymale.co.uk/windows-atomic-file-writes.html),
[atomicwrites](https://pypi.org/project/atomicwrites/),
[PostgreSQL's pgrename on Windows](https://www.postgresql.org/message-id/CAPpHfduNx3F2vx-0HV0nDp4OmN8d5mEfQ0qd3EDhGb9WyBdE9w%40mail.gmail.com).

**Options.**

| option | what                                                                                                                                                                    | cost                                                                                                                                        |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| A      | one helper, `Files::writeWhole(path, bytes, Durability)`, behind `components/platform` for the sync and the replace; the six writers call it; the harness's writers too | the fork's code only; `Durability::Synced` for a store's index and the manifest, `Flushed` for a cache entry whose digest finds a torn body |
| B      | A, and upstream's settings and saves through it                                                                                                                         | two upstream changes, which AGENTS.md lets in only as approved fixes                                                                        |

**Recommendation.** B: a lost `settings.cfg` or save is a fault the player meets, and the approval rule is the
user's to apply. The helper retries a sharing violation a bounded number of times and then fails as an I/O error.

**Plan.** (a) `components/platform/writewhole{posix,win32}.cpp` with one header; (b) the six writers moved onto it and
their own renames deleted; (c) the harness's writers; (d) with approval, `saveSettingsFile` and `saveGame`; tests: a
write over an existing file replaces it whole, a failed write (a folder that is not there) leaves the old file and no
temporary, and a stale temporary from a killed run is not read.

## 3. Vertices packed for the device

**The problem.** A vertex's colour is three floats and its normal three floats: 22 of the 125 MB of vertex and index
bytes at `seyda-neen-ship`, read on every hit.

**Established answer.** A unit normal in two 16-bit octahedral coordinates loses at most 0.0035° (Cigolle et al.,
_A Survey of Efficient Representations for Independent Unit Vectors_, JCGT 2014), the form the tangent here already
takes. A colour in linear light needs more than 8 bits in its dark end; 16-bit UNORM a channel holds it. A position
stays a float: it is what the bottom level is built from, and Vulkan's mandatory acceleration-structure vertex formats
(`R32G32_SFLOAT`, `R32G32B32_SFLOAT`, `R16G16_SFLOAT`, `R16G16B16A16_SFLOAT`, `R16G16_SNORM`, `R16G16B16A16_SNORM`,
[Vulkan spec, required format support](https://docs.vulkan.org/spec/latest/chapters/formats.html)) would quantize a
large mesh's positions and move every hit.

**Recommendation.** The normal as one 32-bit octahedral word (`R16G16_SNORM`), the colour as `R16G16B16A16_UNORM`
less its alpha where the format allows three channels, or four with the alpha the mesh already carries; positions and
texture coordinates unchanged. Packed once where the mesh is read (`MeshResolver`), never per frame. Cost: a decode in
every shader that reads them, which the tangent's shows is cheaper than the bytes.

**Plan.** (a) `packNormal`/`unpackNormal` beside `packTangent`, in the shared header, with a test of the worst error
over a dense sphere against the 0.0035° bound; (b) the colour's packing and its test at the dark end; (c) the blocks'
formats and their readers; (d) `./omw kernels`, `shot --against`, and a `bench` A/B on the three hosts. The pictures
move by the normal's rounding: a decision for the user on the measurement.

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
