#include "filestamps.hpp"

namespace Rtx
{
    FileStamp KeptStamps::stampOf(const VFS::Path::NormalizedView path)
    {
        const std::string_view name = path.value();
        {
            const std::scoped_lock lock(mMutex);
            if (const auto kept = mKept.find(name); kept != mKept.end())
                return kept->second;
        }

        // Asked outside the lock, so the threads stamp side by side; two that ask one path at once
        // both ask it, and keep the one stamp.
        const FileStamp stamp = mSource.stampOf(path);
        const std::scoped_lock lock(mMutex);
        mKept.emplace(name, stamp);
        return stamp;
    }
}
