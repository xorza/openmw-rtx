# Open issues


- `visibility.rgen` writes a coverage of nought for a pixel whose ray ends in nothing under
  `mTransparentBackground`, whatever the peel composited in front of it: a see-through surface
  over an empty background in a picture inside the interface is not drawn at all.
- `lib/water.glsl` (`pixelBlur(frame.mCamera)` in the sky a water ray finds) and `tone.comp`
  (the star field drawn along `rayAt(frame.mCamera, …)` with `pixelBlur(frame.mCamera)`) use the
  world's eye for a pixel whose sky was reached through the arms' eye, past a see-through arm.
- `visibility.rgen` carries the arms' ray on past a see-through arm into the world, so where
  `first person field of view` differs from `field of view` the world behind a Chameleon or
  Invisibility arm is drawn at the arms' magnification, discontinuous with the world beside it.
- `spritecomposite.rgen` keeps `before.a` as a picture's alpha under `mTransparentBackground`, so
  a puff or an additive flame over an empty background (a torch in the inventory doll's hand)
  lays colour on a pixel whose coverage stays nought.
