# Open issues

- A lamp's own model that glows lights the room twice: its `LIGH` lamp lights every surface through
  the model's fitting (`lampPassage`), and a bounce that lands on the model's glowing surface — the
  paper of a lantern, emissive 1 under `EMISSIVE_INTENSITY` — carries the glow back as well
  (`bounceArriving`). At the Balmora mages' guild under M[FR], the glow the bounce takes from the
  emissive colours there, three lanterns' paper among them, is 9.6% of the converged frame by the
  tree, and it was the tree's fireflies.
- `EMISSIVE_INTENSITY`'s comment in `look.h` says a glow lights nothing, and `bounceArriving` counts
  a glowing surface's light in every bounce that lands on it.
