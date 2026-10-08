# Open issues

- The mesh reader (`components/rtx/mirror/meshreader.cpp`) takes vertex positions, texture coordinates, colours and morph target offsets that are not finite numbers as the content wrote them. A skinned or morphed mesh with such a position stores values that are not finite in `skin.comp` or the morph kernel, and stops a played session; a static one hands the acceleration structure a vertex that is not finite.
- `MeshResolver::pose` hands bone matrices and morph weights to the device without a finiteness check, so a pose an animation makes that is not finite stores values that are not finite in the deforming kernels.
