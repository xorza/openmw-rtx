# What holds the host's memory under the repack — 2026-11-16

A high-end laptop CPU beside a high-end NVIDIA laptop GPU, release build at `395077cdd4`, the Fullrest Repack's profile,
`tour --cache-budget=512` so the resource caches stay as small as the handheld's, under `heaptrack` (with
`GLIBC_TUNABLES=glibc.malloc.hugetlb=1` set ahead, so the harness does not start itself again), stopped after 23 legs.

## The process

At stop 23 the process held 6,663 MiB of anonymous memory, the heap 6,668 MiB taken and 5,655 MiB in use
(`Host memory after stop`). The handheld's tour at `3ab057cd39` held 7,610–7,728 MiB at stops 13 to 16, its heap
6,444–7,468 MiB in use: on both, what the heap holds is live, not freed memory glibc keeps.

## The peak's holders

`heaptrack` put the heap's peak at 6.17 GiB. By the part of the tree that allocated it, from the 600 largest call sites
(6.01 GiB of it):

| MiB | where |
|---|---|
| 1,141 | the game's cell stores (`MWWorld::CellStore::load`, `cellstore.cpp:413`): the references of every cell loaded |
| 943 | the ray tracer's run storage (`BlockedValues::reach` under `MeshTable`): the host's copy of the scene's geometry, as far as the runs ever reached |
| 443 | the ray tracer's mirror: the template walk's records and the cell placer's placements |
| 398 | the NVIDIA driver inside `vkCreateComputePipelines` |
| 369 | the interface's vertex buffers (`MyGUIRtx` `VertexBuffer::setVertexCount`), 40,718 of them, made by `MapWindow::init`'s map entries |
| 345 | Bullet's triangle shapes (`Resource::BulletShape`) |
| 275 | the content store's resident entries (`ContentStore::Store::residentCopy`) |
| 171 | NIF keyframes |
| 254 | the navigation meshes (`RecastMesh`, Recast's allocator) |
| 157 | `NifOsg::Loader` |

The interface's buffers are allocated at `setVertexCount`, as MyGUI sizes each render item, whether or not it is ever
drawn; the OpenSceneGraph backend's (`MyGUIPlatform::OSGVertexBuffer`) allocates at `lock`, when a draw fills it.
