# Open issues

- `MyGUIPlatform::OSGTexture` allocates an `osg::Image` on every `lock` and an `osg::Texture2D` on
  every `unlock`, and uploads the whole texture either way. A picture written once a frame — the
  video widget — therefore allocates twice a frame and re-sends every pixel of the frame. It does not
  implement `MyGUIPlatform::RegionTexture`, so a caller writing part of a picture writes all of it.

- The inventory doll's mirrored scene names one texture with an empty path, which resolves to
  nothing and is drawn with the grey stand-in. The world's scene names none.

- `Rtx::Image::read`'s documentation says it "leaves the image in
  `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` rather than putting it back" and that a caller wanting it
  back must transition it again. The implementation transitions it back to the layout it was handed,
  and says so in a comment beside the barrier that does it.
