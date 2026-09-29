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

