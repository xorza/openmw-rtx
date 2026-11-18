#pragma once

#include <string_view>

#include <components/esm/refid.hpp>

namespace EsmLoader
{
    struct LessById
    {
        template <class T>
        bool operator()(const T& lhs, const T& rhs) const
        {
            return lhs.mId < rhs.mId;
        }

        template <class T>
        bool operator()(const T& lhs, const ESM::RefId& rhs) const
        {
            return lhs.mId < rhs;
        }

        template <class T>
        bool operator()(const T& lhs, std::string_view rhs) const
        {
            return lhs.mId < rhs;
        }
    };
}
