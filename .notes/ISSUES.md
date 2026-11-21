# Open issues

- The handheld's Fullrest Repack land legs hold a frame p99 of 105.7 ms at a median of 26.0 (the sampled tour, at `740f9accc2` with `SceneDesc::boundResting`), led by the game's update while it loads cells: the update's p99 stands near 85 ms in a land leg, settled or streamed alike (`docs/research/2026-11-20-tour-pacing-ring-reads-sea-tiles.md`).
- `Version::getDocumentationUrl` (`components/version/version.cpp.in`) names upstream's documentation of
  `openmw-<PROJECT_VERSION>` wherever the commit is the newest tag's, and at a fork's release tag that is an upstream
  version whose documentation upstream has not published.
