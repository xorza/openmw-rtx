#pragma once

#include <cstddef>
#include <utility>

namespace CSMWorld
{
    struct NestedTableWrapperBase
    {
        virtual ~NestedTableWrapperBase() = default;

        virtual std::size_t size() const = 0;
    };

    template <typename NestedTable>
    struct NestedTableWrapper : public NestedTableWrapperBase
    {
        NestedTable mNestedTable;

        explicit NestedTableWrapper(NestedTable nestedTable)
            : mNestedTable(std::move(nestedTable))
        {
        }

        ~NestedTableWrapper() override = default;

        std::size_t size() const override
        {
            return mNestedTable.size(); // i hope that this will be enough
        }
    };
}
