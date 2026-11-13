# What capturing pipeline statistics costs a start — 2026-11-13

Measured on a high-end NVIDIA laptop GPU, driver 615.78.08, with the release build at `38e8f2bcf2`:
`openmw-rtxtool info` under SDL's offscreen driver, with `VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR` forced
off and on for an A/B, and the pipeline cache kept in a directory of the run's own. Every run started with the
harness's driver cache (`shaders-census-driver-cache`) emptied, so a cold run compiled every pipeline and a warm
run found only what the pipeline cache file held. The figures are the device's summary line
(`Device::logPipelineSummary`); `info` ends before the background tuples do, so each run made 99–119 pipelines.

## Cold, then warm on the cold run's file

| run | statistics | made | compiled | from the pipeline cache | compiling, summed | a compiled pipeline |
|---|---|---|---|---|---|---|
| 1 cold | off | 109 | 106 | 3 | 17.62 s | 166 ms |
| 1 warm | off | 117 | 8 | 109 | 3.00 s | — |
| 1 cold | on | 102 | 99 | 3 | 15.37 s | 155 ms |
| 1 warm | on | 111 | 9 | 102 | 2.71 s | — |
| 2 cold | off | 105 | 102 | 3 | 16.56 s | 162 ms |
| 2 warm | off | 113 | 8 | 105 | 2.33 s | — |
| 2 cold | on | 105 | 102 | 3 | 16.65 s | 163 ms |
| 2 warm | on | 115 | 10 | 105 | 3.18 s | — |
| 3 cold | off | 111 | 108 | 3 | 18.43 s | 171 ms |
| 3 warm | off | 119 | 8 | 111 | 2.95 s | — |
| 3 cold | on | 106 | 103 | 3 | 16.80 s | 163 ms |
| 3 warm | on | 115 | 9 | 106 | 3.04 s | — |

- **The pipeline cache is kept**: with statistics on, every pipeline the cold run made was found in its file by
  the warm run, as with them off. The extension allows the flag to "disable pipeline caching or otherwise affect
  pipeline creation time" (`VkPipelineCreateFlagBits`); NVIDIA's driver does neither here.
- **No compile cost shows**: 155–163 ms a compiled pipeline with statistics against 162–171 ms without, inside
  the spread of either.
- **RADV** keys a shader's cache entry on the flag (`keep_statistic_info` in `radv_shader_stage_key`,
  `radv_pipeline_get_shader_key`) and has ACO record its statistics in each compile (`record_stats`); it does not
  skip its cache for it, which only `VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR` and `RADV_DEBUG=shaders` do
  (`radv_pipeline_skip_shaders_cache`). Read in Mesa's source at `31e9a6b`; not measured, as the machine has no
  AMD card. AMD's own drivers are not measured either.
