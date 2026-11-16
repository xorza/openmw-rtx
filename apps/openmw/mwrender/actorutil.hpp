#pragma once

#include <string>

#include <components/vfs/pathutil.hpp>

namespace MWRender
{
    const std::string& getActorSkeleton(bool firstPerson, bool female, bool beast, bool werewolf);
    bool isDefaultActorSkeleton(VFS::Path::NormalizedView model);
    std::string addSuffixBeforeExtension(const std::string& filename, const std::string& suffix);
}
