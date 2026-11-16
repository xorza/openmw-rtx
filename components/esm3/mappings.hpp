#pragma once

#include <string>

#include "loadarmo.hpp"
#include "loadbody.hpp"

namespace ESM
{
    BodyPart::MeshPart getMeshPart(PartReferenceType type);
    std::string getBoneName(PartReferenceType type);
    std::string getMeshFilter(PartReferenceType type);
}
