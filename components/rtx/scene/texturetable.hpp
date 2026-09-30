#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/image/formatcensus.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    /// What stands in a live texture slot, and so which of the row's two names carries it, stated
    /// rather than deduced from two names. Whether a slot is live is the table's to say
    /// (`HeldRows::isLive`), and a free row's kind means nothing.
    enum class TextureKind : std::uint8_t
    {
        /// A file the content named. `TextureRow::mPath` carries it.
        File,

        /// A key this renderer made for something it baked. `TextureRow::mBaked` carries it.
        Baked,
    };

    /// One slot of the table: what stands in it, its name under the kind, and how it is addressed
    /// past its edges. The two names are two fields because a file's name is a
    /// `VFS::Path::Normalized` with a guarantee its readers rely on.
    struct TextureRow
    {
        TextureKind mKind = TextureKind::File;
        VFS::Path::Normalized mPath{};
        std::string mBaked{};
        TextureWrap mWrap = TextureWrap::Repeat;

        /// What the slot is read as: a file's as it was taken, a bake's as `addBaked` was told.
        TextureEncoding mEncoding = TextureEncoding::Colour;

        /// A file's image, which the upload reads: the one the adder held, so the frame that
        /// uploads it opens nothing. Null for a bake, and for a file nothing reads at.
        osg::ref_ptr<const osg::Image> mImage{};
    };

    /// Every texture the scene names, what still names each one, and which slots changed. A slot
    /// is live from its take, before anything names it, reference counted from the hold that
    /// follows, and given back the moment nothing names it, because waiting for a
    /// sweep would keep a region's images alive across the crossing that left it. Two ways in and
    /// one table: a slot is a file the content named or a key this renderer made for something it
    /// baked, never both. A slot that is freed keeps its index.
    ///
    /// **A slot is a file, its wrap and its encoding.** The same file bound clamped and bound
    /// repeating is two slots, because a sampler is per slot and the wrap is the sampler's; the
    /// same file bound as a colour and as data is two, because the encoding is the image's format.
    /// A file names up to twelve, one per `TextureWrap` and `TextureEncoding`.
    class TextureTable : public HeldRows<TextureRow>
    {
    public:
        /// How many slots stand at once: the bindless array's, less the one its neutral texel
        /// takes. A world past it is content this renderer draws neutral, and never a frame that
        /// stops: an arrival that would take one more slot is refused and counted.
        static constexpr std::size_t sCapacity = Shaders::TEXTURE_NEUTRAL;

        /// The slot for `path` under `wrap` and `encoding`, taking one where this has not met the
        /// three. Live from here, before anything names it, and until the last thing that named it
        /// lets go. `sNoIndex` where they are new and `sCapacity` slots already stand.
        ///
        /// @param image what the upload reads for the slot: kept by the add that takes it, and
        ///        counted in `getFormats` while the slot stands. Null where nothing reads at `path`,
        ///        which the upload stands in for and refuses.
        Index add(VFS::Path::NormalizedView path, const osg::Image* image, TextureWrap wrap = TextureWrap::Repeat,
            TextureEncoding encoding = TextureEncoding::Colour);

        /// The same with no image, for a caller that describes its textures to the backend itself,
        /// as a test does.
        Index add(VFS::Path::NormalizedView path, TextureWrap wrap = TextureWrap::Repeat,
            TextureEncoding encoding = TextureEncoding::Colour)
        {
            return add(path, nullptr, wrap, encoding);
        }

        /// The slot for a texture this renderer made — a composite baked for a distant chunk —
        /// keyed by `key` rather than by a file, taking one where `key` is not known. Two chunks
        /// that would bake the same image must find the same slot, so `key` has to be stable across
        /// frames. The same slots and the same reference counting as a file's. Clamped, because a
        /// bake is one image whose coordinates run edge to edge. `sNoIndex` as `add` answers it.
        Index addBaked(std::string_view key, TextureEncoding encoding);

        /// The slot `path` stands in as a colour under any wrap, or `sNoIndex` where it stands in
        /// none. What a bake made from a file's alpha finds its source by: the alpha is the same
        /// under every wrap, and the bake's key carries the file and not the wrap.
        Index findFile(VFS::Path::NormalizedView path) const;

        std::span<const Index> getArrived() const { return mChanges.getArrived(); }
        std::span<const Index> getFreed() const { return mChanges.getFreed(); }

        /// How many slots this has ever taken, which is the share of the scene's structure revision
        /// that textures decide. A revision and not a count, because a slot freed and taken again
        /// has to read as a change.
        std::uint64_t getRevision() const { return mRevision; }

        void clearArrivals() { mChanges.clearArrivals(); }

        /// How many new textures were refused because `sCapacity` slots stood, ever. Drawn neutral,
        /// and reported by `SceneTextures`.
        std::uint32_t getRefused() const { return mRefused; }

        /// The formats of the images the standing slots keep, one count a slot.
        const FormatCensus& getFormats() const { return mFormats; }

    private:
        /// What holds and takes a slot: the scene, whose `Hold` is the one way a holder outside
        /// the tables names one, and the material table, which holds what a material names.
        friend class SceneDesc;
        friend class MaterialTable;

        /// The slot `image`, read from `path`, stands in under `wrap` and `encoding`, held for the
        /// caller until it drops it: what a surface, a sprite, a sky layer and a moon turn their
        /// image into. `sNoIndex`, holding nothing, where `add` refuses it.
        Index take(VFS::Path::NormalizedView path, const osg::Image& image, TextureWrap wrap = TextureWrap::Repeat,
            TextureEncoding encoding = TextureEncoding::Colour);

        /// Takes and gives back one name on a slot. A slot this never hands out — `sNoIndex`, and
        /// the neutral texel a layer names where the table had no room — is "none" and costs a
        /// compare. The slot is freed by the `drop` after which nothing names it. A particle
        /// emitter's sprite names a texture this way: an emitter is rebuilt every frame, so
        /// whatever recognises it between frames is what has to hold the texture.
        void hold(Index texture);
        void drop(Index texture);

        /// Whether a new slot may be taken, counting and reporting the refusal where it may not.
        bool hasRoom();

        /// Puts `row` in a free slot where there is one, in a new one otherwise, and counts the
        /// arrival.
        Index takeSlot(TextureRow row);

        /// The slot a file holds under each encoding and wrap, `sNoIndex` where it holds none.
        using FileSlots = std::array<std::array<Index, sTextureWrapCount>, sTextureEncodingCount>;

        SlotChanges mChanges;

        /// Hashes a baked key without building a `std::string` to do it.
        struct BakedHash
        {
            using is_transparent = void;

            std::size_t operator()(std::string_view key) const { return std::hash<std::string_view>{}(key); }
        };

        /// The two lookups, so that naming a texture again is the slot it already has, where a scan
        /// was O(materials x textures): a cell is a hundred of each and paid it on every material
        /// it resolved. A file's entry leaves the first map when its last slot is freed.
        std::unordered_map<VFS::Path::Normalized, FileSlots, VFS::Path::Hash, std::equal_to<>> mPathIndex;
        std::unordered_map<std::string, Index, BakedHash, std::equal_to<>> mBakedIndex;

        std::uint64_t mRevision = 0;
        std::uint32_t mRefused = 0;
        FormatCensus mFormats;
    };

    /// Which of up to sixteen takes of one image the table refused, and how many slots it had freed
    /// then (`TextureTable::getFreedCount`). A refused take is asked again once that count moves,
    /// because nothing else makes room, and not before, because until then asking is a path built
    /// to be refused. The one rule for every resolver that takes a texture.
    class RefusedTakes
    {
    public:
        /// Whether take `bit` was refused and the table has freed nothing since.
        bool stands(std::uint16_t bit, std::uint64_t freed) const { return mAt == freed && (mBits & bit) != 0; }

        void refuse(std::uint16_t bit, std::uint64_t freed)
        {
            if (mAt != freed)
                mBits = 0;

            mBits |= bit;
            mAt = freed;
        }

    private:
        std::uint16_t mBits = 0;
        std::uint64_t mAt = 0;
    };
}
