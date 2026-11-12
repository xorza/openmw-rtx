# Open issues

- **An object's `ObjectTransform` reaches the navigator and nothing reads it**: `Navigator::addObject` takes it in
  `ObjectShapes` and `DoorShapes`, `CollisionShape` and `RecastMeshObject` keep it, and since the tile store keys a
  tile on its triangles (`NavMeshTileStore::keyOf`), no code reads `RecastMeshObject::getObjectTransform`.
