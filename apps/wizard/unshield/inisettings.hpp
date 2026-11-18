#pragma once

#include <cstdint>
#include <fstream>

#include <QHash>
#include <QVariant>

#include <components/toutf8/toutf8.hpp>

namespace Wizard
{

    typedef QHash<QString, QVariant> SettingsMap;

    class IniSettings
    {
    public:
        explicit IniSettings() = default;

        inline QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const
        {
            return mSettings.value(key, defaultValue);
        }

        inline QList<QVariant> values() const { return mSettings.values(); }

        inline void setValue(const QString& key, const QVariant& value) { mSettings.insert(key, value); }

        inline void remove(const QString& key) { mSettings.remove(key); }

        QStringList findKeys(const QString& text);

        bool readFile(std::ifstream& stream, ToUTF8::FromType encoding);
        bool writeFile(const QString& path, std::ifstream& stream, ToUTF8::FromType encoding);

        bool parseInx(const QString& path);

    private:
        std::int32_t getLastNewline(const QString& buffer, std::int32_t from);

        SettingsMap mSettings;
    };

}
