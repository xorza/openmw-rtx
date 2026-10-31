# Open issues

- **`RigGeometry::cull` runs on a rig with no skeleton under the ray tracer.** In a release `view`
  on the PBR profile (`~/.config/openmw-pbr`), about 35 s after the player entered Samarys
  Ancestral Tomb from the Bitter Coast, the log showed once: `Error: RigGeometry rendering with no
  skeleton, should have been initialized by UpdateVisitor`. The run before it, on the same profile,
  did not enter an interior and did not show it.
- **`RtxMemoryTest.whatEachUseHoldsIsItsRangesExactly` fails in some shuffled orders.** In a gate on
  2026-10-31 and again under `--gtest_filter='RtxTextureArray*:RtxMemory*'`, it failed at
  `memory.cpp:429` ("a range given back was still counted"): `before[2]` read 51200 texture bytes
  held, and after `collectIdle` the use held 0. Alone it passes.
