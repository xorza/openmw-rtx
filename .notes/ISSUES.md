# Open issues

`EsmLoader` never fills a cell's `mMovedRefs` or `mLeasedRefs` — only `MWWorld::Store` does — so
anything reading the content files through it places references that a later content file moved to
another cell or leased away.
