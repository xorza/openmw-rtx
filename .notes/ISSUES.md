# Open issues

- `Rtx::Image`'s constructor names every image and view it makes "target", so a capture and a
  validation message both call each of them that. `GBuffer` renames its four afterwards; nothing
  else does.
