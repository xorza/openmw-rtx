#include "look.hpp"

#include <cassert>

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStyleHints>

#include "palette.hpp"
#include "style.hpp"

namespace Graphite
{
    void apply()
    {
        assert(qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr && "a look with no application");
        // The application takes the style and deletes it with itself.
        QApplication::setStyle(new Style);
        QApplication::setPalette(qtPaletteOf(sDark));
        // A menu's ground is palantir's floating panel, `mElement`, and a combo box's list takes a menu's
        // palette and fills each row with its window colour.
        QPalette menu = qtPaletteOf(sDark);
        menu.setColor(QPalette::Window, QColor::fromRgba(sDark.mElement));
        QApplication::setPalette(menu, "QMenu");
        // In points, which Qt's logical pixels stand at 96 a inch: a font sized in pixels reports no point
        // size, and a widget that grows its font by points, as `QWizard` grows its page title, grew it from -1.
        QFont font = QApplication::font();
        font.setPointSizeF(sBodyPixels * 72.0 / 96.0);
        QApplication::setFont(font);
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }
}
