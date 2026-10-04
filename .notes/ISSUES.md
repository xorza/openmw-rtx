# Open issues

- The bounce's anti-lag clamp (`accumulateclamp.comp`) darkens a bounce whose light a ray finds
  rarely. In a GPU scene of a floor whose only bounce is a lamp-lit spot on a wall
  (`theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo` in `filter.cpp`), the settled denoised floor at
  the frame's edge stands at 36% of a 128-frame unfiltered reference with the clamp on, and at 81%
  with it off.
