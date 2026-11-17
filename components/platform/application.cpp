#include "application.hpp"

#include <cstdint>

#include <components/debug/debuglog.hpp>
#include <components/graphite/look.hpp>
#include <components/misc/scalableicon.hpp>

namespace Platform
{
    namespace
    {
        void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
        {
            const Debug::Level level = [&] {
                switch (type)
                {
                    case QtDebugMsg:
                        return Debug::Debug;
                    case QtInfoMsg:
                        return Debug::Info;
                    case QtWarningMsg:
                        return Debug::Warning;
                    case QtCriticalMsg:
                    case QtFatalMsg:
                        return Debug::Error;
                    default:
                        return Debug::Debug;
                }
            }();

            Log(level) << qUtf8Printable(qFormatLogMessage(type, context, msg));
        }
    }

    Application::Application(std::int32_t& argc, char* argv[], const Look look)
        : QApplication(argc, argv)
    {
        qInstallMessageHandler(qtMessageHandler);
        if (look == Look::Graphite)
        {
            Graphite::apply();
            return;
        }
        followSystemLook();
    }

    bool Application::notify(QObject* receiver, QEvent* event)
    {
        try
        {
            if (event->type() == QEvent::ThemeChange || event->type() == QEvent::PaletteChange)
            {
                if (darkModeTurned())
                {
                    const bool result = QApplication::notify(receiver, event);
                    emit darkModeChanged(mDarkMode);
                    return result;
                }
                Misc::ScalableIcon::updateAllIcons();
            }

            return QApplication::notify(receiver, event);
        }
        catch (const std::exception& exception)
        {
            Log(Debug::Error) << "An exception has been caught: " << exception.what();
        }

        return false;
    }
}
