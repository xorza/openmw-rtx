# Proposal: a blend linear in what is behind it, traced exactly

The user's decision, taken over the repack's `quicksilver_bottle.nif` (`SRC_ALPHA, ONE_MINUS_SRC_COLOR`), which the
trace draws as the nearest of its four blends (`UnreadState::BlendPair`).

- **A surface carries an emission and an RGB transmittance, each derived from its blend's two factors**: any blend
  whose destination factor reads the source alone — `ZERO`, `ONE`, the source's alpha or colour and their complements —
  is exact, a multiply (`DST_COLOR, ZERO`) among them, its source factor `DST_COLOR` taken as transmittance. As the
  order-independent transparency literature models every such blend (McGuire and Enderton 2011, "Colored Stochastic
  Shadow Maps"; McGuire and Mara 2017, "Phenomenological Transparency"). Refused: the nearest of four, which draws a
  coloured filter as grey; one more kind for the one blend met, which the next repeats.
- **A destination factor that reads the destination stays unread**: `DST_COLOR` as the destination squares what is
  behind (the repack's `ex_t_wellpod.nif`), which no layered trace makes. `DST_ALPHA` reads one, the frame's alpha.
- **Where it lands**: the material row's blend, the panes' stack and what it stores, which carry an opacity alone
  today, the additive gather's emission, and the shadow and bounce paths, which already carry an RGB through.
