# Open issues

- `shot --against` under the Fullrest Repack's profile and the PBR stack's reports the scene differing on every frame of every view (positions, normals, texcoords, indices, meshes, materials, deformers, poses) between two builds whose host code is the same, with every picture the same; under vanilla it reports the scene the same.
- `shot --views=all` under a configuration whose content is `Morrowind.esm` alone ends at `mournhold-arrival` with "no interior is called" and exit code 1, and the seven views listed after it are not drawn.
