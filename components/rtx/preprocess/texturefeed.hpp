#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <osg/Image>
#include <osg/Referenced>
#include <osg/ref_ptr>

#include <components/rtx/common/index.hpp>
#include <components/rtx/image/imageshape.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/scene/refusal.hpp>

namespace Rtx
{
    /// Which request of the feed a completion answers, never issued twice by one asker.
    enum class FeedTicket : std::uint32_t
    {
    };

    /// **Whether the feed's room is still open to an ask that takes some**: closed once an ask before it in the same
    /// hand-over was refused for room, so the one refused is never passed by those behind it, while an ask that takes
    /// no room still goes.
    enum class FeedRoom : std::uint8_t
    {
        Open,
        Closed,
    };

    /// What the feed answers an ask.
    enum class FeedAdmission : std::uint8_t
    {
        /// In flight, every room its making takes reserved with it.
        Admitted,

        /// Refused for the room its making takes: none left, or `FeedRoom::Closed`. The rest may still go.
        NoRoom,

        /// Refused because every request the feed keeps in flight is: nothing more goes.
        NoSlot,
    };

    /// A run of a chain's levels, `mFirst` up to and not including `mEnd`, level 0 the finest.
    struct LevelSpan
    {
        std::uint8_t mFirst = 0;
        std::uint8_t mEnd = 0;

        bool empty() const { return mFirst >= mEnd; }

        friend bool operator==(const LevelSpan&, const LevelSpan&) = default;
    };

    /// **One ask of the feed**: a file's texels as a slot reads them, the levels `mLevels` names, for the tenant
    /// `mGeneration` names of the asker's row. What the asker names its scene and row by is handed back whole in the
    /// completion, so the asker keeps no table of what it asked.
    struct FeedRequest
    {
        FeedTicket mTicket{};

        /// The asker's own name for the scene the row is of.
        std::uint8_t mScene = 0;
        TextureIndex mTexture = sNoRow;
        std::uint32_t mGeneration = 0;

        /// The file, its partner where the slot is a converted pair's, and how the slot reads them.
        osg::ref_ptr<const osg::Image> mFile{};
        osg::ref_ptr<const osg::Image> mPartner{};
        TextureEncoding mEncoding = TextureEncoding::Colour;

        /// The shape the asker planned the texels at (`shapeOfFile`), which the completion answers with what they are.
        ImageShape mShape{};
        LevelSpan mLevels{};
    };

    /// **The texels made or read**, described as a backend uploads them and holding what the description spans: the
    /// file or the chain made of it, the texels laid where the file's could not be uploaded as they stand, and the
    /// level table. Each span points into a heap buffer or an image this holds, so a move leaves it valid.
    struct FeedMade
    {
        osg::ref_ptr<const osg::Referenced> mHolds{};
        std::vector<std::byte> mLaid{};
        std::vector<MipLevel> mLevels{};
        TextureData mTexels{};

        /// What the texels are, which a file whose chain was made in another form than planned answers differently.
        ImageShape mShape{};
    };

    /// Content the renderer cannot use, and why: terminal for the tenant.
    struct FeedFailed
    {
        Refused mCause = Refused::Texture;
        std::string mWhy{};
    };

    /// A request answered, with the request whole.
    struct FeedArrival
    {
        FeedRequest mRequest;
        std::variant<FeedMade, FeedFailed> mOutcome;
    };

    /// **Where a texture's texels come from**: requests issued by residency, completions taken in the order asked, so
    /// two processes stand one image on one frame whatever their threads' timing. Only what is in flight is past
    /// recall, and the credit bounds it.
    class TextureFeed
    {
    public:
        /// **Admits `request` where the feed has credit for it**, every room its making takes reserved with it, and
        /// answers whether it did and why not: what is admitted is made without waiting on anything a later request
        /// holds, and what is refused is charged nothing and asked again later. One that takes room is refused where
        /// `room` is closed.
        [[nodiscard]] virtual FeedAdmission ask(FeedRequest request, FeedRoom room) = 0;

        /// The next completion in the order asked, or nothing where it has not come; never blocks.
        virtual std::optional<FeedArrival> take() = 0;

        /// Its completion is dropped as it lands.
        virtual void forget(FeedTicket ticket) = 0;

        TextureFeed(const TextureFeed&) = delete;
        TextureFeed& operator=(const TextureFeed&) = delete;

    protected:
        TextureFeed() = default;
        ~TextureFeed() = default;
    };
}
