#pragma once

#include <any>
#include <cstdint>

#include "ptr.hpp"

namespace MWWorld
{
    /// \brief Manually constructed live cell ref. The resulting Ptr shares its lifetime with this ManualRef and must
    /// not be used past its end.
    class ManualRef
    {
        // Stores the ref (LiveCellRef<T>) by value.
        std::any mRef;
        Ptr mPtr;

    public:
        explicit ManualRef(const MWWorld::ESMStore& store, const ESM::RefId& name, const std::int32_t count = 1);
        explicit ManualRef(
            const MWWorld::ESMStore& store, const MWWorld::Ptr& templatePtr, const std::int32_t count = 1);

        ManualRef(const ManualRef&) = delete;
        ManualRef& operator=(const ManualRef&) = delete;

        ManualRef(ManualRef&&) noexcept = default;
        ManualRef& operator=(ManualRef&&) noexcept = default;
        ~ManualRef() = default;

        const Ptr& getPtr() const { return mPtr; }
    };
}
