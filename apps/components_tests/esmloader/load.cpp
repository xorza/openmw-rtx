#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esmloader/esmdata.hpp>
#include <components/esmloader/load.hpp>
#include <components/files/collections.hpp>
#include <components/files/multidircollection.hpp>
#include <components/toutf8/toutf8.hpp>

#include <gtest/gtest.h>

#ifndef OPENMW_DATA_DIR
#error "OPENMW_DATA_DIR is not defined"
#endif

namespace
{
    using namespace testing;
    using namespace EsmLoader;

    struct EsmLoaderTest : Test
    {
        const Files::PathContainer mDataDirs{ { std::filesystem::path{ OPENMW_DATA_DIR } } };
        const Files::Collections mFileCollections{ mDataDirs };
        const std::vector<std::string> mContentFiles{ { "template.omwgame" } };
    };

    TEST_F(EsmLoaderTest, loadEsmDataShouldSupportOmwgame)
    {
        Query query;
        query.mLoadCells = true;
        query.mLoadGameSettings = true;
        query.mLoadLands = true;
        query.mModels = modelRecords<ESM::Activator, ESM::Container, ESM::Door, ESM::Static>();
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.get<ESM::Activator>().size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 1);
        EXPECT_EQ(esmData.get<ESM::Container>().size(), 0);
        EXPECT_EQ(esmData.get<ESM::Door>().size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 1521);
        EXPECT_EQ(esmData.mLands.size(), 1);
        EXPECT_EQ(esmData.get<ESM::Static>().size(), 2);
    }

    TEST_F(EsmLoaderTest, shouldIgnoreCellsWhenQueryLoadCellsIsFalse)
    {
        Query query;
        query.mLoadCells = false;
        query.mLoadGameSettings = true;
        query.mLoadLands = true;
        query.mModels = modelRecords<ESM::Activator, ESM::Container, ESM::Door, ESM::Static>();
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.get<ESM::Activator>().size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 0);
        EXPECT_EQ(esmData.get<ESM::Container>().size(), 0);
        EXPECT_EQ(esmData.get<ESM::Door>().size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 1521);
        EXPECT_EQ(esmData.mLands.size(), 1);
        EXPECT_EQ(esmData.get<ESM::Static>().size(), 2);
    }

    TEST_F(EsmLoaderTest, shouldIgnoreCellsGameSettingsWhenQueryLoadGameSettingsIsFalse)
    {
        Query query;
        query.mLoadCells = true;
        query.mLoadGameSettings = false;
        query.mLoadLands = true;
        query.mModels = modelRecords<ESM::Activator, ESM::Container, ESM::Door, ESM::Static>();
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.get<ESM::Activator>().size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 1);
        EXPECT_EQ(esmData.get<ESM::Container>().size(), 0);
        EXPECT_EQ(esmData.get<ESM::Door>().size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 0);
        EXPECT_EQ(esmData.mLands.size(), 1);
        EXPECT_EQ(esmData.get<ESM::Static>().size(), 2);
    }

    /// A type the mask does not name is skipped even when the file is full of it.
    ///
    /// The gate is per type rather than all-or-nothing because what it lets through decides what the
    /// caller's world is made of: the navmesh tools ask for the four record types that carry
    /// collision, and a navmesh with the lights and the books in it is a different navmesh.
    TEST_F(EsmLoaderTest, shouldIgnoreRecordTypesTheQueryDoesNotName)
    {
        Query query;
        query.mModels = modelRecords<ESM::Activator>();
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);

        // The file holds two of these, and the test above reads them both.
        EXPECT_EQ(esmData.get<ESM::Static>().size(), 0);
        EXPECT_EQ(esmData.mRefIdTypes.size(), 0);
    }

    TEST_F(EsmLoaderTest, shouldIgnoreAllWithDefaultQuery)
    {
        const Query query;
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.get<ESM::Activator>().size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 0);
        EXPECT_EQ(esmData.get<ESM::Container>().size(), 0);
        EXPECT_EQ(esmData.get<ESM::Door>().size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 0);
        EXPECT_EQ(esmData.mLands.size(), 0);
        EXPECT_EQ(esmData.get<ESM::Static>().size(), 0);
    }

    TEST_F(EsmLoaderTest, loadEsmDataShouldSkipUnsupportedFormats)
    {
        Query query;
        query.mLoadCells = true;
        query.mLoadGameSettings = true;
        query.mLoadLands = true;
        query.mModels = modelRecords<ESM::Activator, ESM::Container, ESM::Door, ESM::Static>();
        const std::vector<std::string> contentFiles{ { "script.omwscripts" } };
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, contentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.get<ESM::Activator>().size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 0);
        EXPECT_EQ(esmData.get<ESM::Container>().size(), 0);
        EXPECT_EQ(esmData.get<ESM::Door>().size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 0);
        EXPECT_EQ(esmData.mLands.size(), 0);
        EXPECT_EQ(esmData.get<ESM::Static>().size(), 0);
    }
}
