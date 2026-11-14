#pragma once

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
