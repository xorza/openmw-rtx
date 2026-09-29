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

