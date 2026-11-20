# Open issues

- The handheld's Fullrest Repack land legs hold a frame p99 of 105.7 ms at a median of 26.0 (the sampled tour, at `740f9accc2` with `SceneDesc::boundResting`), led by the game's update while it loads cells: the update's p99 stands near 85 ms in a land leg, settled or streamed alike (`docs/research/2026-11-20-tour-pacing-ring-reads-sea-tiles.md`).
- `openmw-rtxtool` reads the player's `settings.cfg` as a layer of defaults ahead of its own run folder
  (`apps/rtxtool/runfolder.hpp`), and `Settings::Manager::load` runs `migrateUserSettings` on the last layer alone,
  so a settings file from 0.4.5 or 0.5 holding `[RTX] upscale = off` is refused by the harness
  (`"off" is not an upscale mode`) while the game and the launcher read it.
