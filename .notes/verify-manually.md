# What a person has to look at

Things the unattended run cannot judge: a moving picture, a figure taken under the session's own
spinner, a choice the plan left to taste. Each item says what to run and what to look for.

## Phase 4: the shadow denoiser

- **Penumbra contrast.** The FidelityFX denoiser darkens penumbras on purpose (a contrast step). The
  port leaves it out, because it moved the penumbra 3.4% away from the converged reference. Look at
  soft shadows under trees and posts in motion (`./omw release view --view=seyda-neen-pier`) and say
  whether they read too soft.
- **Shadow lag in motion.** The temporal pass keeps up to eight frames. Walk along the pier and turn;
  look for shadow edges that trail behind a moving camera or a swaying branch.
- **Map tiles.** A local map tile is filtered by the wavelet and now by the shadow denoiser as well,
  from one frame with no history: its tree shadows are smooth where they were speckled. Open the
  local map around Seyda Neen and say whether the shadows read right.
- **Arms in the sun.** The filter rebuilds an arm's pixels through the arms' own eye, as the wavelet
  does. Look at the player's arms in full sun in first person.

## Phase 5a: the lamps in the filter

- **Lamp shadows.** A lamp's shadow edge is now filtered by the wavelet with the bounce. Look at the
  shadows candles and lanterns cast in the Mages Guild (`./omw release view --view=balmora-mages-guild`):
  are sharp contact shadows blurred more than you want?
- **Flickering torches.** The accumulator keeps up to sixteen frames of lamp light now. A torch's
  flicker may lag or smear until phase 6a's fast history lands. Look at a flickering torch.

## Phase 6a: the fast history (not built)

- **Is the lag a problem?** A lamp that goes out leaves 24% of its light eight frames later (about
  0.13 s at 60 fps). Walk past flickering torches in the Mages Guild and say whether the light
  trails. If it does, the next step is ReLAX's clamp to the fast history's spatial neighbourhood, a
  pass of its own; the per-pixel clamp was tried and made the picture noisier and darker
  (`.notes/denoise-progress.md`).

## Phase 8: FSR 3.1

Pictures of every view, taken with the release build: `~/rtx-review/off`, `~/rtx-review/native`,
`~/rtx-review/quality` (same file names).

- **Anti-aliasing and sharpness.** Compare `native` with `off`: edges should be smooth and textures
  no softer. The level bias is FSR's minus one level, so textures may read slightly sharper.
- **Motion.** Only a moving picture shows ghosting and smearing: play with `upscale = native`, then
  `quality`, and walk and turn fast past the Seyda Neen docks, water, torches, and the player's own
  arms. Look for trails behind moving things, and for water and fog that smear (FSR is given no
  reactive mask for them; the next step would be one).
- **Quality mode's guild.** At quality the Mages Guild is a little noisier than the bar (p99 42
  against 38). Say whether it reads acceptable.
- **Sharpening.** RCAS is off. Say whether native or quality want it.

## Phase 9: the glossy filter

Only under the PBR profile: the Zed tasks that start the game with `~/.config/openmw-pbr`, or
`./omw release view --view=balmora-mages-guild --replace=config --config="$HOME/.config/openmw-pbr"`.

- **Glossy speckle.** Look at polished floors, metal and wet stone under lamps: the cyan and white
  sparkles should be gone at `native` and at `off`.
- **Reflection lag when the view turns.** The history is kept while the view turns less than the
  lobe's half angle, and dropped past it (ReLAX's rule). Walk and turn past a glossy floor under a
  lamp: a highlight should not trail behind the camera. A sharp highlight may be noisier while you
  move than when you stand, which is the rule working.
- **Glossy arms and armour.** A glossy gauntlet in first person: turn quickly and look for a highlight
  that lags.

## Phase 3: AMD cards

- **On a real Radeon.** Nothing here ran on AMD hardware; the shim fakes the kernel driver and
  executes nothing. If you have access to an RX 6000, 7000 or 9000 with Mesa 26.2 or later, run
  `openmw-rtxtool info` and one `bench`. The trace kernels spill heavily on every AMD chip
  (`.notes/denoise-progress.md`, phase 3).
- **A device loss on AMD.** A debug build now writes AMD buffer markers where NVIDIA's checkpoints
  are not offered. If an AMD card ever loses the device, the fault report should name the last zone
  at the top and the bottom of the pipe.

## Phase 10: three wavelet levels

- **Where a history is lost.** The wavelet now runs three levels, not five, which saves 1.2–1.5 ms.
  Still and strafing frames measure the same, but a frame right after a door, a teleport or a
  fast turn leans on the wavelet alone. Walk through a door and turn fast in the Seyda Neen docks:
  look for a noisier first frame or two than before.
- **Map tiles.** Open the local map in a few cells: a tile is one frame the wavelet filters alone.
  The pictures moved by a few levels; say whether any looks noisier.
- **Stars.** `STAR_RADIANCE` fell from 0.45 to 0.36 (the level was matched through DLSS, which took a
  fifth off a star). Look at a clear night sky and say whether the stars read as Morrowind's.

## The whole run

- **A bench on a quiet desktop.** Every figure in `.notes/denoise-progress.md` was taken while this
  session ran. Close the editor and the terminal's animation and run
  `./omw release bench --upscale=native` twice, then once with the PBR profile, to confirm the frame
  times in "The finished frame".

## The upscaler's shake, fixed

- **Ultra performance, standing still and walking.** The frame no longer follows the jitter (the
  motion vector fault, `.notes/denoise-progress.md`). Stand still at ultra performance and at quality:
  nothing should shake. Walk: ghosting behind moving things may differ from before, because the
  history is now reprojected where it should be.
- **The PBR pier at native** measured a little noisier after the fix (5.94 against 5.09, under its bar).
  Look at glossy stone on the pier at native for sparkle.
- **`Off` in the menus.** Pick it in the settings window while playing: the picture is traced at the
  window's size, denoised, with no anti-aliasing.

## The water's speckle, fixed

- **Water under trees.** `openmw-rtxtool view --view=seyda-neen-pond`, and any pond or shore in the
  shade of leaves: what the water reflects and shows through it should be as clean as the rock or
  bank beside it, with no black-and-white grain. Walk along the shore: look for smearing of shadows
  seen through the water, which the shadow denoiser now filters.
- **Glass and faded actors** trace every occlusion ray now. Look at a window or a fading ghost in
  daylight for less grain.
