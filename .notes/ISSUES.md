# Open issues

- `Scene::mLowestPoint` follows no collision object moved or removed after its cell loaded, so a script that lowers a
  platform past that point can trigger the void reset in `Scene::playerMoved` for a player who stands on it.
