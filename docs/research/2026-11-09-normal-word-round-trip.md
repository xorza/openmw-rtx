# A normal's word, decoded and packed again

Checked 2026-11-09 on the host, over every code of the normal's word (`Shaders::packNormal`, `unpackNormal`): the
present bit and two coordinates of 32767 steps each, 1,073,676,289 words.

- Decoded and packed again, 65,531 words came back as another word, and every one of those named the same direction
  as the word it came from: no word moved its direction.
- A word that a decode and a pack made packed back to itself: the round trip is the identity on what the packing
  makes.

So a shape's normals filed as words and read back hand the mesh table the direction a run hands it, once a run
hands its normals as their words read back (`ShapePass::run`), and the mesh table packs them to one word either way.
