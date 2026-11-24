# What fills the texture table at ten and thirty cells of land — 2026-11-24

The debug harness at master with pull request #68 on it, `check --seconds=10` at `seyda-neen-ship`, `balmora` and
`vivec`, `--distant-cells` at 10 and at 30 (the harness's bound raised to the setting's for the run), a census of the
table's live slots at the `textures-readable` claim. A high-end NVIDIA laptop GPU.

## Live slots, the three places

| content | reach | live | files | file paths | ground composites | sprite bakes |
|---|---|---|---|---|---|---|
| vanilla | 10 | 1113 / 1348 / 1372 | 827 / 989 / 1043 | 826 / 987 / 1042 | 280 / 355 / 328 | 6 / 4 / 1 |
| vanilla | 30 | 2247 / 2649 / 2397 | 1032 / 1264 / 1198 | 1030 / 1262 / 1196 | 1209 / 1381 / 1198 | 6 / 4 / 1 |
| PBR stack | 10 | 2436 / 2936 / 3037 | 2146 / 2577 / 2708 | 2145 / 2573 / 2707 | 284 / 355 / 328 | 6 / 4 / 1 |
| PBR stack | 30 | 3869 / 4624 / 4280 | 2654 / 3239 / 3081 | 2650 / 3235 / 3077 | 1209 / 1381 / 1198 | 6 / 4 / 1 |
| Fullrest Repack | 10 | 1802 / 1833 / 2178 | 1426 / 1451 / 1742 | 1419 / 1446 / 1734 | 369 / 378 / 432 | 7 / 4 / 4 |
| Fullrest Repack | 30 | 4375 / 4730 / 4880 | 2213 / 2454 / 2458 | 2199 / 2440 / 2442 | 2155 / 2272 / 2418 | 7 / 4 / 4 |

- **A ground composite is one a land cell outside the active grid**, so their count grows with the reach's area: four
  times over from ten cells to thirty, to half the table's live slots under the Repack at thirty.
- **Files grow a fifth to a half over the same step**: the statics of the distant cells.
- **A PBR stack multiplies the files by 2.5 to 2.6**: a colour, a normal and a converted slot apiece for most paths
  (vanilla's 1032 files at `seyda-neen-ship` and thirty cells become 1034 colour, 770 normal and 850 converted).
- **No place took a ground gloss slot** on any of the three contents.
- **No slot stood twice for one path but a handful**: the wraps and encodings beside a path's first slot are
  0.1–0.7% of the files.
- **None refused**: the largest, 4880 at Vivec under the Repack, is 60% of the table's 8191. A Repack-sized land under
  a PBR stack at thirty cells — about 2440 paths at 2.6 slots and 2400 composites — comes to some 8800.
