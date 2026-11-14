#pragma once

#include <QString>

#include "pathutil.hpp"

namespace VFS::Path
{
    QString normalizedToQString(NormalizedView path);

    QString normalizedToQString(Normalized&& path);

    Normalized normalizedFromQString(QStringView path);

    Normalized normalizedFromQString(QString&& path);
}
