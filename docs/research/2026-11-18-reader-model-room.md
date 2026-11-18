# The cell reader's model room, trimmed at the fill — 2026-11-18

The desk's Repack, `./omw release tour --legs=24-32 --every=1`, at `396d45614b` (a prepared model's buffers trimmed as
its walk ends) against the same legs of the closing tour at `9f41093017` (trimmed as a spare is given back). The desk is
in use, so only memory is read. The legs flew the same models: the counts of lent, spare and filed models agree at every
leg.

| leg | lent models | lent room, MiB, before → after | their content, MiB | spare room, MiB, before → after |
|---|---|---|---|---|
| 24 | 960 | 59.4 → 31.7 | 25.1 | 48.2 → 56.7 |
| 25 | 1139 | 83.9 → 43.1 | 33.5 | 42.4 → 50.8 |
| 26 | 1047 | 56.2 → 35.2 | 27.6 | 58.2 → 67.4 |
| 27 | 685 | 40.0 → 28.1 | 21.8 | 82.1 → 94.2 |
| 28 | 1842 | 128.1 → 66.5 | 51.8 | 29.4 → 33.7 |
| 29 | 625 | 41.7 → 26.1 | 20.1 | 66.6 → 77.7 |
| 30 | 1340 | 83.4 → 51.2 | 39.8 | 46.8 → 54.2 |
| 31 | 696 | 44.1 → 30.2 | 23.3 | 67.8 → 79.0 |
| 32 | 1095 | 68.6 → 45.1 | 35.2 | 56.4 → 65.4 |

- **Lent**: the room held 1.26–1.30 times its content, from 1.83–2.50 times.
- **Spare**: 15–20% more. A spare now keeps the room its last model was trimmed to, where one with slack was emptied.
- **Together**: the reader's peak, at leg 28, fell from 157.6 to 100.2 MiB.
