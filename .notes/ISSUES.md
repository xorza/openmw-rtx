# Open issues

- `MyGUIPlatform::OSGTexture` allocates an `osg::Image` on every `lock` and an `osg::Texture2D` on
  every `unlock`, and uploads the whole texture either way. A picture written once a frame — the
  video widget — therefore allocates twice a frame and re-sends every pixel of the frame. It does not
  implement `MyGUIPlatform::RegionTexture`, so a caller writing part of a picture writes all of it.

- The inventory doll's mirrored scene names one texture with an empty path, which resolves to
  nothing and is drawn with the grey stand-in. The world's scene names none.

- `Rtx::GuiTextures::add` clears each new texture through a submit it then waits on, so every texture
  the interface creates — one per picture widget, per font atlas, per traced view — costs a queue
  round trip.
