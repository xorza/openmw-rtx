# Open issues

- **`RigGeometry::cull` runs on a rig with no skeleton under the ray tracer.** In a release `view`
  on the PBR profile (`~/.config/openmw-pbr`), about 35 s after the player entered Samarys
  Ancestral Tomb from the Bitter Coast, the log showed once: `Error: RigGeometry rendering with no
  skeleton, should have been initialized by UpdateVisitor`. The run before it, on the same profile,
  did not enter an interior and did not show it.
