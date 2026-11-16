# The content store's start and the preparation's manifest — 2026-11-14

The desk (a high-end NVIDIA laptop GPU, an NVMe drive under btrfs with zstd), the store as three profiles left it:
126,370 files.

- **The start's sweep**: listing the folders took 0.15 s; reading every file of 4,152 bytes or less whole, as the start
  reads one to learn its kind, 0.26 s warm and 1.48 s cold. 81,046 of the 86,640 small files were shapes, which the
  start holds at any size and never loads, so 94% of its reads were files it dropped.
- **The manifest**: 11.6 MB, 60,204 entries, read in 23–39 ms.
- **Two cold stores of vanilla** from one build: 23.2 s each, 19,403 entries each, the folders the same byte for byte
  but for the manifest, which keeps each run's day.
