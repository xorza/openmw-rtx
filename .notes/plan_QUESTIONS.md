# Questions from working through `plan.md`

## Does the anti-firefly ring stay on? (found by part 1)

**What was measured.** After part 1 took the lanterns' glow off the bounce, `noise --ab=antifirefly`
at the guild, the planter and the yurt: one frame after a cut the ring takes 0.06 to 0.08 off the
frame's noise and adds 0.14 to 0.23 of bias, and moves the firefly count by 0.01 in a thousand;
strafed and walked in, it moves nothing by more than 0.04. The figures are in `plan.md`, Results,
part 1. Its own measurements in `look.h` (`ACCUMULATE_RING_FRAMES`) were taken on M[FR]'s tree,
where the glow it held down is now gone at its source.

**Options.**
- Keep it on: a little less noise in the first frames, more bias, and a cost the clamp pays every
  frame (`look.h`: the clamp's median 0.17 → 0.22 ms).
- Turn it off by default (`ReconstructionRequest::mAntiFirefly = false`), keeping the switch for
  content whose glows have no lamp.
- Keep it on, and measure again after step 16, which changes the variance a short history is
  filtered by, before deciding.

**Recommendation**: the third. Step 16 moves exactly the frames the ring acts on, so its trade is
worth reading once more after it; if the bias still stands past the bar then, turn it off.

**Measured again after step 16** (`plan.md`, Results, step 16), at the pier, the pond and the
guild: one frame after a cut the ring takes 0.01 to 0.06 off the noise and adds 0.06 to 0.14 of
bias, and the firefly count is the same with it and without it at every place. Strafed and walked
in, it moves nothing by more than 0.02. Its trade did not change: it buys a little noise in the
first frame, at a bias past the plan's bar of 0.05 at the guild and the pond.

**Recommendation now**: the second. On vanilla content it holds no firefly the count can see, and
its bias is past the bar. Turning it off is one default (`ReconstructionRequest::mAntiFirefly`)
and the switch stays for content whose glows have no lamp, M[FR]'s tree among them.

**What is blocked**: nothing. The ring stays on until you decide.

## Is the temporal reuse in rooms still worth its time? (found by step 18)

**What was measured** (`plan.md`, Results, step 18, and "Why the reuse lost its noise gain"). The
reuse takes no noise off anywhere now, and the cause is part 1, not the denoiser: a bisect shows
it took 0.06 to 0.15 off the rooms' noise before part 1 and nothing from part 1 on, at every later
commit alike. Its gain was on the lanterns' glow, a rare bright bounce, which part 1 found was the
lamps' light counted twice and removed. With the filter off it takes only 1 to 8% off a frame's
noise, and nothing in a young history. What it still buys is bias, in the dark rooms: the Andrano
tomb 1.54 → 1.32, Addamasartus 1.60 → 1.52, the customs house 1.60 → 1.54, and nothing measurable
in the guild, the planter, the yurt or Arkngthand. It costs 0.24 to 0.77 ms a room
(`bench --suite=interiors`, the reuse's passes).

**Options.**
- Keep `rooms` (temporal in rooms, none under the sky), the default now: the dark rooms nearer
  the converged picture, for up to 0.77 ms.
- Make `off` the default everywhere: 0.24 to 0.77 ms back in every room, a little less noise, and
  the dark rooms' bias back.
- Keep `rooms` and measure it on content with rare bright bounces that are not a lamp's own model
  (glowing plants and crystals, a replacer's emissive maps, sunlight through a window into a dark
  room), which is what resampling is for and what the bounce suite no longer holds.

**Recommendation**: the third. The bounce suite lost the one case the reuse was built for when part
1 fixed the double count, so it can no longer judge the reuse. A view with a glowing surface that
lights a room by its bounce would. Until then `rooms` stays, for the dark rooms' bias.

**What is blocked**: nothing. The default is `rooms`.
