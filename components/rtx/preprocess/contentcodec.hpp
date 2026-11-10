#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

#include <osg/Vec3f>

#include <components/misc/bytebuffer.hpp>

#include <components/rtx/image/alpharange.hpp>
#include <components/rtx/image/specularlayout.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/shaders/tangent.h>

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
        void put(Misc::ByteBuffer& into, T value)
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

        /// Appends a run of `values`: its count, then their bytes. `T` carries no padding, which the
        /// caller asserts where it is not a number.
        template <class T>
            requires std::is_trivially_copyable_v<T>
        void putRun(Misc::ByteBuffer& into, std::span<const T> values)
        {
            put(into, static_cast<std::uint32_t>(values.size()));
            const std::span<const std::byte> bytes = std::as_bytes(values);
            into.insert(into.end(), bytes.begin(), bytes.end());
        }

        /// Reads a run `putRun` wrote into `values` and moves past it, answering whether `from` held
        /// the whole of one.
        template <class T>
            requires std::is_trivially_copyable_v<T>
        bool takeRun(std::span<const std::byte>& from, std::vector<T>& values)
        {
            std::uint32_t count = 0;
            if (!take(from, count) || from.size() / sizeof(T) < count)
                return false;
            // `std::copy` and not `memcpy`, which an empty run hands two null pointers.
            values.resize(count);
            const std::span<const std::byte> run = from.first(std::size_t{ count } * sizeof(T));
            std::copy(run.begin(), run.end(), reinterpret_cast<std::byte*>(values.data()));
            from = from.subspan(run.size());
            return true;
        }
    }

    /// How a pass's output is written into a stored payload and read back out of one: `encode`
    /// appends it, and `decode` reads it and answers whether the payload held exactly one, so a
    /// payload of another length is a miss and never a half-read output. A decoder takes the payload
    /// whole where its output keeps the bytes as they stand (`ContentCodec<MadeChain>`), and reads it
    /// where it does not.
    template <class T>
    struct ContentCodec;

    template <>
    struct ContentCodec<AlphaRange>
    {
        static void encode(const AlphaRange& range, Misc::ByteBuffer& into)
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
        static void encode(const MeanTexel& mean, Misc::ByteBuffer& into)
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

    /// What a `_spec` map's channels mean: one byte.
    template <>
    struct ContentCodec<SpecularLayout>
    {
        static void encode(const SpecularLayout& layout, Misc::ByteBuffer& into)
        {
            ContentBytes::put(into, static_cast<std::uint8_t>(layout));
        }

        static bool decode(std::span<const std::byte> from, SpecularLayout& layout)
        {
            std::uint8_t stored = 0;
            if (!ContentBytes::take(from, stored) || !from.empty()
                || stored > static_cast<std::uint8_t>(SpecularLayout::MetalRoughness))
                return false;
            layout = static_cast<SpecularLayout>(stored);
            return true;
        }
    };

    /// A made chain: its bytes as they stand, then its form's byte.
    ///
    /// **The form last, so a read takes the payload whole** (`take`): the bytes the store read become
    /// the chain's by a move, the form dropped by a resize, where a byte at the front moved every byte
    /// after it, and a copy out of the store's buffer was a second chain in memory.
    template <>
    struct ContentCodec<MadeChain>
    {
        static void encode(const MadeChain& chain, Misc::ByteBuffer& into)
        {
            into.reserve(into.size() + chain.mBytes.size() + 1);
            into.insert(into.end(), chain.mBytes.begin(), chain.mBytes.end());
            ContentBytes::put(into, static_cast<std::uint8_t>(chain.mForm));
        }

        static bool decode(Misc::ByteBuffer& payload, MadeChain& chain)
        {
            if (payload.empty())
                return false;
            const auto form = std::to_integer<std::uint8_t>(payload.back());
            if (form > static_cast<std::uint8_t>(ChainForm::Own))
                return false;
            payload.pop_back();
            chain.mForm = static_cast<ChainForm>(form);
            chain.mBytes = std::move(payload);
            payload.clear();
            return true;
        }
    };

    /// A shape: its three findings in a byte, and in its fourth bit whether the indices are sixteen bits
    /// each, then the kept triangles, the normals as the device's words (`Shaders::packNormal`) and the
    /// added vertices' sources, each a run.
    ///
    /// **Sixteen-bit indices where every vertex is under 65536**, which nearly every model's are, and a
    /// normal four bytes and not twelve: on the PBR profile's shapes, 69.3 MiB of payload came to 29.6,
    /// and the entries held in memory (`ContentStore::Store::sResidentMost`) from 77% to 92%. The normals come
    /// back as their words read back, so a run's output is handed back so too (`sRounds`).
    template <>
    struct ContentCodec<ShapePass::Output>
    {
        /// What a decode hands back is the encoded output rounded, and not the output itself: a kept
        /// output is decoded back into its caller's, so a run and a later hit hand over one output
        /// (`ContentCache::keep`), which every normal's word reads back to whole
        /// (`aNormalsWordReadsBackToItsDirection`).
        static constexpr bool sRounds = true;

        static void encode(const ShapePass::Output& shape, Misc::ByteBuffer& into)
        {
            const std::uint32_t most = std::max(shape.mKept.empty() ? 0u : std::ranges::max(shape.mKept),
                shape.mSources.empty() ? 0u : std::ranges::max(shape.mSources));
            const bool narrow = most <= 0xffffu;
            ContentBytes::put(into,
                static_cast<std::uint8_t>((shape.mShape.mSheet ? 1u : 0u) | (shape.mShape.mFolded ? 2u : 0u)
                    | (shape.mShape.mPocketed ? 4u : 0u) | (narrow ? 8u : 0u)));
            const auto putIndices = [&](const std::vector<std::uint32_t>& indices) {
                if (!narrow)
                {
                    ContentBytes::putRun(into, std::span<const std::uint32_t>(indices));
                    return;
                }
                ContentBytes::put(into, static_cast<std::uint32_t>(indices.size()));
                for (const std::uint32_t index : indices)
                    ContentBytes::put(into, static_cast<std::uint16_t>(index));
            };
            putIndices(shape.mKept);
            ContentBytes::put(into, static_cast<std::uint32_t>(shape.mNormals.size()));
            for (const osg::Vec3f& normal : shape.mNormals)
                ContentBytes::put(into, Shaders::packNormal(normal));
            putIndices(shape.mSources);
        }

        static bool decode(std::span<const std::byte> from, ShapePass::Output& shape)
        {
            std::uint8_t findings = 0;
            if (!ContentBytes::take(from, findings) || findings > 15u)
                return false;
            const bool narrow = (findings & 8u) != 0;
            const auto takeIndices = [&](std::vector<std::uint32_t>& indices) {
                if (!narrow)
                    return ContentBytes::takeRun(from, indices);
                std::uint32_t count = 0;
                if (!ContentBytes::take(from, count) || from.size() / sizeof(std::uint16_t) < count)
                    return false;
                indices.resize(count);
                for (std::uint32_t& index : indices)
                {
                    std::uint16_t narrowed = 0;
                    ContentBytes::take(from, narrowed);
                    index = narrowed;
                }
                return true;
            };
            std::uint32_t normals = 0;
            if (!takeIndices(shape.mKept) || !ContentBytes::take(from, normals)
                || from.size() / sizeof(std::uint32_t) < normals)
                return false;
            shape.mNormals.resize(normals);
            for (osg::Vec3f& normal : shape.mNormals)
            {
                std::uint32_t word = 0;
                ContentBytes::take(from, word);
                normal = Shaders::unpackNormal(word);
            }
            if (!takeIndices(shape.mSources) || !from.empty())
                return false;

            shape.mShape = FoldedShape{
                .mSheet = (findings & 1u) != 0, .mFolded = (findings & 2u) != 0, .mPocketed = (findings & 4u) != 0
            };
            return true;
        }
    };

    /// Whether `T` has a `ContentCodec`: what a pass whose outputs are stored needs.
    template <class T>
    concept StoredOutput = requires(const T& value, T& read, Misc::ByteBuffer& payload) {
        ContentCodec<T>::encode(value, payload);
        { ContentCodec<T>::decode(payload, read) } -> std::same_as<bool>;
    };
}
