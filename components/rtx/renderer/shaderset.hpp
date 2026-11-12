#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>

#include <components/misc/digestwords.hpp>
#include <components/misc/hashstate.hpp>

namespace Rtx
{
    /// One compiled shader, under the file name the build gave it, and its words as a driver takes them.
    struct ShaderModule
    {
        std::string_view mName;
        std::span<const std::uint32_t> mWords;
    };

    /// A number that changes when any of `modules` does and with nothing else: each one's name and words, in
    /// order, so renaming one is a change and two trading contents is not the same set. Inline, because the
    /// build's embedding tool states the game's set's with it and links nothing of the core.
    inline Misc::DigestWords digestShaders(const std::span<const ShaderModule> modules)
    {
        Misc::HashState digest;
        for (const ShaderModule& module : modules)
        {
            digest.add(std::span<const char>(module.mName));
            digest.add(module.mWords);
        }
        return digest.getWords();
    }

    /// The compiled shaders a renderer makes its pipelines of, borrowed for as long as the renderer lives: the
    /// game's, compiled into it by the build, or a harness's, read from the tree that built it. Sorted by name.
    struct ShaderSet
    {
        std::span<const ShaderModule> mModules;

        /// `digestShaders` of the modules, which the driver's caches are keyed on.
        Misc::DigestWords mDigest{};

        /// Whether the modules count their stores that were not finite into the census (`lib/census.glsl`): one
        /// value with the modules, because the two cannot disagree — a census module binds what a device that
        /// does not count lays out nowhere, and a device that counts handed modules that do not would say nought.
        bool mCensus = false;

        /// The module called `name`, or null where the set holds none.
        const ShaderModule* find(const std::string_view name) const
        {
            const auto found = std::ranges::lower_bound(mModules, name, {}, &ShaderModule::mName);
            return found != mModules.end() && found->mName == name ? &*found : nullptr;
        }
    };
}
