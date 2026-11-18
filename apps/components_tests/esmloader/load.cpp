#include <gtest/gtest.h>

#include <components/esm3/loadacti.hpp> // IWYU pragma: keep: the record whole, which the stores' templates instantiate
#include <components/esm3/loadcell.hpp> // IWYU pragma: keep: the record whole, which the stores' templates instantiate
#include <components/esm3/loadcont.hpp> // IWYU pragma: keep: the record whole, which the stores' templates instantiate
#include <components/esm3/loaddoor.hpp> // IWYU pragma: keep: the record whole, which the stores' templates instantiate
#include <components/esm3/loadgmst.hpp> // IWYU pragma: keep: the record whole, which the stores' templates instantiate
#include <components/esm3/loadland.hpp> // IWYU pragma: keep: the record whole, which the stores' templates instantiate
#include <components/esm3/loadstat.hpp> // IWYU pragma: keep: the record whole, which the stores' templates instantiate
#include <components/esm3/readerscache.hpp>
#include <components/esmloader/esmdata.hpp>
#include <components/esmloader/load.hpp>
#include <components/files/collections.hpp>
#include <components/files/multidircollection.hpp>
#include <components/toutf8/toutf8.hpp>

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
        query.mLoadActivators = true;
        query.mLoadCells = true;
        query.mLoadContainers = true;
        query.mLoadDoors = true;
        query.mLoadGameSettings = true;
        query.mLoadLands = true;
        query.mLoadStatics = true;
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.mActivators.size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 1);
        EXPECT_EQ(esmData.mContainers.size(), 0);
        EXPECT_EQ(esmData.mDoors.size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 1521);
        EXPECT_EQ(esmData.mLands.size(), 1);
        EXPECT_EQ(esmData.mStatics.size(), 2);
    }

    TEST_F(EsmLoaderTest, shouldIgnoreCellsWhenQueryLoadCellsIsFalse)
    {
        Query query;
        query.mLoadActivators = true;
        query.mLoadCells = false;
        query.mLoadContainers = true;
        query.mLoadDoors = true;
        query.mLoadGameSettings = true;
        query.mLoadLands = true;
        query.mLoadStatics = true;
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.mActivators.size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 0);
        EXPECT_EQ(esmData.mContainers.size(), 0);
        EXPECT_EQ(esmData.mDoors.size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 1521);
        EXPECT_EQ(esmData.mLands.size(), 1);
        EXPECT_EQ(esmData.mStatics.size(), 2);
    }

    TEST_F(EsmLoaderTest, shouldIgnoreCellsGameSettingsWhenQueryLoadGameSettingsIsFalse)
    {
        Query query;
        query.mLoadActivators = true;
        query.mLoadCells = true;
        query.mLoadContainers = true;
        query.mLoadDoors = true;
        query.mLoadGameSettings = false;
        query.mLoadLands = true;
        query.mLoadStatics = true;
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.mActivators.size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 1);
        EXPECT_EQ(esmData.mContainers.size(), 0);
        EXPECT_EQ(esmData.mDoors.size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 0);
        EXPECT_EQ(esmData.mLands.size(), 1);
        EXPECT_EQ(esmData.mStatics.size(), 2);
    }

    TEST_F(EsmLoaderTest, shouldIgnoreAllWithDefaultQuery)
    {
        const Query query;
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, mContentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.mActivators.size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 0);
        EXPECT_EQ(esmData.mContainers.size(), 0);
        EXPECT_EQ(esmData.mDoors.size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 0);
        EXPECT_EQ(esmData.mLands.size(), 0);
        EXPECT_EQ(esmData.mStatics.size(), 0);
    }

    TEST_F(EsmLoaderTest, loadEsmDataShouldSkipUnsupportedFormats)
    {
        Query query;
        query.mLoadActivators = true;
        query.mLoadCells = true;
        query.mLoadContainers = true;
        query.mLoadDoors = true;
        query.mLoadGameSettings = true;
        query.mLoadLands = true;
        query.mLoadStatics = true;
        const std::vector<std::string> contentFiles{ { "script.omwscripts" } };
        ESM::ReadersCache readers;
        ToUTF8::Utf8Encoder* const encoder = nullptr;
        const EsmData esmData = loadEsmData(query, contentFiles, mFileCollections, readers, encoder);
        EXPECT_EQ(esmData.mActivators.size(), 0);
        EXPECT_EQ(esmData.mCells.size(), 0);
        EXPECT_EQ(esmData.mContainers.size(), 0);
        EXPECT_EQ(esmData.mDoors.size(), 0);
        EXPECT_EQ(esmData.mGameSettings.size(), 0);
        EXPECT_EQ(esmData.mLands.size(), 0);
        EXPECT_EQ(esmData.mStatics.size(), 0);
    }
}
