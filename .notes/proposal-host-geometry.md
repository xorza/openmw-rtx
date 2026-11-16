# Proposal: the scene's geometry stands on the device alone

The ray tracer keeps a host copy of every mesh's arrays in `MeshTable` (`BlockedValues` of positions, packed normals
and tangents, texture coordinates, colours and indices): 943 MiB on the desk's Fullrest Repack tour at a resource cache
budget of 512 MiB, the second holder of the host's memory after the game's cell stores
(`research/2026-11-16-host-memory-holders-repack.md`), and storage that never shrinks, as far as the runs ever reached.
On the handheld it is the same bytes twice in one memory: the device's geometry buffers stand in the RAM the host's
copy does. When this lands, its decisions fold into `docs/architecture.md` ("Device and memory", "One residency policy
for the content"), and this file is deleted.

## What reads the copy

Found by every reader of `MeshTable`'s arrays:

- **An arrival**: `SceneBuffers::writeMeshes`, `SceneAcceleration::writeGeometry` and `SkinTables::write` stage the
  arrived meshes' runs. Read once, in the hand-over the mesh arrives in.
- **A world built again after a change of mode**: where the frame's reserve moves and content no longer fits, or held
  something back (`VulkanRenderer::resize`, `rebuildsWorld`), the world's `DeviceScene` is let go whole and the next
  hand-over builds it from the table, every mesh's arrays staged again. The geometry is essential memory the reserve
  never touched; only the textures, which stand against the room, had reason to change.
- **The harness**: `SceneDigester` digests the arrays whole and each mesh's triangles, and `SceneReport` counts the
  meshes with tangents and the arrays' bytes.

Nothing else: a resting mesh wakes on the device with no upload, a picture's scene lives and goes with its slot
(`ViewScene`), and the renderer is made once a run.

## Decisions

- **A mesh's arrays are held only until the hand-over stages them**: the table keeps each mesh's runs, its bounds and
  its key, and the arrays of what arrived since the last hand-over, which `clearArrivals` lets go of; the device's
  buffers are the geometry. As Unity frees a mesh's system-memory copy once uploaded where nothing reads it
  ([`Mesh.UploadMeshData`](https://docs.unity3d.com/ScriptReference/Mesh.UploadMeshData.html), `markNoLongerReadable`)
  and Unreal drops a static mesh's CPU copy in a cooked build unless it is asked for
  ([`bAllowCPUAccess`](https://docs.unrealengine.com/4.27/en-US/API/Runtime/Engine/Engine/UStaticMesh/bAllowCPUAccess/)).
  Refused: the copy kept and trimmed to the live runs, which keeps the duplication on unified memory.
- **The arrivals' arrays are one flat store a stream, kept between hand-overs to a bound**: a crossing's arrivals reuse
  the capacity the last left, so a hand-over allocates nothing, and a store past the bound after a world's build is
  given back, which a crossing never reaches. Refused: a store per hand-over, an allocation per arrival; capacity kept
  whatever its size, a world's geometry held for the run.
- **A change of mode stands the world's textures again, and keeps its geometry and structures**: the world's
  `DeviceScene` stays, and lets go of and describes again only its textures against the new room, from the files as a
  rebuild reads them. Direct3D 9's managed pool kept a system copy for a device that lost its memory at a mode change
  ([`D3DPOOL`](https://learn.microsoft.com/windows/win32/direct3d9/d3dpool)); under Vulkan a change of mode makes
  the swapchain again and takes no other memory with it, and a device lost ends the renderer here, so a copy kept for
  either guards against nothing. Refused: the old scene's geometry buffers moved into a
  new one, which builds every structure again for nothing; the world walked again, a reload of the active cells for
  a change of resolution.
- **The harness digests each mesh's shape once, when it is added**: `MeshRange` keeps the digest of its triangles, the
  multiset `SceneDigester::digestTriangles` makes today, and the layout's digest reads each live mesh's runs and digest
  in slot order in place of the arrays whole. The tangent count reads `MeshRange::mTangents`, the report's bytes the
  runs' reach. Refused: reading the device's buffers back, a harness that waits on the device to digest.

## Final shape

- `MeshTable` drops `mPositions` … `mSecondTexCoords`; `RunAllocator`s keep handing out the runs, and
  `MeshArrivals` (one file) holds the arrived meshes' arrays, flat a stream, indexed by each arrival's run, with
  `clear()` giving its storage back past the bound. `getMeshPositions` and the arrays' getters read it, and assert the
  mesh arrived since the last hand-over.
- `MeshRange::mShapeDigest`, set by `MeshTable::add`; `meshKeyOf` stays the static key.
- `DeviceScene::standTexturesAgain(const SceneDesc&, std::span<const TextureData>)`, which `VulkanRenderer::resize`
  asks for in place of letting the world go; `SceneUploader` hands the whole texture table over on the hand-over after,
  as it does for a rebuild, through `SceneHolder::describeHeld` saying the textures are owed.

## Steps

1. A tour on the desk and the handheld logs the arrays' reach and the live runs at every stop beside the heap's
   arenas, so what the copy holds at the handheld's peak is measured before it goes.
2. The textures stood again in place of a world rebuilt, with its test: a change of mode that moves the reserve keeps
   every structure and geometry buffer, and stands the textures against the new room.
3. The per-mesh shape digest, and the harness's digest and report read it; `repeat --pairs=10` agrees.
4. The arrays held to their hand-over, with the allocation test of a crossing and the store's bound; tours of the three
   contents on the desk, the handheld and Windows, the host's peak against step 1's.
