# What the pinned arithmetic costs — 2026-10-22

A high-end NVIDIA laptop GPU, driver 615.78.08, release at `1f3b20786a`, 1920x1080 at the quality upscale, eight places
in the order A B B A after a throwaway leg: A the modules as the build makes them, pinned (`spirvpin.hpp`); B the same
modules unpinned, so the driver may fuse, reassociate and order the operations the pin fixes.

| leg | frame medians, summed | p99s, summed |
|---|---|---|
| A, pinned | 48.79, 49.45 ms | 56.96, 55.67 ms |
| B, unpinned | 47.02, 47.38 ms | 53.00, 54.14 ms |

**The pinning costs about 4% of the frame**: 1.85 ms over the eight places' medians, 0.23 ms a place, and about 3 ms of
their p99s. Most of it is the wavelet's first level (−10% unpinned), the landing (−8%) and the direct light (−5%), the
kernels with the most products and dot products to fuse. The price of frames that agree to the bit on every device and
from process to process.
