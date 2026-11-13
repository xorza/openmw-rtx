#ifndef OPENMW_COMPONENTS_SCENEUTIL_SERIALIZE_H
#define OPENMW_COMPONENTS_SCENEUTIL_SERIALIZE_H

namespace SceneUtil
{

    /// Register osg node serializers for certain SceneUtil classes if not already done so. Every program that
    /// reads or writes a scene as .osgt calls it first: `Resource::SceneManager` does as it is made.
    void registerSerializers();

}

#endif
