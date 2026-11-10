# Open issues

- **`test_script_placed` fails on Windows**: "a script placed object joins the navigation mesh when an
  object is disabled" fails three runs of three on an AMD integrated GPU under Windows 11 (full flavour,
  `7b26b022b8`): the physics meets the placed `hubtemple_pillar01` at x 4272.2, and the navigation ray
  reaches x 4500 through the whole 600-frame wait. The cell grid case of the same suite passes there, and
  both pass on Linux. CI runs the integration tests on Linux alone.

- **A cached navmesh tile ignores an animated collision's pose**: a tile's key in the disk cache holds each source's
  shape and its object's transform (`MeshSource`), and not a compound shape's child transforms, which
  `RecastMeshObject::update` follows. A tile written with an animated part in one pose is read back for every pose of
  that object at that transform.
