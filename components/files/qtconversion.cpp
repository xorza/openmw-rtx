#include "qtconversion.hpp"

#include <string_view>

#include <components/misc/strings/conversion.hpp>

QString Files::pathToQString(const std::filesystem::path& path)
{
    const auto tmp = path.u8string();
    return QString::fromUtf8(Misc::StringUtils::u8StringToString(tmp.data()), static_cast<qsizetype>(tmp.size()));
}

QString Files::pathToQString(std::filesystem::path&& path)
{
    const auto tmp = path.u8string();
    return QString::fromUtf8(Misc::StringUtils::u8StringToString(tmp.data()), static_cast<qsizetype>(tmp.size()));
}

std::filesystem::path Files::pathFromQString(QStringView path)
{
    return std::filesystem::path(std::u16string_view(path.utf16(), static_cast<std::size_t>(path.size())));
}

std::filesystem::path Files::pathFromQString(QString&& path)
{
    return std::filesystem::path(path.toStdU16String());
}
