#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/variant.hpp>
#include <components/esmloader/esmdata.hpp>

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace
{
    using namespace testing;
    using namespace EsmLoader;

    struct Params
    {
        std::string mRefId;
        ESM::RecNameInts mType;
        VFS::Path::NormalizedView mResult;
        std::function<void(EsmData&)> mPushBack;
    };

    struct EsmLoaderGetModelTest : TestWithParam<Params>
    {
    };

    TEST_P(EsmLoaderGetModelTest, shouldReturnFoundModelName)
    {
        EsmData data;
        GetParam().mPushBack(data);
        EXPECT_EQ(EsmLoader::getModel(data, ESM::RefId::stringRefId(GetParam().mRefId), GetParam().mType),
            GetParam().mResult);
    }

    template <class T>
    struct PushBack
    {
        std::string mId;
        std::string mModel;

        void operator()(EsmData& esmData) const
        {
            T value;
            value.mId = ESM::RefId::stringRefId(mId);
            value.mModel = mModel;
            esmData.get<T>().push_back(std::move(value));
        }
    };

    const std::array params = {
        Params{ "acti_ref_id", ESM::REC_ACTI, VFS::Path::NormalizedView("acti_model"),
            PushBack<ESM::Activator>{ "acti_ref_id", "acti_model" } },
        Params{ "cont_ref_id", ESM::REC_CONT, VFS::Path::NormalizedView("cont_model"),
            PushBack<ESM::Container>{ "cont_ref_id", "cont_model" } },
        Params{ "door_ref_id", ESM::REC_DOOR, VFS::Path::NormalizedView("door_model"),
            PushBack<ESM::Door>{ "door_ref_id", "door_model" } },
        Params{ "static_ref_id", ESM::REC_STAT, VFS::Path::NormalizedView("static_model"),
            PushBack<ESM::Static>{ "static_ref_id", "static_model" } },
        Params{ "acti_ref_id_a", ESM::REC_ACTI, {}, PushBack<ESM::Activator>{ "acti_ref_id_z", "acti_model" } },
        Params{ "cont_ref_id_a", ESM::REC_CONT, {}, PushBack<ESM::Container>{ "cont_ref_id_z", "cont_model" } },
        Params{ "door_ref_id_a", ESM::REC_DOOR, {}, PushBack<ESM::Door>{ "door_ref_id_z", "door_model" } },
        Params{ "static_ref_id_a", ESM::REC_STAT, {}, PushBack<ESM::Static>{ "static_ref_id_z", "static_model" } },
        Params{ "acti_ref_id_z", ESM::REC_ACTI, {}, PushBack<ESM::Activator>{ "acti_ref_id_a", "acti_model" } },
        Params{ "cont_ref_id_z", ESM::REC_CONT, {}, PushBack<ESM::Container>{ "cont_ref_id_a", "cont_model" } },
        Params{ "door_ref_id_z", ESM::REC_DOOR, {}, PushBack<ESM::Door>{ "door_ref_id_a", "door_model" } },
        Params{ "static_ref_id_z", ESM::REC_STAT, {}, PushBack<ESM::Static>{ "static_ref_id_a", "static_model" } },
        // The types the widening added, which the four above cannot speak for: a lookup that only
        // knew the collision-bearing records would answer these with nothing and look right doing it.
        Params{ "light_ref_id", ESM::REC_LIGH, VFS::Path::NormalizedView("light_model"),
            PushBack<ESM::Light>{ "light_ref_id", "light_model" } },
        Params{ "book_ref_id", ESM::REC_BOOK, VFS::Path::NormalizedView("book_model"),
            PushBack<ESM::Book>{ "book_ref_id", "book_model" } },
        Params{ "misc_ref_id", ESM::REC_MISC, VFS::Path::NormalizedView("misc_model"),
            PushBack<ESM::Miscellaneous>{ "misc_ref_id", "misc_model" } },
        // The right id in the wrong table stays unfound: the type picks the table before the id is
        // looked for, so a light and a book sharing a name are still two different records.
        Params{ "light_ref_id", ESM::REC_BOOK, {}, PushBack<ESM::Light>{ "light_ref_id", "light_model" } },
        Params{ "ref_id", ESM::REC_STAT, {}, [](EsmData&) {} },
        // A record type the list deliberately leaves out. An NPC names no model of its own.
        Params{ "ref_id", ESM::REC_NPC_, {}, [](EsmData&) {} },
    };

    INSTANTIATE_TEST_SUITE_P(Params, EsmLoaderGetModelTest, ValuesIn(params));

    TEST(EsmLoaderGetGameSettingTest, shouldReturnFoundValue)
    {
        std::vector<ESM::GameSetting> settings;
        ESM::GameSetting setting;
        setting.mId = ESM::RefId::stringRefId("setting");
        setting.mValue = ESM::Variant(42);
        setting.mRecordFlags = 0;
        settings.push_back(setting);
        EXPECT_EQ(EsmLoader::getGameSetting(settings, "setting"), ESM::Variant(42));
    }

    TEST(EsmLoaderGetGameSettingTest, shouldThrowExceptionWhenNotFound)
    {
        const std::vector<ESM::GameSetting> settings;
        EXPECT_THROW(EsmLoader::getGameSetting(settings, "setting"), std::runtime_error);
    }
}
