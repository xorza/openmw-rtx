# The host-written window at the world's room — 2026-10-22

A high-end NVIDIA laptop GPU, driver 615.78.08, Linux. Every buffer made host-written (`DEVICE_LOCAL | HOST_VISIBLE`)
logged as it was made and buried, over three places, the island crossing and the inventory's doll.

An RTX 20 card has no resizable BAR: its host-written window is a 256 MiB heap, of which the Linux driver's budget
offers about 246 MiB. At the world's room the per-frame tables stood 121–123 MiB live in 184 MiB of blocks, the instance
and top-level rows 112 MiB of it (a copy a frame slot, the world's and the doll's), so the window held them with no room
for a growth past the room.

Superseded: NVIDIA's Windows driver gives that window at a budget of nought without resizable BAR
(`2026-11-09-host-written-window-on-windows.md`), so host writes are now staged on every card
(`2026-11-10-host-writes-on-three-machines.md`).
