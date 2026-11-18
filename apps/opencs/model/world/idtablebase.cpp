#include "idtablebase.hpp"

#include <cstdint>

CSMWorld::IdTableBase::IdTableBase(std::uint32_t features)
    : mFeatures(features)
{
}

std::uint32_t CSMWorld::IdTableBase::getFeatures() const
{
    return mFeatures;
}
