# FidelityFX SDK: the FSR 3.1.4 upscaler's GPU headers

The headers the renderer's FSR port includes, copied unchanged from AMD's FidelityFX SDK, tag
`v1.1.4` (commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`),
https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK. MIT, `LICENSE.txt`.

- `gpu/` holds `sdk/include/FidelityFX/gpu/`'s core headers, `fsr3upscaler/` and `spd/ffx_spd.h`:
  the include closure of the seven passes the renderer runs, and nothing else.
- Not here: the SDK's callbacks (`ffx_fsr3upscaler_callbacks_glsl.h`) and its pass entry files,
  because the renderer has its own, derived from them and carrying the notice:
  `components/rtxvulkan/shaders/upscale/`. The host side is a port as well,
  `components/rtxvulkan/upscale/`.

A newer FSR 3.1 is taken by replacing these files whole from a later tag, and reading the
derived files against that tag's callbacks, pass files and `ffx_fsr3upscaler.cpp`.
