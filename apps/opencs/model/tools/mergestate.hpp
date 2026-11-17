#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include <apps/opencs/model/doc/document.hpp>

namespace CSMTools
{
    struct MergeState
    {
        std::unique_ptr<CSMDoc::Document> mTarget;
        CSMDoc::Document& mSource;
        bool mCompleted;
        std::map<std::pair<std::uint16_t, std::int32_t>, std::int32_t>
            mTextureIndices; // (texture, content file) -> new texture

        explicit MergeState(CSMDoc::Document& source)
            : mSource(source)
            , mCompleted(false)
        {
        }
    };
}
