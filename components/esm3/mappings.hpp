#pragma once

#include <string>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>

namespace ESM
{
    BodyPart::MeshPart getMeshPart(PartReferenceType type);
    std::string getBoneName(PartReferenceType type);
    std::string getMeshFilter(PartReferenceType type);
}
