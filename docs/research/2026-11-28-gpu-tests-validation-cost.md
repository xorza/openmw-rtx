# What the GPU test binary's time is spent on — 2026-11-28

Two machines, the debug flavour, the driver's pipeline cache warm, `rtx-gpu-tests` run alone in one process unless a
row says otherwise:

- a high-end NVIDIA laptop GPU beside a 24-core desktop-class laptop processor, Linux;
- a low-end AMD integrated GPU (RDNA 2) inside a 15–28 W eight-core laptop processor, Windows 11, MSVC.

## The first run after a shader change

On the AMD machine, the gate's first test run after a shader change ran its suites at 42 to 84 s. The run after it,
with nothing changed, ran them at 23 to 32 s: the first run compiled every pipeline. The suite bound now counts a
suite's fastest of up to three runs of the test step (`tools/omw/testing.py`).

## Where the time goes

| | NVIDIA, Linux | AMD, Windows |
|---|---|---|
| the binary alone, wall | 28.6 s | 72.6 s |
| its CPU time | 34.3 s | 52.9 s |
| the four shards beside the host suites, the slowest shard | 12.7 s | 31.6 s |

On the AMD machine the process spent 52.9 of the alone run's 72.6 s on the processor and the rest waiting. In a renderer
frame of the release build at 64x64, the size most pixel tests draw at, every host stage took four to five times
as long there as on the NVIDIA machine (the frame's median 6.73 ms against 0.89), while the device's zones summed to
about 3 ms.

A CPU profile of the binary on the NVIDIA machine (`perf record -g`) put 31.6% of its samples in the validation
layer, 21% in the driver, 14% in the kernel and 14% in the C library; the binary's own code was 7%.

## The validation layers' share

| `RtxVisibilityTest`, 217 tests, NVIDIA | wall |
|---|---|
| core and synchronization checks | 12.4 s |
| core checks alone | 7.2 s |
| no layers | 3.8 s |

| the whole binary, NVIDIA | wall | CPU |
|---|---|---|
| core and synchronization checks | 28.6 s | 34.3 s |
| core checks alone | 20.5 s | 17.6 s |
| no layers | 16.5 s | 13.2 s |

With no layers, 520 of the 522 tests pass. The two that fail ask for the layer itself. With core checks alone, all
522 pass.

On the AMD machine the synchronization checks weigh less: with core checks alone the binary took 66.3 s alone
against 72.6, its CPU time 45 s against 52.9. In the gate the four shards' runs went from 27.0 to 31.6 s to 24.4 to
27.4 s. Its three test runs after a shader change took 95.8, 31.6 and 27.5 s: the slowest shard's fastest run is
`rtx.gpu.0`'s 27.5 s, and from one warm run to the next that shard moved by 4 s.
