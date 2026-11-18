# Open issues

- The handheld's Fullrest Repack tour at `c8664cd001` holds a leg's p99 at a median of 57.4 ms against the baseline's 26.2 and 26.0 (`docs/research/2026-11-18-closing-tours.md`): 64 of its 99 room stops hold a few frames of 30–40 ms in the uploader's placement with nothing arriving, the host waiting for the device while no timed zone of the device passes 8 ms, and the device holding 5.8 GB there against a budget of 4.8–5.4 GB.
- The handheld's Fullrest Repack tour refuses three 64 MiB structure blocks a flight, each with 887 MiB free in the texture pool's blocks, the largest range 26 MiB, and 2.5 GiB of resting meshes beside them; a mesh is left out.
- The handheld's PBR tour draws the stand-in for a 0.4 MiB texture with no name at Seyda Neen (leg 3, frame 559) where the device has no room for it, at `f45fb184d7`, `579612228d` and `8cdf5be569`: the last after a file the device refuses waits where what stands moves down for it (`TextureArray::makeRoom`).
- `./omw remote <host> tour --out=<dir>` cannot bring the run's folder back where `<dir>`'s parent does not exist on the desk: rsync exits 11 and the run ends with status 1 after a whole flight.
