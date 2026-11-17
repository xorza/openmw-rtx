#include "application.hpp"

#include <QFile>
#include <QOperatingSystemVersion>
#include <QStyle>
#include <QStyleHints>

namespace Platform
{
    namespace
    {
        QString getStyleSheetPath()
        {
            QString qssPath(":/dark/dark_win10.qss");
            if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11)
                qssPath = ":/dark/dark_win11.qss";

            return qssPath;
        }

        bool systemIsDark()
        {
            return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        }
    }

    void Application::followSystemLook()
    {
        connect(this, &Application::darkModeChanged, this, &Application::updateStyle);

        const auto currentStyle = QApplication::style()->objectName();
        mInitialStyle = currentStyle.toStdString();
        mCurrentStyle = currentStyle.toStdString();
        if (systemIsDark())
        {
            mDarkMode = true;
            if (currentStyle == "windowsvista")
            {
                mCurrentStyle = "windows";
                setStyle("windows");

                QFile file(getStyleSheetPath());
                if (!file.open(QIODevice::ReadOnly))
                {
                    qDebug() << "Failed to open style sheet file:" << getStyleSheetPath();
                    return;
                }

                setStyleSheet(file.readAll());
            }
        }
    }

    void Application::updateStyle(bool isDark)
    {
        if (mInitialStyle != "windowsvista")
            return;

        if (isDark)
        {
            mCurrentStyle = "windows";
            setStyle("windows");

            QFile file(getStyleSheetPath());
            if (!file.open(QIODevice::ReadOnly))
            {
                qDebug() << "Failed to open style sheet file:" << getStyleSheetPath();
                return;
            }

            setStyleSheet(file.readAll());
        }
        else
        {
            mCurrentStyle = mInitialStyle;
            setStyleSheet("");
            setStyle(mInitialStyle.c_str());
        }
    }

    bool Application::darkModeTurned()
    {
        const bool isDark = systemIsDark();
        if (isDark == mDarkMode)
            return false;
        mDarkMode = isDark;
        return true;
    }
}
