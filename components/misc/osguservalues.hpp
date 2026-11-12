#ifndef OPENMW_COMPONENTS_MISC_OSGUSERVALUES_H
#define OPENMW_COMPONENTS_MISC_OSGUSERVALUES_H

#include <string>

namespace Misc
{
    struct OsgUserValues
    {
        static const std::string sFileHash;
        /// The VFS path a model was read from, on its root (`Resource::SceneManager::getTemplate`).
        static const std::string sFilePath;
        static const std::string sXSoftEffect;
    };
}

#endif
