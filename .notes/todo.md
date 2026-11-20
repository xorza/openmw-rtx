# To do

## Code

- Remove the visibility limit.

- **The seam's structure** (the user's decision, 2026-11-20; found by the renderer's review):
  - The loop clock and the frame-rate limit in `RendererSpec`, known before the window, in place of two of the calls
    that follow the renderer's making.
  - Declined, by the user's decision: the traversal root, the resources and the screenshot writer handed over at the
    renderer's making, which reorders upstream's `Engine::prepareEngine`; a post-processor accessor carrying the
    reason beside the null, which reaches its 29 upstream callers; and the frame pacer owned by the engine, which
    threads it through upstream's `WindowManager`, `LoadingScreen` and `RenderingManager` (`FramePacer` is the
    renderer's instead).

## Prompts

```text
/optimize-perf rtx renderer, make sure implementation is canonical and idiomatic, see if you find inconsistencies or issues - fix them,
try and reduce register and spill memory usage, analyze rtx shaders, denoiser, fsr. etc.  bench and profile vanilla, mfr and pbr,
make sure to test and bench on  steamdeck deck@192.168.0.31 and win 192.168.0.8, optimize kernels, registers and reduce spills there as well,
test. investigate any issues incountered for canonical and idiomatic fixes and apply them. at the end run tour for all the mw flavors on all 3 devices and analyze logs and performance
```
