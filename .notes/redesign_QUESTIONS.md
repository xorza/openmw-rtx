# Questions on `redesign.md`

## 3. Section 8.1: the camera that spins

**Item.** Section 8.1, the camera that sometimes turns fast in the game.

**Why it needs a call.** The diagnostic lines are in the code at `Debug::Warning`, with TODOs to remove
them. The fix waits on a log of a spin from your game. Run the game, wait for a spin, and send
`openmw.log`'s `Mouse diagnostic:` lines from around it.

**What it blocks.** Section 8.1's fix and the removal of the diagnostic lines.
