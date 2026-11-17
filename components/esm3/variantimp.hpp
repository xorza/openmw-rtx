#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "variant.hpp"

namespace ESM
{
    void readESMVariantValue(ESMReader& esm, Variant::Format format, VarType type, std::string& out);

    void readESMVariantValue(ESMReader& esm, Variant::Format format, VarType type, float& out);

    void readESMVariantValue(ESMReader& esm, Variant::Format format, VarType type, std::int32_t& out);

    void writeESMVariantValue(ESMWriter& esm, Variant::Format format, VarType type, const std::string& in);

    void writeESMVariantValue(ESMWriter& esm, Variant::Format format, VarType type, float in);

    void writeESMVariantValue(ESMWriter& esm, Variant::Format format, VarType type, std::int32_t in);

    struct ReadESMVariantValue
    {
        std::reference_wrapper<ESMReader> mReader;
        Variant::Format mFormat;
        VarType mType;

        ReadESMVariantValue(ESMReader& reader, Variant::Format format, VarType type)
            : mReader(reader)
            , mFormat(format)
            , mType(type)
        {
        }

        void operator()(std::monostate) const {}

        template <typename T>
        void operator()(T& value) const
        {
            readESMVariantValue(mReader.get(), mFormat, mType, value);
        }
    };

    struct WriteESMVariantValue
    {
        std::reference_wrapper<ESMWriter> mWriter;
        Variant::Format mFormat;
        VarType mType;

        WriteESMVariantValue(ESMWriter& writer, Variant::Format format, VarType type)
            : mWriter(writer)
            , mFormat(format)
            , mType(type)
        {
        }

        void operator()(std::monostate) const {}

        template <typename T>
        void operator()(const T& value) const
        {
            writeESMVariantValue(mWriter.get(), mFormat, mType, value);
        }
    };
}
