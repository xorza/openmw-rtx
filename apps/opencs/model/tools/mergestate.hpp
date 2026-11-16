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
        std::map<std::pair<uint16_t, int>, int> mTextureIndices; // (texture, content file) -> new texture

        explicit MergeState(CSMDoc::Document& source)
            : mSource(source)
            , mCompleted(false)
        {
        }
    };
}
