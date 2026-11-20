#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

#include <components/rtx/common/slots.hpp>

#include "refusal.hpp"

namespace Rtx
{
    /// Content the game uses and this renderer cannot, as one record for every kind of it.
    ///
    /// **One way for all of it.** A reader of content answers with a `Result`, and the error says
    /// why: a mesh's arrays, a texture's format, a sky mesh the archives do not hold. Whatever
    /// decides what becomes of the content reports that here, with the kind and the name. Content
    /// that draws nothing in the game — a lamp of no radius, an empty geometry, a moon of size
    /// nought — draws nothing here too and is not a refusal.
    ///
    /// **Named once and counted.** Each distinct refusal — its kind, its name and its reason — goes
    /// to the log the first time it is met, in one shape at one level, and is counted by kind; a
    /// lamp refused every frame is one refusal. A repeat allocates nothing. The frame's thread
    /// only: what cannot reach it holds its refusals as `Refusal` and hands them over.
    class Refusals
    {
    public:
        /// Reports `kind` of content named `name`, or unnamed where `name` is empty, left out or
        /// stood in for because of `why`.
        void refuse(Refused kind, std::string_view name, std::string_view why);

        /// Reports what something that could not reach this held.
        void refuse(std::span<const Refusal> held);

        /// How many distinct refusals of `kind` were reported.
        std::uint32_t count(Refused kind) const { return mCounts[static_cast<std::size_t>(kind)]; }

        /// Every distinct refusal, in the order it was met: a reader that keeps how many it has read takes the new
        /// ones off the end.
        std::span<const Refusal> getMet() const { return mNamed; }

    private:
        /// What one refusal is told apart by: its kind, its name and its reason.
        using Key = std::tuple<Refused, std::string_view, std::string_view>;

        struct KeyOf
        {
            Key operator()(const Refusal& refusal) const { return Key(refusal.mKind, refusal.mName, refusal.mWhy); }
        };

        /// Every distinct refusal, in the order met, and its places in it sorted by key, searched by one made of views
        /// so a repeat builds no string. **Bounded by the content**: one entry a kind, name and cause, and every name
        /// is one of the content's drawables, models, textures, sky layers, lamps or emitters, each refused for a cause
        /// of a fixed few; a repeat adds nothing. A vector kept sorted by hand rather than a `flat_set`, because this
        /// header reaches every file that includes the scene and Boost.Container is the heaviest thing it would bring.
        std::vector<Refusal> mNamed;
        std::vector<std::uint32_t> mSorted;
        std::array<std::uint32_t, sRefusedKinds> mCounts{};
    };
}
