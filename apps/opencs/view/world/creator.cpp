#include "creator.hpp"

#include <cstdint>
#include <stdexcept>

#include <apps/opencs/model/world/scope.hpp>
#include <apps/opencs/model/world/universalid.hpp>

namespace CSMDoc
{
    class Document;
}

void CSVWorld::Creator::setScope(std::uint32_t scope)
{
    if (scope != CSMWorld::Scope_Content)
        throw std::logic_error("Invalid scope in creator");
}

CSVWorld::Creator* CSVWorld::NullCreatorFactory::makeCreator(
    CSMDoc::Document& document, const CSMWorld::UniversalId& id) const
{
    return nullptr;
}
