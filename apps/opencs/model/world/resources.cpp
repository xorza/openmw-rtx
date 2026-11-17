#include "resources.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <components/misc/strings/lower.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp> // IWYU pragma: keep: the range whole, which a call returns by value

#include "universalid.hpp"

CSMWorld::Resources::Resources(
    const VFS::Manager* vfs, std::string baseDirectory, UniversalId::Type type, const char* const* extensions)
    : mBaseDirectory(std::move(baseDirectory))
    , mType(type)
{
    recreate(vfs, extensions);
}

void CSMWorld::Resources::recreate(const VFS::Manager* vfs, const char* const* extensions)
{
    mFiles.clear();
    mIndex.clear();

    std::size_t baseSize = mBaseDirectory.size();

    for (const auto& filepath : vfs->getRecursiveDirectoryIterator())
    {
        const std::string_view view = filepath.view();
        if (view.size() < baseSize + 1 || !view.starts_with(mBaseDirectory) || view[baseSize] != '/')
            continue;

        if (extensions)
        {
            const auto extensionIndex = view.find_last_of('.');

            if (extensionIndex == std::string_view::npos)
                continue;

            std::string_view extension = view.substr(extensionIndex + 1);

            std::int32_t i = 0;

            for (; extensions[i]; ++i)
                if (extensions[i] == extension)
                    break;

            if (!extensions[i])
                continue;
        }

        std::string file(view.substr(baseSize + 1));
        mFiles.push_back(file);
        mIndex.emplace(std::move(file), static_cast<std::int32_t>(mFiles.size()) - 1);
    }
}

std::int32_t CSMWorld::Resources::getSize() const
{
    return static_cast<std::int32_t>(mFiles.size());
}

std::string CSMWorld::Resources::getId(std::int32_t index) const
{
    return mFiles.at(static_cast<std::size_t>(index));
}

std::int32_t CSMWorld::Resources::getIndex(const std::string& id) const
{
    std::int32_t index = searchId(id);

    if (index == -1)
    {
        std::ostringstream stream;
        stream << "Invalid resource: " << mBaseDirectory << '/' << id;

        throw std::runtime_error(stream.str());
    }

    return index;
}

std::int32_t CSMWorld::Resources::searchId(std::string_view id) const
{
    std::string id2 = Misc::StringUtils::lowerCase(id);

    std::replace(id2.begin(), id2.end(), '\\', '/');

    auto iter = mIndex.find(id2);

    if (iter == mIndex.end())
        return -1;

    return iter->second;
}

CSMWorld::UniversalId::Type CSMWorld::Resources::getType() const
{
    return mType;
}
