#include "filestamps.hpp"

namespace Rtx
{
    std::uint64_t KeptStamps::stampOf(const VFS::Path::NormalizedView path)
    {
        const std::string_view name = path.value();
        {
            const std::lock_guard lock(mMutex);
            if (const auto kept = mKept.find(name); kept != mKept.end())
                return kept->second;
        }

        // Asked outside the lock, so the threads stamp side by side; two that ask one path at once
        // both ask it, and keep the one stamp.
        const std::uint64_t stamp = mSource.stampOf(path);
        const std::lock_guard lock(mMutex);
        mKept.emplace(name, stamp);
        return stamp;
    }
}
