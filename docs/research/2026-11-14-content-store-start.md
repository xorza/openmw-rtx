# The content store's start and the preparation's manifest — 2026-11-14

The desk: a high-end NVIDIA laptop GPU, a Samsung 980 PRO under btrfs with zstd. The store at
`~/.cache/openmw/rtx-content/v2`, as the three profiles left it: 126,370 files.

## The start's sweep

The sweep's own listing and reads, timed by a script that lists the folders as `sweepFolder` does (a
`stat` a file) and reads every file of 4152 bytes or less whole, as `Store::start` reads one to learn its
kind. "Cold" evicted each small file from the page cache first (`posix_fadvise(POSIX_FADV_DONTNEED)`).

| | listed | small files read whole |
|---|---|---|
| warm, twice | 0.15 s | 0.26 s |
| cold | 0.15 s | 1.48 s |

Of the 86,640 small files, by kind (`ContentPassId`): 81,046 shapes (88.8 MB), 3131 of kind 6, 1213 of
kind 5, 1086 of kind 4 and 164 texel means. A shape is held at any size and never loaded by the start, so
94% of the start's reads are files it drops.

## The manifest

`PreparationManifest::read` of the 11.6 MB manifest of 60,204 entries, in the release build, three reads
in one process: 38.6, 27.9 and 23.1 ms.

## Two cold stores

`./omw release scene --view=seyda-neen-ship --content-cache=<fresh folder>` twice, vanilla, on the review
branch after its second batch: 23.2 s each, 19,403 entries written by each run, no write dropped, and the two
folders the same file for file and byte for byte but for `prepared.manifest`, which keeps each run's day.
