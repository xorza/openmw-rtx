# What staged host writes cost on a card with resizable BAR

Measured 2026-11-10 on a high-end NVIDIA laptop GPU under Linux, driver 615.78.08, resizable BAR on,
release builds of the `staged-host-writes` branch differing in `sHostWrites` alone. `bench`, the
default suite and the `streaming` suite, after a throwaway warm-up leg, mapped and staged alternated
twice, back to back. Milliseconds a frame: median / p99 / worst.

| place | mapped, run 1 | mapped, run 2 | staged, run 1 | staged, run 2 |
|---|---|---|---|---|
| seyda-neen-ship | 6.00 / 8.04 / 8.69 | 5.99 / 7.24 / 8.01 | 6.03 / 8.18 / 11.10 | 6.01 / 8.01 / 10.35 |
| seyda-neen-ship-dawn | 6.46 / 8.35 / 8.83 | 6.48 / 7.22 / 8.66 | 6.52 / 7.94 / 8.80 | 6.54 / 8.36 / 10.40 |
| balmora-mages-guild | 5.12 / 7.28 / 8.51 | 5.28 / 7.41 / 9.10 | 5.09 / 6.54 / 7.34 | 5.14 / 6.89 / 7.29 |
| island-crossing | 4.87 / 14.65 / 20.40 | 4.92 / 14.08 / 19.37 | 5.08 / 14.88 / 22.12 | 5.02 / 15.03 / 22.62 |

The host's side of the trace, where the trace's writes are recorded (`trace` row, median):

| place | mapped | staged |
|---|---|---|
| seyda-neen-ship | 0.159, 0.133 | 0.169, 0.173 |
| island-crossing | 0.155, 0.165 | 0.209, 0.182 |

- **At the standing places the transports are one figure**: medians within 0.06 ms, with the two
  mapped runs 0.16 ms apart at the guild. The GPU's zones move by nothing above their noise: the
  refit 0.180 against 0.184 ms, the top level 0.097 against 0.098.
- **On the crossing staged writes cost 0.1 to 0.2 ms a frame at the median** (4.87 and 4.92 against
  5.08 and 5.02), and the host's side of the trace 0.02 to 0.05 ms: a crossing writes the most rows,
  and every staged one is written twice, into staging and by the copy.
- The worst frames are the present's and the arrivals' at both: no row of the report moved with the
  transport by more than the two runs of one transport moved apart.
- **Measurable, so mapped writes stay**, a constant away (`sHostWrites`), for a build on a card with
  resizable BAR that wants the 0.1 to 0.2 ms back. Staged writes ship, since they start on every card.
