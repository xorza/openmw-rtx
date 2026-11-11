#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/contentmodels.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWRender
{
    namespace
    {
        /// **A sheathed item's `_sh` model is prepared where the game would wear it**: a sword and a
        /// shield, each with a scabbard beside it, a shield's body part with one, and an axe with none.
        /// Each switch adds its own items' siblings and nothing of the other's; the axe's, which no
        /// file holds, is never listed.
        TEST(RtxContentModelsTest, aSheathedItemsSiblingIsPreparedWhereTheGameWouldWearIt)
        {
            TestingOpenMW::VFSTestFile file("");
            const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS({
                { VFS::Path::NormalizedView("meshes/w/sword.nif"), &file },
                { VFS::Path::NormalizedView("meshes/w/sword_sh.nif"), &file },
                { VFS::Path::NormalizedView("meshes/w/axe.nif"), &file },
                { VFS::Path::NormalizedView("meshes/a/shield.nif"), &file },
                { VFS::Path::NormalizedView("meshes/a/shield_sh.nif"), &file },
                { VFS::Path::NormalizedView("meshes/a/shield_part.nif"), &file },
                { VFS::Path::NormalizedView("meshes/a/shield_part_sh.nif"), &file },
            });
            Resource::ResourceSystem resources(vfs.get(), 5.0, nullptr);

            MWWorld::ESMStore store;
            const auto weapon = [&](std::string_view id, std::string_view model) {
                ESM::Weapon record;
                record.blank();
                record.mId = ESM::RefId::stringRefId(id);
                record.mModel = std::string(model);
                store.insertStatic(record);
            };
            weapon("sword", "w/sword.nif");
            weapon("axe", "w/axe.nif");
            ESM::Armor shield;
            shield.blank();
            shield.mId = ESM::RefId::stringRefId("shield");
            shield.mModel = std::string("a/shield.nif");
            store.insertStatic(shield);
            ESM::BodyPart part;
            part.blank();
            part.mId = ESM::RefId::stringRefId("shield_part");
            part.mModel = std::string("a/shield_part.nif");
            store.insertStatic(part);

            const auto listed = [&](bool weapons, bool shields) {
                Settings::game().mWeaponSheathing.set(weapons);
                Settings::game().mShieldSheathing.set(shields);
                std::vector<std::string> models = contentModels(store, resources);
                Settings::game().mWeaponSheathing.set(false);
                Settings::game().mShieldSheathing.set(false);
                return models;
            };

            // The sky's models, which the settings name whatever the content holds, among the rest.
            EXPECT_EQ(listed(false, false),
                (std::vector<std::string>{ "meshes/a/shield.nif", "meshes/a/shield_part.nif",
                    "meshes/sky_atmosphere.nif", "meshes/sky_clouds_01.nif", "meshes/sky_night_01.nif",
                    "meshes/w/axe.nif", "meshes/w/sword.nif" }));
            EXPECT_EQ(listed(true, false),
                (std::vector<std::string>{ "meshes/a/shield.nif", "meshes/a/shield_part.nif",
                    "meshes/sky_atmosphere.nif", "meshes/sky_clouds_01.nif", "meshes/sky_night_01.nif",
                    "meshes/w/axe.nif", "meshes/w/sword.nif", "meshes/w/sword_sh.nif" }));
            EXPECT_EQ(listed(false, true),
                (std::vector<std::string>{ "meshes/a/shield.nif", "meshes/a/shield_part.nif",
                    "meshes/a/shield_part_sh.nif", "meshes/a/shield_sh.nif", "meshes/sky_atmosphere.nif",
                    "meshes/sky_clouds_01.nif", "meshes/sky_night_01.nif", "meshes/w/axe.nif", "meshes/w/sword.nif" }));
        }
    }
}
