# Open issues

- `apps/rtxtool/view.cpp` allocates a full-size float history image the window never reads — 33 MiB
  at 1080p and 133 MiB at 4K — because the composite's descriptor has to point somewhere.

- `RtxBridge::makeDaylight` asks for `Weather_<name>_Land_Fog_Sunrise_Depth` and the sunset
  equivalent, which no content file defines, so `openmw-rtxtool --hour` anywhere in the sunrise or
  sunset window exits with "Requested invalid float fallback" for every weather the option names.

- `apps/rtxtool/shot.cpp` tells anyone quoting a trace time to pass `--validation=false`, which does
  not turn the layers off: `--sync-validation` defaults on outside a Release build and implies them.
