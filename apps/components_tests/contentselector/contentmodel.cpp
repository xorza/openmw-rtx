#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <QIcon>
#include <QString>
#include <QStringList>

#include <apps/components_tests/graphite/offscreen.hpp>
#include <components/contentselector/model/contentmodel.hpp>
#include <components/contentselector/model/esmfile.hpp>
#include <components/files/qtconversion.hpp>
#include <components/testing/util.hpp>

namespace ContentSelectorModel
{
    namespace
    {
        QStringList checkedNames(const ContentModel& model)
        {
            QStringList names;
            for (const EsmFile* file : model.checkedItems())
                names << file->fileName() + (file->isMissing() ? " missing" : "");
            return names;
        }

        /// **A content list keeps the files no data folder holds, in its order, shown as missing**: the launcher
        /// saves what is checked, and a data folder off its drive saved a load order of nothing (upstream #7300).
        /// With `kept.omwscripts` on disk, a list of it between two files that are not keeps all three checked in
        /// order, the two missing. A folder read later that holds one of them fills that entry, still checked, where
        /// it stood. An order alone, as the launcher sorts by, adds nothing.
        TEST(ContentModelTest, aContentListKeepsTheFilesNoDataFolderHoldsInItsOrder)
        {
            Graphite::offscreenApplication();
            const std::filesystem::path first = TestingOpenMW::outputDirPath("content-first");
            const std::filesystem::path later = TestingOpenMW::outputDirPath("content-later");
            std::ofstream(first / "kept.omwscripts") << "";

            QIcon warning;
            QIcon error;
            ContentModel model(nullptr, warning, error, true);
            model.addFiles(Files::pathToQString(first), false);
            model.setContentList({ "Morrowind.esm", "kept.omwscripts", "late.omwscripts" });
            EXPECT_EQ(checkedNames(model),
                (QStringList{ "Morrowind.esm missing", "kept.omwscripts", "late.omwscripts missing" }));

            std::ofstream(later / "late.omwscripts") << "";
            model.addFiles(Files::pathToQString(later), false);
            EXPECT_EQ(
                checkedNames(model), (QStringList{ "Morrowind.esm missing", "kept.omwscripts", "late.omwscripts" }));

            ContentModel ordered(nullptr, warning, error, true);
            ordered.setContentList({ "Morrowind.esm" }, true);
            EXPECT_EQ(ordered.rowCount(), 0);
        }
    }
}
