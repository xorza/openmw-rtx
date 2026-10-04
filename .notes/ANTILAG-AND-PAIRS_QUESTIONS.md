# Questions from executing `ANTILAG-AND-PAIRS.md`

Each question names the plan's item, the options with what was measured, a recommendation, and what
waits on the answer.

## Answered 2026-10-04

- **Q1, the anti-lag's cost:** kept on; the limit is 0.2 ms over the start (about 0.2 since the
  clamp left the moments alone).
- **Q2, the trail target:** 8 pixels for this plan; what is left belongs to the reuse.
- **Q3, the reuse's cost:** the limit is 1.3 ms at the guild for now, and the temporal merge is
  profiled with Nsight Compute.
