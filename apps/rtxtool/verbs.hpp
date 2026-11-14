#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <components/rtx/frame/reconstruction.hpp>

namespace RtxTool
{
    /// One of the harness's commands.
    enum class Verb : std::uint8_t
    {
        Info,
        Scene,
        Shot,
        View,
        Bench,
        Check,
        Film,
        Noise,
        Tour,

        Count,
    };

    /// A set of the harness's commands: the question the options ask, since an option is read by one command, by
    /// two or by every one, and that fact written only in the prose of its own help line is one nothing can check
    /// (`shot --views=balmora` rendered the default place and said nothing). A type apart from `Verb`, so a
    /// question only one command can answer — its policy, its name — cannot be asked of a set.
    class VerbSet
    {
    public:
        /// The commands listed, braced at an option's declaration as `{ Verb::Bench, Verb::Check }`.
        constexpr VerbSet(std::initializer_list<Verb> verbs)
        {
            for (const Verb verb : verbs)
                mBits = static_cast<std::uint16_t>(mBits | bitOf(verb));
        }

        /// Every command, which is what an option nobody restricted is read by.
        static constexpr VerbSet every() { return VerbSet(sEvery); }

        constexpr bool contains(const Verb verb) const { return (mBits & bitOf(verb)) != 0; }

        constexpr std::size_t size() const { return static_cast<std::size_t>(std::popcount(mBits)); }

        /// Every command this does not hold.
        constexpr VerbSet complement() const { return VerbSet(static_cast<std::uint16_t>(~mBits & sEvery)); }

        constexpr bool operator==(const VerbSet&) const = default;

    private:
        static constexpr std::uint16_t sEvery = (1u << static_cast<unsigned>(Verb::Count)) - 1;
        static_assert(static_cast<unsigned>(Verb::Count) <= 16, "a command past the set's bits");

        static constexpr std::uint16_t bitOf(const Verb verb)
        {
            return static_cast<std::uint16_t>(1u << static_cast<unsigned>(verb));
        }

        constexpr explicit VerbSet(const std::uint16_t bits)
            : mBits(bits)
        {
        }

        std::uint16_t mBits = 0;
    };

    /// What `one` is typed as.
    std::string_view verbName(Verb one);

    /// The command called `name`, or nothing where nothing is called that.
    std::optional<Verb> verbNamed(std::string_view name);

    /// The commands in `set`, spelled the way a help line spells them: "`bench` and `check`".
    std::string describeVerbs(VerbSet set);

    /// Which shaders a command's frames run.
    enum class VerbShaders : std::uint8_t
    {
        /// The game's own, so a figure is of the kernels it ships.
        Shipped,

        /// The same modules compiled with the census of every store that was not finite (`Rtx::NotFinite`), which
        /// names the module that wrote one, at a cost no figure of the game's carries.
        Counting,
    };

    /// What one command does with the places it runs at: one row a command, which the staging, the
    /// hold and the setup every command shares all read, so what `shot` does with a routed view is
    /// a fact of the table and not of whichever branch staged it.
    struct VerbPolicy
    {
        /// Whether the world's clock stops at every place, so a frame traced again is the same frame:
        /// what a picture, a digest and a claim about a still are each about.
        bool mFreezes = false;

        /// Whether a view's route is flown. A command that flies none stands at the route's start,
        /// and a route flown runs with the clock going, whatever `mFreezes` says: a camera moving
        /// through a stopped world measures the streaming and nothing that lives in it.
        bool mFliesRoutes = false;

        /// Whether a film's track moves the eye, the clock and the sky.
        bool mFollowsTracks = false;

        /// Whether the frames are measured or filmed, which runs them without the layers unless the
        /// line asks for some: `Rtx::sValidationByDefault` is a debugging build's, and a number or a
        /// frame taken under instrumentation is not the build's.
        bool mMeasures = false;

        /// Whether every measured frame is hashed, whatever the line asks.
        bool mHashes = false;

        /// Which shaders the frames run: the counting set unless the command measures the game's figures, and a
        /// tour's both ways, the game's frames traced with the census.
        VerbShaders mShaders = VerbShaders::Counting;

        /// Whether somebody plays the session, so the interface is the game's — its menus, its
        /// console, a script's window — and `--hud` decides the bars alone. Everywhere else a
        /// picture is of the world: the run closes any menu a script opens (`Staging::closeMenus`),
        /// and `--hud` decides whether anything of the interface is drawn over it
        /// (`MWRender::RunSetup::mInterface`).
        bool mPlayed = false;

        /// The width the light is traced at: the game's where frames are measured or played, since
        /// those are the frames a player sees and none is summed, and the reference's where they
        /// are not.
        Rtx::RadianceWidth radianceWidth() const
        {
            return mMeasures || mPlayed ? Rtx::RadianceWidth::Shown : Rtx::RadianceWidth::Summed;
        }
    };

    /// The row of `one`.
    const VerbPolicy& policyOf(Verb one);
}
