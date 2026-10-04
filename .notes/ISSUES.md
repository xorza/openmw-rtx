# Open issues

Open defects that give a wrong or missing result for an input the tree can produce. Most come from
the reviews of 2026-10-02.

- A measured run's host rows move as a whole between runs of one build: at `one-cell-walk` six legs
  held to the performance cores read walk medians of 1.02 to 1.53 ms at a steady clock, and the
  frame thread's cache misses a thousand instructions moved with them, 3.14 to 4.75.
