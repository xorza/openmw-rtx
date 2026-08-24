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

- `Rtx::SceneDesc::release` returns before it looks at a texture whenever the live mesh and material
  counts match the table's, so a texture that stops being named while every mesh and material
  survives — an emitter whose sprite went, an animated material that switched to another image —
  keeps its slot and its uploaded image for the rest of the session.

- `VulkanRenderer::traceGuiTexture` copies the traced picture into a GUI texture without a
  dependency on the write that put the texture in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
  Synchronization validation reports `SYNC-HAZARD-WRITE-AFTER-WRITE` on `vkCmdCopyImage` for every
  traced view — the harness loads the layers without it, so the suite is green.
