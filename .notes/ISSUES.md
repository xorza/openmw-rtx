# Open issues

Open defects that give a wrong or missing result for an input the tree can produce. Most come from
the reviews of 2026-10-02.

- Upstream's groundcover places a plant in its shapes' own space: `groundcover.vert` turns, scales
  and moves the vertex before the model's transforms apply, where every other reference stands
  above them. A groundcover model whose shapes stand under a transform other than the identity
  draws its plants moved, turned and scaled by it, and the ray tracer, which stands a plant as it
  stands a static, draws them elsewhere. `apps/openmw/mwrender/groundcover.cpp` (`InstancingVisitor`).
- A measured run's host rows move as a whole between runs of one build: at `one-cell-walk` six legs
  held to the performance cores read walk medians of 1.02 to 1.53 ms at a steady clock, and the
  frame thread's cache misses a thousand instructions moved with them, 3.14 to 4.75.
