# Staged against mapped host writes on three machines — 2026-11-10

Release builds differing in the host-write transport alone, `bench --window=false`, the default and `streaming` suites,
1200 frames a place, mapped and staged alternated twice, back to back:

- **a high-end NVIDIA laptop GPU** under Linux, resizable BAR on, 1920x1080 traced at 1280x720;
- **an AMD handheld** (RDNA 2) under RADV, a device-local heap the host maps whole, its clock held, 1280x800;
- **an AMD laptop's integrated RDNA 2 graphics** under Windows, a 256 MiB host window, 1280x800.

## What they say

- **On the NVIDIA card with resizable BAR, mapped writes are 0.04 to 0.10 ms a frame faster** at the median, 1–2% of a
  frame: staged writes cost the host less, since the staging is cached and the window write-combined, and the device
  more, the frame waiting 0.04–0.2 ms longer on the copy (`2026-11-10-staged-host-writes-cost.md`).
- **On the AMD handheld the two are one figure**: medians within 0.2 ms of 53 to 98, the crossing's order flipping
  between rounds; only the guild's p99 held 5 ms worse staged in both rounds, its median and worst unmoved.
- **On the AMD integrated graphics under Windows the two are one figure too**, inside what two runs of one transport
  part by. Its 256 MiB window serves mapped writes, as NVIDIA's Windows driver without resizable BAR did not
  (`2026-11-09-host-written-window-on-windows.md`).

So writes are staged on every card: the one card where mapped writes win is one with resizable BAR, by about 0.1 ms,
and neither AMD part prefers either.
