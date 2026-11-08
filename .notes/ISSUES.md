# Open issues

- A world built again in one arrival chooses its textures' levels while its bottom-level structures stand loose, and never again after compaction gives that room back: at `seyda-neen-wall-noon` on the PBR profile with `--memory-budget=6004`, the world built again on a change of upscaling mode stood 2784.3 MiB of textures, 63 of them held smaller, and kept 203.0 MiB of texture room unused once its structures stood tight at 66.5 MiB, where the same world loaded cell by cell stood 3095.1 MiB.
