#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/image/alpharange.hpp>
#include <components/rtx/image/texels.hpp>

namespace Rtx
{
    // **A stored output is its fields' bytes in a fixed order, little-endian as every target is**, and
    // never the struct's own bytes: a struct's padding holds whatever the memory held, which would make
    // one output two payloads with two digests.
    static_assert(std::endian::native == std::endian::little, "a stored output is little-endian bytes");

    /// The bytes a codec writes and reads, a value at a time.
    namespace ContentBytes
    {
        /// Appends `value`'s bytes to `into`.
        template <class T>
        requires std::is_arithmetic_v<T>
        void put(std::vector<std::byte>& into, T value)
        {
            const std::size_t at = into.size();
            into.resize(at + sizeof(T));
            std::memcpy(into.data() + at, &value, sizeof(T));
        }

        /// Reads a value off the front of `from` and moves past it, answering whether `from` held one.
        template <class T>
        requires std::is_arithmetic_v<T>
        bool take(std::span<const std::byte>& from, T& value)
        {
            if (from.size() < sizeof(T))
                return false;
            std::memcpy(&value, from.data(), sizeof(T));
            from = from.subspan(sizeof(T));
            return true;
        }
    }

    /// How a pass's output is written into a stored payload and read back out of one: `encode`
    /// appends it, and `decode` reads it and answers whether the payload held exactly one, so a
    /// payload of another length is a miss and never a half-read output.
    template <class T>
    struct ContentCodec;

    template <>
    struct ContentCodec<AlphaRange>
    {
        static void encode(const AlphaRange& range, std::vector<std::byte>& into)
        {
            ContentBytes::put(into, range.mLow);
            ContentBytes::put(into, range.mHigh);
        }

        static bool decode(std::span<const std::byte> from, AlphaRange& range)
        {
            return ContentBytes::take(from, range.mLow) && ContentBytes::take(from, range.mHigh) && from.empty();
        }
    };

    template <>
    struct ContentCodec<MeanTexel>
    {
        static void encode(const MeanTexel& mean, std::vector<std::byte>& into)
        {
            for (const osg::Vec3f& colour : { mean.mColour, mean.mWhole })
                for (int channel = 0; channel < 3; ++channel)
                    ContentBytes::put(into, colour[channel]);
            ContentBytes::put(into, mean.mAlpha);
        }

        static bool decode(std::span<const std::byte> from, MeanTexel& mean)
        {
            for (osg::Vec3f* colour : { &mean.mColour, &mean.mWhole })
                for (int channel = 0; channel < 3; ++channel)
                    if (!ContentBytes::take(from, (*colour)[channel]))
                        return false;
            return ContentBytes::take(from, mean.mAlpha) && from.empty();
        }
    };

    /// A payload of bytes, as it stands: a chain's levels.
    template <>
    struct ContentCodec<std::vector<std::byte>>
    {
        static void encode(const std::vector<std::byte>& bytes, std::vector<std::byte>& into)
        {
            into.insert(into.end(), bytes.begin(), bytes.end());
        }

        static bool decode(std::span<const std::byte> from, std::vector<std::byte>& bytes)
        {
            bytes.assign(from.begin(), from.end());
            return true;
        }
    };

    /// Whether `T` has a `ContentCodec`: what a pass whose outputs are stored needs.
    template <class T>
    concept StoredOutput
        = requires(const T& value, T& read, std::vector<std::byte>& into, std::span<const std::byte> from)
    {
        ContentCodec<T>::encode(value, into);
        {
            ContentCodec<T>::decode(from, read)
            } -> std::same_as<bool>;
    };
}
