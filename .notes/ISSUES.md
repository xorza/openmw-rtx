# Open issues

- **`OffscreenTrace::pick` culls the doll's subject with `PoseCull` before its update binds a new
  part.** A part `NpcAnimation::updateParts` attaches after the last `rebuildSubject` has no
  skeleton until the next redraw's update, and a pick in that gap logs `Error: RigGeometry rendering
  with no skeleton, should have been initialized by UpdateVisitor` for it.
