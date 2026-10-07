# Open issues

- The glossy and pane filters (`trace/denoise/specular.comp`, `trace/denoise/pane.comp`) blend
  their history with no fast mean and no clamp, so a rough reflection or a pane's light keeps up to
  `ACCUMULATE_FRAMES` of old light after a lamp changes on a still surface.
