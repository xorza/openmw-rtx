#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <apps/opencs/model/doc/stage.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    struct Pathgrid;
    template <typename T>
    class SubCellCollection;
}

namespace CSMTools
{
    struct Point
    {
        std::uint8_t mConnectionNum;
        std::vector<std::size_t> mOtherIndex;
        Point()
            : mConnectionNum(0)
            , mOtherIndex(0)
        {
        }
    };

    class PathgridCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& mPathgrids;
        bool mIgnoreBaseRecords;

    public:
        explicit PathgridCheckStage(const CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& pathgrids);

        std::int32_t setup() override;

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
    };
}
