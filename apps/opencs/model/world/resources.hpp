#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "universalid.hpp"

namespace VFS
{
    class Manager;
}

namespace CSMWorld
{
    class Resources
    {
        std::map<std::string, std::int32_t> mIndex;
        std::vector<std::string> mFiles;
        std::string mBaseDirectory;
        UniversalId::Type mType;

    public:
        /// \param type Type of resources in this table.
        Resources(const VFS::Manager* vfs, std::string baseDirectory, UniversalId::Type type,
            const char* const* extensions = nullptr);

        void recreate(const VFS::Manager* vfs, const char* const* extensions = nullptr);

        std::int32_t getSize() const;

        std::string getId(std::int32_t index) const;

        std::int32_t getIndex(const std::string& id) const;

        std::int32_t searchId(std::string_view id) const;

        UniversalId::Type getType() const;
    };
}
