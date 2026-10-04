# Open issues

Open defects that give a wrong or missing result for an input the tree can produce. Most come from
the reviews of 2026-10-02.

- On huge pages, the host rows of a measured run hold still within one sitting and move between
  sittings: at `one-cell-walk` six legs read walk medians of 0.41 ms each, at 1.5 instructions a cycle
  and 6.2 to 6.9 cache misses a thousand instructions; hours later, on a build that differs by the
  harness's restart and its report line alone, twelve legs
  read 0.56 to 0.59 ms at 1.2 instructions a cycle and 12.2 to 13.3 misses, the clock at 5.0 GHz in
  both, with the tunable given by the shell and by the harness's restart alike.
