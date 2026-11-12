#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QUiLoader>
#include <QWidget>

#include <components/files/conversion.hpp>
#include <components/graphite/look.hpp>

#include "offscreen.hpp"

namespace Graphite
{
    namespace
    {
        /// Every page the launcher and the wizard are built from.
        std::vector<std::filesystem::path> pages()
        {
            std::vector<std::filesystem::path> found;
            const std::filesystem::path root(OPENMW_PROJECT_SOURCE_DIR);
            for (const char* folder : { "apps/launcher/ui", "apps/wizard/ui" })
                for (const auto& entry : std::filesystem::directory_iterator(root / folder))
                    if (entry.path().extension() == ".ui")
                        found.push_back(entry.path());
            return found;
        }

        /// The height a widget lays out at, as its layout asks it.
        int heightOf(const QWidget& widget)
        {
            return widget.sizeHint().height();
        }

        /// **Every page of the launcher and the wizard loads in the look, and its buttons, fields, combo boxes
        /// and spin boxes all stand one row's height**: a page upstream adds is read here, so a control the
        /// look leaves at Fusion's height shows.
        TEST(GraphiteGalleryTest, everyPagesButtonsFieldsAndBoxesStandOneRowsHeight)
        {
            offscreenApplication();
            apply();
            const std::vector<std::filesystem::path> files = pages();
            ASSERT_GE(files.size(), 10u) << "the pages were not found";

            QPushButton reference(QStringLiteral("Row"));
            const int row = heightOf(reference);

            QUiLoader loader;
            for (const std::filesystem::path& path : files)
            {
                const std::string name = Files::pathToUnicodeString(path.filename());
                QFile file(QString::fromStdString(Files::pathToUnicodeString(path)));
                ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << name;
                const std::unique_ptr<QWidget> page(loader.load(&file));
                ASSERT_NE(page, nullptr) << name << ": " << loader.errorString().toStdString();
                page->ensurePolished();

                std::vector<QWidget*> controls;
                for (QWidget* control : page->findChildren<QPushButton*>())
                    controls.push_back(control);
                for (QWidget* control : page->findChildren<QComboBox*>())
                    controls.push_back(control);
                for (QWidget* control : page->findChildren<QAbstractSpinBox*>())
                    controls.push_back(control);
                for (QLineEdit* control : page->findChildren<QLineEdit*>())
                    if (qobject_cast<QAbstractSpinBox*>(control->parentWidget()) == nullptr
                        && qobject_cast<QComboBox*>(control->parentWidget()) == nullptr)
                        controls.push_back(control);
                for (const QWidget* control : controls)
                    EXPECT_EQ(heightOf(*control), row) << name << ": " << control->metaObject()->className() << " "
                                                       << control->objectName().toStdString();
            }
        }
    }
}
