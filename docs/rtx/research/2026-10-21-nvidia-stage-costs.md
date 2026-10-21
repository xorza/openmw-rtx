# The trace's stages on an RTX 4090 Laptop — 2026-10-21

RTX 4090 Laptop (Ada, AD103), driver 615.78.08, Linux, KWin on Wayland. Release builds,
`./omw release bench` at 1920x1080 with the default upscale (1280x720 traced), 20 s of world a
place, each side run twice in the order A B B A after a warm-up leg a side. "17 places" is the
`default`, `exteriors`, `skies` and `interiors` suites together. Zones are each one's mean share of
a frame, averaged over the places; frame times are the mean of the two runs' medians and p99s.
Counters are Nsight Graphics 2026.3.1 GPU Trace (`Top-Level Triage`, real-time shader profiler) at
`seyda-neen-ship`, one frame.

**A figure is read against its own table alone.** Each table's sides ran in one session, and the
stages with one `bounce` kernel, a side of all three, summed their 17 medians to 97.6 ms in the
first table's session and to 102.1 and 102.3 ms in the later two.

## The stages against the launch they replaced

Master at `562ad284a6` traced the frame as one ray generation launch with hit and miss shaders;
the stages as first merged, the bounce's landing visited inside `bounce`.

| | master | stages |
|---|---|---|
| sum of the 17 frame medians | 79.2 ms | 97.6 ms (+23%) |
| sum of the 17 p99s | 115.5 ms | 148.5 ms (+29%) |
| the trace's zones | `trace` 1.68 | `trace` 0.62, `direct` 0.44, `media` 0.06, `panes` 0.06, `bounce` 1.35, `composite` 0.07: 2.59 |

Every place was slower, from +7% (`vivec-canalworks`) to +31% (`balmora`) at the median. The
other passes took 0.2 ms more between them, at a core clock 2–4% lower under the same power cap.

| stage | ms | RT cores busy | lanes active of 32 | registers | stalls |
|---|---|---|---|---|---|
| `trace` | 0.65 | 74% | 28.4 | 128 | RT cores 55%, memory 23% |
| `direct` | 0.51 | 63% | 27.1 | 168 | RT cores 28%, memory 27% |
| `bounce` | 1.98 | 26% | 12.2 | 255 | RT cores 37%, memory 19%, instruction fetch 17% |

`bounce` held 255 registers and 260–280 KB of code in the tuples with maps (`sun sea maps` at
noon), and a third of its warps' launches waited on registers.

**The p99 is another context's time slices.** Over 120 frames about the worst stretch, a spike
landed in `air`, `clamp`, `shadow`, every wavelet level, `composite`, `direct` and `bounce` alike,
each adding 1.24–1.32 ms, or twice that. With `--window=0` the stages' p99 stood +24% to +102%
over master's at the four worst places, against +15% to +76% with the window. Under `nsys`, the
slowest stretch of submits ran at the usual clock with more warps in flight, the SMs' issue
halved and the DRAM's writes at 36% of peak against 8%.

## The bounce's landing as a stage of its own

`bounce` traced the bounce and stepped its landing's visit in one loop (`BounceRun` as it stood);
split, `bounce` traces and stores the landing, and `landing` resolves and visits it. The pictures
of every view, `./omw shot --views=all --map --upscale=off`, came out the same, three within the
denoiser's noise on this card.

| kernel, `sun sea maps` | registers | code | local memory |
|---|---|---|---|
| `bounce`, one kernel | 255 | 260 KB | 0 |
| `bounce`, split | 121 | 97 KB | 0 |
| `landing` | 168 | 154 KB | 80 bytes |

Over the 17 places, four runs a side in two rounds of A B B A: `bounce` and `landing` 0.28 + 0.87 =
1.15 ms against 1.38 for the one kernel; the frame medians' sum 99.9 ms against 102.1 (−2.2%), 16
places of 17 faster, and the p99s' 133.5 against 140.1 (−4.7%). The split ran at 1790–1820 MHz
against 1860–1940 for the one kernel, under the same power cap.

| stage, split | ms | RT cores busy | lanes active of 32 | stalls |
|---|---|---|---|---|
| `bounce` | 0.35 | 30% | 24.2 | RT cores 62%, memory 21% |
| `landing` | 1.25 | 15% | 9.5 | RT cores 36%, memory 21%, waiting 14% |

`landing` still keeps fewer than a third of its lanes busy: a warp holds pixels whose bounce
landed beside pixels whose bounce did not, and its lanes step the visit, the gather and the sky's
passage in one turn.

## The landing over the pixels that landed

`bounce` writes what arrived where its ray met nothing and lists the pixels whose ray met a
surface, a workgroup's run in its tile's order; `landing` runs over the list by an indirect
dispatch. The pictures of every view came out the same, two within the denoiser's noise.

| | branch | split, every pixel | split, the list |
|---|---|---|---|
| `bounce` + `landing`, mean over the 17 places | 1.37 ms | 1.15 | 1.03 |
| `landing` at `seyda-neen-shore` | — | 0.77 | 0.36 |
| `landing` at `balmora-mages-guild` | — | 1.10 | 1.15 |

Against the branch, two runs a side, A B B A: the frame medians' sum 98.5 ms against 102.3 (−3.7%),
14 places of 17 faster, and the p99s' 128.6 against 146.7 (−12.3%). Out of doors, where many
bounces escape, the medians fell 4–8%; `seyda-neen-customs`, `addamasartus` and `andrano-tomb`,
where nearly every bounce lands, rose 1–3%. The list ran at 1770–1785 MHz against 1865–1905 for
the branch, under the same power cap: its warps keep the SMs busier.

