#pragma once

#include <QtGlobal>

#include <QTextStream>

namespace Misc
{
    inline void ensureUtf8Encoding(QTextStream& stream)
    {
        stream.setEncoding(QStringConverter::Utf8);
    }
}
