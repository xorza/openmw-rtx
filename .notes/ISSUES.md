# Open issues

- **The gate's suite bound fails on the Windows machine's AMD integrated GPU**: at `bde4ea6e59` every suite passes
  and `timing` counts six past 30 s there, `components.0` at 41.7 s, `components.1` at 42.0 s and the four GPU shards
  at 59.9 to 84.2 s, so the gate stops before clang-tidy, the release compile, the integration tests, `check` and
  `repeat`.
