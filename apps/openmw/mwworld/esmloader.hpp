#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "contentloader.hpp"

namespace ToUTF8
{
    class Utf8Encoder;
}

namespace ESM
{
    class ReadersCache;
    struct Dialogue;
    enum class GameFormat;
}

namespace MWWorld
{

    class ESMStore;

    struct EsmLoader : public ContentLoader
    {
        explicit EsmLoader(MWWorld::ESMStore& store, ESM::ReadersCache& readers, ToUTF8::Utf8Encoder* encoder,
            std::vector<ESM::GameFormat>& gameFormats);

        std::optional<std::int32_t> getMasterFileFormat() const { return mMasterFileFormat; }

        void load(const std::filesystem::path& filepath, std::int32_t& index, Loading::Listener* listener) override;

    private:
        ESM::ReadersCache& mReaders;
        MWWorld::ESMStore& mStore;
        ToUTF8::Utf8Encoder* mEncoder;
        ESM::Dialogue* mDialogue;
        std::optional<std::int32_t> mMasterFileFormat;
        std::vector<ESM::GameFormat>& mGameFormats;
        std::map<std::string, std::int32_t> mNameToIndex;
    };

} /* namespace MWWorld */
