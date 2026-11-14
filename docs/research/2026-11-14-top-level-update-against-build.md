# The top level updated in place against built whole, held still for five minutes: 2026-11-14

`balmora-mages-guild` (people walking), release build, a high-end NVIDIA laptop GPU, one 300-second leg each
after a ten-second warm-up: the shipped rule (updated wherever only places moved) against a build of the whole
every frame. Zones by `bench --json`, median / p99 in ms; the host's wait on the device from `--frame-times`,
the median over the run's first fifth and its last.

| | `tlas` | `trace` | `direct` | `bounce` | `landing` | wait, first fifth | wait, last fifth |
|---|---|---|---|---|---|---|---|
| updated | 0.032 / 0.039 | 0.363 / 0.65 | 0.509 / 1.36 | 0.363 / 0.40 | 0.513 / 1.37 | 4.137 | 4.371 |
| built | 0.203 / 0.229 | 0.377 / 1.20 | 0.492 / 0.55 | 0.376 / 1.21 | 0.509 / 0.56 | 4.409 | 4.416 |

The decay is real, 0.23 ms over five minutes, and the updated structure still cost less at the run's end than
a build every frame.
