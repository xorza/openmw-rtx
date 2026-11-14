#pragma once

#include <components/esm3/loadinfo.hpp>

namespace CSMWorld
{
    struct Info : public ESM::DialInfo
    {
        ESM::RefId mTopicId;
        ESM::RefId mOriginalId;
    };
}
