#pragma once

#include <QMetaType>
#include <filesystem>

// `std::filesystem::path` as a type a `QVariant` holds, declared once for every view that hands one over.
Q_DECLARE_METATYPE(std::filesystem::path)
