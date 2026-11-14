#pragma once

#include <QApplication>
#include <QTranslator>

namespace L10n
{
    extern QTranslator AppTranslator;
    extern QTranslator ComponentsTranslator;
    extern QTranslator QtBaseAppTranslator;

    void installQtTranslations(QApplication& app, const QString& appName, const QString& resourcesPath);
}
