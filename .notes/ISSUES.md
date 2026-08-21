# Open issues

- `VulkanRenderer::setScene` keeps the `VisibilityPass` it built for the first scene, but
  `TextureArray`'s set layout declares `descriptorCount = max(textureCount, 1)`, so a later scene
  with a different number of textures produces a layout the cached pipeline layout is not compatible
  with. `RtxVisibilityTest.groundSumsItsLayersByTheWeightsItsMasksName` and
  `RtxVisibilityTest.aGlowJoinsTheLightAndAGlowingMapIsAddedPastIt` fail with
  `VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358` and `VUID-vkCmdDispatch-None-08600`; both
  pass when run alone.

- `components-tests` aborts after the last test reports, exiting 134, when the run includes the RTX
  pixel tests.

- `apps/rtxtool/main.cpp:244` — `runInfo` builds `RendererOptions` without `mShaderDirectory`, which
  warns under `-Wmissing-field-initializers`, and asks a renderer for its device description with no
  path to the shaders it would need to trace.
