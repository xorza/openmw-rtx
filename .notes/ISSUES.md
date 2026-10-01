# Open issues

- The packages (AppImage, Windows folder, macOS bundle) install only the GPL `LICENSE`. They do not
  carry the MIT notices of what is compiled into the binary: AMD's FidelityFX (`extern/fidelityfx/LICENSE.txt`,
  in the SPIR-V of the FSR passes) and VMA.
- `components/rtxvulkan/upscale/fsrframe.cpp` ports part of the SDK's `ffx_fsr3upscaler.cpp`, but carries
  no AMD notice. The derived shaders in `components/rtxvulkan/shaders/upscale/` carry it.
