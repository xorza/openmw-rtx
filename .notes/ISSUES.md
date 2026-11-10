# Open issues

- **`test_script_placed` fails on Windows**: "a script placed object joins the navigation mesh when an
  object is disabled" fails three runs of three on an AMD integrated GPU under Windows 11 (full flavour,
  `7b26b022b8`): the physics meets the placed `hubtemple_pillar01` at x 4272.2, and the navigation ray
  reaches x 4500 through the whole 600-frame wait. The cell grid case of the same suite passes there, and
  both pass on Linux. CI runs the integration tests on Linux alone.

- **`test_script_placed`'s disable case does not reach the disable on Linux**: the player's teleport back to
  the start, a same-cell move, runs `Scene::changeCellGrid`, whose `addPostponedPhysicsObjects` gives the placed
  pillar its physics and its place in the navigation mesh a frame before the other pillar is made and disabled.
  The disable's own call then finds nothing postponed, so the case passes whether or not that call works.
