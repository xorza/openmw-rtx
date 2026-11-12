#include "offscreen.hpp"

#include <QApplication>
#include <QByteArray>

namespace Graphite
{
    QApplication& offscreenApplication()
    {
        // Kept for the process's life, as `QApplication` keeps references to both, and the application
        // never taken apart: among the statics at exit, it would outlive Qt's own.
        static int argc = 1;
        static char name[] = "components-tests";
        static char* argv[] = { name, nullptr };
        static QApplication* const application = [] {
            qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
            return new QApplication(argc, argv);
        }();
        return *application;
    }
}
