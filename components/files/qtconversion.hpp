#pragma once

#include <filesystem>

#include <QString>

namespace Files
{
    QString pathToQString(const std::filesystem::path& path);

    QString pathToQString(std::filesystem::path&& path);

    std::filesystem::path pathFromQString(QStringView path);

    std::filesystem::path pathFromQString(QString&& path);
}
