#include "cellworld.hpp"

#include <string>
#include <string_view>

namespace Rtx
{
    std::string describeChange(const CellWorld& from, const CellWorld& to)
    {
        std::string moved;
        const auto add = [&](const bool changed, const std::string_view name) {
            if (!changed)
                return;
            if (!moved.empty())
                moved += ", ";
            moved += name;
        };
        add(from.mStorage != to.mStorage, "the object storage");
        add(from.mGround != to.mGround, "the ground's storage");
        add(from.mContent != to.mContent, "the content");
        add(from.mGroundcover != to.mGroundcover, "the groundcover");
        add(from.mWorldspace != to.mWorldspace, "the worldspace");
        add(from.mGrid != to.mGrid, "the grid");
        add(from.mMask != to.mMask, "the mask");
        add(from.mAbove != to.mAbove, "the state above");
        add(from.mStatics != to.mStatics, "the statics' switch");
        add(from.mStore != to.mStore, "the store");
        return moved;
    }
}
