#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QString>
#include <QToolBar>
#include <QToolButton>
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

        /// **Every page of the launcher and the wizard loads in the look, its buttons, fields, combo boxes and
        /// spin boxes all stand one row's height, its check boxes and radios one toggle's, and a tool button that
        /// shows only its icon is a row's square**: a page upstream adds is read here, so a control the look
        /// leaves at Fusion's height shows.
        TEST(GraphiteGalleryTest, everyPagesButtonsFieldsAndBoxesStandOneRowsHeight)
        {
            offscreenApplication();
            apply();
            const std::vector<std::filesystem::path> files = pages();
            ASSERT_GE(files.size(), 10u) << "the pages were not found";

            QPushButton reference(QStringLiteral("Row"));
            const int row = heightOf(reference);
            QCheckBox toggleReference(QStringLiteral("Row"));
            const int toggleRow = heightOf(toggleReference);
            ASSERT_LT(toggleRow, row) << "a toggle as tall as a row proves nothing";

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
                // No two of a grid's items share a cell: a spacer laid in a setting's cell stretched that row and
                // stood its field in the middle of a tall gap.
                for (const QGridLayout* grid : page->findChildren<QGridLayout*>())
                {
                    std::set<std::pair<int, int>> taken;
                    for (int item = 0; item < grid->count(); ++item)
                    {
                        int top = 0;
                        int left = 0;
                        int rows = 0;
                        int columns = 0;
                        grid->getItemPosition(item, &top, &left, &rows, &columns);
                        for (int r = top; r < top + rows; ++r)
                            for (int c = left; c < left + columns; ++c)
                                EXPECT_TRUE(taken.insert({ r, c }).second)
                                    << name << ": " << grid->objectName().toStdString() << " holds two items at " << r
                                    << ", " << c;
                    }
                }

                for (const QWidget* control : controls)
                    EXPECT_EQ(heightOf(*control), row) << name << ": " << control->metaObject()->className() << " "
                                                       << control->objectName().toStdString();
                std::vector<QWidget*> toggles;
                for (QWidget* toggle : page->findChildren<QCheckBox*>())
                    toggles.push_back(toggle);
                for (QWidget* toggle : page->findChildren<QRadioButton*>())
                    toggles.push_back(toggle);
                for (const QWidget* toggle : toggles)
                    EXPECT_EQ(heightOf(*toggle), toggleRow) << name << ": " << toggle->metaObject()->className() << " "
                                                            << toggle->objectName().toStdString();
                // A tool button whose icon the page sets in code shows its text until then, which Qt lays out as
                // text; one whose icon is larger than a row stands square at the icon's size.
                for (const QToolButton* tool : page->findChildren<QToolButton*>())
                    if (tool->toolButtonStyle() == Qt::ToolButtonIconOnly && !tool->icon().isNull()
                        && qobject_cast<QToolBar*>(tool->parentWidget()) == nullptr)
                    {
                        const QSize hint = tool->sizeHint();
                        EXPECT_EQ(hint.width(), hint.height()) << name << ": " << tool->objectName().toStdString();
                        EXPECT_GE(hint.height(), row) << name << ": " << tool->objectName().toStdString();
                    }
            }
        }
    }
}
