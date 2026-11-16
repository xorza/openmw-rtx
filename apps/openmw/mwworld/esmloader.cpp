#include "esmloader.hpp"

#include <fstream>

#include <apps/openmw/mwbase/environment.hpp>
#include <components/esm/format.hpp>
#include <components/esm/gameformat.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm4/reader.hpp>
#include <components/files/conversion.hpp>
#include <components/files/openfile.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/resourcesystem.hpp>

#include "esmstore.hpp"

namespace MWWorld
{

    EsmLoader::EsmLoader(MWWorld::ESMStore& store, ESM::ReadersCache& readers, ToUTF8::Utf8Encoder* encoder,
        std::vector<ESM::GameFormat>& gameFormats)
        : mReaders(readers)
        , mStore(store)
        , mEncoder(encoder)
        , mDialogue(nullptr) // A content file containing INFO records without a DIAL record appends them to the
                             // previous file's dialogue
        , mGameFormats(gameFormats)
    {
    }

    void EsmLoader::load(const std::filesystem::path& filepath, int& index, Loading::Listener* listener)
    {

        auto stream = Files::openBinaryInputFileStream(filepath);
        const ESM::Format format = ESM::readFormat(*stream);
        stream->seekg(0);

        switch (format)
        {
            case ESM::Format::Tes3:
            {
                const ESM::ReadersCache::BusyItem reader = mReaders.get(static_cast<std::size_t>(index));
                reader->setEncoder(mEncoder);
                reader->setIndex(index);
                reader->open(filepath);
                reader->resolveParentFileIndices(mReaders);

                const std::vector<int>& parentIndices = reader->getParentFileIndices();
                assert(reader->getGameFiles().size() == parentIndices.size());
                for (std::size_t i = 0, n = parentIndices.size(); i < n; ++i)
                    if (parentIndices[i] == reader->getIndex())
                        throw std::runtime_error("File " + Files::pathToUnicodeString(reader->getName()) + " asks for parent file "
                + reader->getGameFiles()[i].name
                + ", but it is not available or has been loaded in the wrong order. "
                  "Please run the launcher to fix this issue.");

                mGameFormats[static_cast<std::size_t>(index)]
                    = ESM::getGameFormat(static_cast<std::uint32_t>(reader->getVer()), std::nullopt);
                mStore.load(*reader, listener, mDialogue);

                if (!mMasterFileFormat.has_value()
                    && (Misc::StringUtils::ciEndsWith(reader->getName().u8string(), u8".esm")
                        || Misc::StringUtils::ciEndsWith(reader->getName().u8string(), u8".omwgame")))
                    mMasterFileFormat = reader->getFormatVersion();
                break;
            }
            case ESM::Format::Tes4:
            {
                ESM4::Reader reader(std::move(stream), filepath,
                    MWBase::Environment::get().getResourceSystem()->getVFS(),
                    mEncoder != nullptr ? &mEncoder->getStatelessEncoder() : nullptr);
                reader.setModIndex(static_cast<std::uint32_t>(index));
                reader.updateModIndices(mNameToIndex);

                // Collect previously-loaded masters' game formats to help detect this one
                std::vector<ESM::GameFormat> masterFormats;
                for (const ESM::MasterData& master : reader.getGameFiles())
                    masterFormats.push_back(mGameFormats[static_cast<std::size_t>(
                        mNameToIndex.at(Misc::StringUtils::lowerCase(master.name)))]);

                reader.setGameFormat(ESM::getGameFormat(
                    reader.esmVersion(), reader.headerFormVersion(), reader.getGameFiles(), masterFormats));
                mGameFormats[static_cast<std::size_t>(index)] = reader.gameFormat();
                mStore.loadESM4(reader, listener);
                break;
            }
        }
        mNameToIndex[Misc::StringUtils::lowerCase(Files::pathToUnicodeString(filepath.filename()))] = index;
    }

} /* namespace MWWorld */
