#pragma once

#include <filesystem>

#include <QMetaType>

// `std::filesystem::path` as a type a `QVariant` holds, declared once for every view that hands one over.
Q_DECLARE_METATYPE(std::filesystem::path)
