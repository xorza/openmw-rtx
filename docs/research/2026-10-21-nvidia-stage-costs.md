# The trace's stages on a high-end NVIDIA laptop GPU — 2026-10-21

Driver 615.78.08, Linux. Release, `bench` at 1920x1080 (1280x720 traced), 20 s a place, each side twice in the order A B
B A after a warm-up leg, over 17 places (the `default`, `exteriors`, `skies` and `interiors` suites). Counters from
Nsight Graphics' GPU Trace at `seyda-neen-ship`. A figure is read against its own table's session alone.

## The stages against the launch they replaced

The stages as first merged ran 23% longer than the one ray generation launch they replaced (the 17 medians summing to
97.6 ms against 79.2). `bounce` held 255 registers and 260–280 KB of code in the tuples with maps, kept 12 of 32 lanes
active, and a third of its warps' launches waited on registers.

The p99's spikes were another context's time slices: they landed in every pass alike, 1.2–1.3 ms each.

## The bounce's landing as a stage of its own

Split from `bounce`, which then traces and stores the landing: `bounce` 255 → 121 registers and 97 KB of code; the
frame medians' sum −2.2% and the p99s' −4.7%, 16 places of 17 faster. `landing` still kept under a third of its lanes
busy, a warp holding pixels whose bounce landed beside pixels whose bounce did not.

## The landing over the pixels that landed

`bounce` lists the pixels whose ray met a surface and `landing` runs over the list by an indirect dispatch: the frame
medians' sum −3.7% against the one kernel and the p99s' −12.3%; out of doors, where many bounces escape, −4 to −8%; in
rooms where nearly every bounce lands, +1–3%. No picture moved past the denoiser's noise.
