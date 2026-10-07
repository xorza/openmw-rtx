# Open issues

- The glossy and pane filters (`trace/denoise/specular.comp`, `trace/denoise/pane.comp`) blend
  their history with no fast mean and no clamp, so a rough reflection or a pane's light keeps up to
  `ACCUMULATE_FRAMES` of old light after a lamp changes on a still surface.
- One frame of a half-opaque pane before four lamps, summed at full float, comes out 0.05–0.07%
  darker in every channel when the denoiser runs (the pane filter's first frame after a cut) than
  when the trace composes it: pane `(0.3, 0.37, 0.41)` at opacity 0.43, `darkEyeAt` and
  `addLampsBefore` of `trace/visibility/pane.cpp`, frame 2000.
- `SceneUtil::LightController` declares no clone of its own, so a `LightSource` copied with
  `osg::CopyOp::DEEP_COPY_CALLBACKS` runs a plain `osg::Callback` in the controller's place and no
  longer flickers or pulses.
- `omw kernels` refuses on a box set up by `omw bootstrap`: it wants `spirv-dis` beside the build's
  `spirv-opt`, and the SDK `deps.py` fetches keeps only `glslc`, `spirv-val` and `spirv-opt`.
