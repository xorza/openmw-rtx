/// Functions used to read raw binary data from .nif files

#pragma once

#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <format>
#include <istream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include <osg/BoundingSphere>
#include <osg/Quat>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/files/istreamptr.hpp>
#include <components/files/utils.hpp>
#include <components/misc/endianness.hpp>
#include <components/misc/float16.hpp>

#include "niftypes.hpp"

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace Nif
{

    class Reader;

    template <std::size_t numInstances, typename T>
    inline void readBufferOfType(Files::IStreamPtr& pIStream, T* dest)
    {
        static_assert(
            std::is_arithmetic_v<T> || std::is_same_v<T, Misc::float16_t>, "Buffer element type is not arithmetic");
        static_assert(!std::is_same_v<T, bool>, "Buffer element type is boolean");
        pIStream->read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(numInstances * sizeof(T)));
        if (pIStream->fail())
            throw std::runtime_error(std::format("Failed to read typed ({}) dynamic buffer of {} instances: {}",
                typeid(T).name(), numInstances, std::generic_category().message(errno)));
        if constexpr (Misc::IS_BIG_ENDIAN)
            for (std::size_t i = 0; i < numInstances; i++)
                Misc::swapEndiannessInplace(dest[i]);
    }

    template <std::size_t numInstances, typename T>
    inline void readBufferOfType(Files::IStreamPtr& pIStream, T (&dest)[numInstances])
    {
        readBufferOfType<numInstances>(pIStream, static_cast<T*>(dest));
    }

    template <typename T>
    inline void readDynamicBufferOfType(Files::IStreamPtr& pIStream, T* dest, std::size_t numInstances)
    {
        static_assert(
            std::is_arithmetic_v<T> || std::is_same_v<T, Misc::float16_t>, "Buffer element type is not arithmetic");
        static_assert(!std::is_same_v<T, bool>, "Buffer element type is boolean");
        pIStream->read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(numInstances * sizeof(T)));
        if (pIStream->fail())
            throw std::runtime_error(std::format("Failed to read typed ({}) dynamic buffer of {} instances: {}",
                typeid(T).name(), numInstances, std::generic_category().message(errno)));
        if constexpr (Misc::IS_BIG_ENDIAN)
            for (std::size_t i = 0; i < numInstances; i++)
                Misc::swapEndiannessInplace(dest[i]);
    }

    class NIFStream;

    template <class T>
    void readRecord(NIFStream& stream, T& value);

    class NIFStream
    {
        const Reader& mReader;
        Files::IStreamPtr mStream;
        const ToUTF8::StatelessUtf8Encoder* mEncoder;
        std::string mBuffer;
        std::size_t mStreamSize;

    public:
        explicit NIFStream(
            const Reader& reader, Files::IStreamPtr&& stream, const ToUTF8::StatelessUtf8Encoder* encoder)
            : mReader(reader)
            , mStream(std::move(stream))
            , mEncoder(encoder)
            , mStreamSize(static_cast<std::size_t>(Files::getStreamSizeLeft(*mStream)))
        {
        }

        const Reader& getFile() const { return mReader; }

        std::uint32_t getVersion() const;
        std::uint32_t getUserVersion() const;
        std::uint32_t getBethVersion() const;

        /// Convert human-readable version numbers into a number that can be compared.
        static constexpr std::uint32_t generateVersion(
            std::uint8_t major, std::uint8_t minor, std::uint8_t patch, std::uint8_t rev)
        {
            return static_cast<std::uint32_t>((major << 24) + (minor << 16) + (patch << 8) + rev);
        }

        void skip(std::size_t size) { mStream->ignore(static_cast<std::streamsize>(size)); }

        /// Read into a single instance of type
        template <class T>
        void read(T& data)
        {
            readBufferOfType<1>(mStream, &data);
        }

        /// Read multiple instances of type into an array
        template <class T, std::size_t size>
        void readArray(std::array<T, size>& arr)
        {
            readBufferOfType<size>(mStream, arr.data());
        }

        /// Read instances of type into a dynamic buffer
        template <class T>
        void read(T* dest, std::size_t size)
        {
            readDynamicBufferOfType<T>(mStream, dest, size);
        }

        /// Read multiple instances of type into a vector
        template <class T>
        void readVector(std::vector<T>& vec, std::size_t size)
        {
            if (size == 0)
                return;

            checkStreamSize(size * sizeof(T));

            vec.resize(size);
            read(vec.data(), size);
        }

        /// Extract an instance of type
        template <class T>
        T get()
        {
            T data;
            read(data);
            return data;
        }

        /// Read a string of the given length
        std::string getSizedString(std::size_t length);

        /// Read a string of the length specified in the file
        std::string getSizedString() { return getSizedString(get<std::uint32_t>()); }

        /// Read a list of strings without using the string table, e.g. the string table itself
        void getSizedStrings(std::vector<std::string>& vec, std::size_t size);

        /// Read a Bethesda header string that uses a byte for length
        std::string getExportString() { return getSizedString(get<std::uint8_t>()); }

        /// Read the version string which doesn't start with a number and ends with "\n"
        std::string getVersionString();

        /// Read a sequence of null-terminated strings
        std::string getStringPalette();

        template <class Count, class T, class Read>
        void readVectorOfRecords(Count count, Read&& read, std::vector<T>& values)
        {
            values.clear();
            values.reserve(count);
            for (Count i = 0; i < count; ++i)
            {
                T value{};
                read(*this, value);
                values.push_back(std::move(value));
            }
        }

        template <class Count, class T, class Read>
        void readVectorOfRecords(Read&& read, std::vector<T>& values)
        {
            readVectorOfRecords(get<Count>(), std::forward<Read>(read), values);
        }

        template <class Count, class T>
        void readVectorOfRecords(Count count, std::vector<T>& values)
        {
            readVectorOfRecords(count, readRecord<T>, values);
        }

        template <class Count, class T>
        void readVectorOfRecords(std::vector<T>& values)
        {
            readVectorOfRecords<Count>(readRecord<T>, values);
        }

    private:
        void checkStreamSize(std::size_t size);
    };

    template <class T>
    void readRecord(NIFStream& stream, T& value)
    {
        value.read(&stream);
    }

    template <>
    void NIFStream::read<osg::Vec2f>(osg::Vec2f& data);
    template <>
    void NIFStream::read<osg::Vec3f>(osg::Vec3f& data);
    template <>
    void NIFStream::read<osg::Vec4f>(osg::Vec4f& data);
    template <>
    void NIFStream::read<Matrix3>(Matrix3& data);
    template <>
    void NIFStream::read<osg::Quat>(osg::Quat& data);
    template <>
    void NIFStream::read<osg::BoundingSpheref>(osg::BoundingSpheref& data);
    template <>
    void NIFStream::read<NiTransform>(NiTransform& data);
    template <>
    void NIFStream::read<NiQuatTransform>(NiQuatTransform& data);
    template <>
    void NIFStream::read<bool>(bool& data);
    template <>
    void NIFStream::read<std::string>(std::string& data);

    template <>
    void NIFStream::read<osg::Vec2f>(osg::Vec2f* dest, std::size_t size);
    template <>
    void NIFStream::read<osg::Vec3f>(osg::Vec3f* dest, std::size_t size);
    template <>
    void NIFStream::read<osg::Vec4f>(osg::Vec4f* dest, std::size_t size);
    template <>
    void NIFStream::read<Matrix3>(Matrix3* dest, std::size_t size);
    template <>
    void NIFStream::read<osg::Quat>(osg::Quat* dest, std::size_t size);
    template <>
    void NIFStream::read<osg::BoundingSpheref>(osg::BoundingSpheref* dest, std::size_t size);
    template <>
    void NIFStream::read<NiTransform>(NiTransform* dest, std::size_t size);
    template <>
    void NIFStream::read<NiQuatTransform>(NiQuatTransform* dest, std::size_t size);
    template <>
    void NIFStream::read<bool>(bool* dest, std::size_t size);
    template <>
    void NIFStream::read<std::string>(std::string* dest, std::size_t size);

}
