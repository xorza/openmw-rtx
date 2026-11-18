#pragma once

#include <QTextStream>
#include <QtGlobal>

namespace Misc
{
    inline void ensureUtf8Encoding(QTextStream& stream)
    {
        stream.setEncoding(QStringConverter::Utf8);
    }
}
