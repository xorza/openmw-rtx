#pragma once

#include "configurationmanager.hpp"
#include "qtconversion.hpp"

#include <QString>

namespace Files
{
    inline QString getUserConfigPathQString(const Files::ConfigurationManager& cfgMgr)
    {
        return Files::pathToQString(cfgMgr.getUserConfigPath() / openmwCfgFile);
    }

    inline QStringList getActiveConfigPathsQString(const Files::ConfigurationManager& cfgMgr)
    {
        const auto& activePaths = cfgMgr.getActiveConfigPaths();
        QStringList result;
        result.reserve(static_cast<int>(activePaths.size()));
        for (const auto& path : activePaths)
            result.append(Files::pathToQString(path / openmwCfgFile));
        return result;
    }
}
