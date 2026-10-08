#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/spirv/spirvinterface.hpp>

namespace Rtx
{
    class Device;

    /// The first binding a module declares that `sets` — its pipeline layout's, each set as its
    /// layout states it, the census left out of `SET_PASS` — states another way, in words: one in a
    /// set the layout does not name, one its set does not declare, or one declared of another type
    /// or count. Nothing where they agree. A binding the layout declares and the module does not
    /// read is no disagreement: a stage reads the part of a set it needs, and a variant that counts
    /// nothing binds no census.
    std::optional<std::string> bindingDisagreement(std::span<const ModuleBinding> module, const SetTables& sets);

    /// The first specialization constant a module declares that `words` — the stage's, a word per
    /// `constant_id` (`Specialization`) — do not specialize as its type asks, in words: one past
    /// the words, which would take its GLSL default in silence, or a `bool` handed a word other
    /// than nought or one. The census's word is the stage's own and is never handed here.
    std::optional<std::string> specializationDisagreement(
        std::span<const ModuleSpecConstant> module, std::span<const std::uint32_t> words);

    /// How a module's push block, ending at `end` where it has one, disagrees with the range the
    /// layout declares, `range` bytes, in words: a block past the range, or a range past the block
    /// by more than the host's rounding of a block that ends in a 64-bit address to a multiple of
    /// eight (`hosttypes.h`). Nothing where they agree, and nothing for a module that reads no push.
    std::optional<std::string> pushDisagreement(std::optional<std::uint32_t> end, std::uint32_t range);

    /// The first input of a vertex module that `attributes` — the pipeline's description of a
    /// vertex — do not feed, in words: one at a location no attribute names, or one whose
    /// attribute's format has fewer components than it reads. Nothing where every input is fed.
    std::optional<std::string> inputDisagreement(
        std::span<const ModuleInput> inputs, std::span<const VkVertexInputAttributeDescription> attributes);

    /// SPIR-V from the device's shader directory, read once a file however many stages name it,
    /// and handed to each stage inline: `maintenance5` takes a stage's code in its `pNext`, so no
    /// `VkShaderModule` is made, named or destroyed. Kept by whoever makes the pipelines that share
    /// a file, and only until they are made: the driver reads the words during the create call.
    ///
    /// **Every file read where this is made, with its interface**, so nothing after reads again:
    /// the trace's variants made one of these each and read its 1.5 MB closest-hit module once
    /// apiece. Read-only once made, so the hands of a parallel compile share one.
    class ShaderCode
    {
    public:
        /// Reads every one of `modules`, each checked for being the one the build wrote, because a
        /// truncated `.spv` is otherwise a driver crash with no explanation.
        ///
        /// @throws InputError where a file cannot be read or is not a module.
        ShaderCode(const Device& device, std::span<const std::string_view> modules);

        ShaderCode(const ShaderCode&) = delete;
        ShaderCode& operator=(const ShaderCode&) = delete;

        /// What the `pNext` of a stage that runs `module`, one this read, points at, valid while
        /// this lives.
        ///
        /// **And the module's interface held to what the C++ states beside the GLSL**: its bindings
        /// to the layout's `sets` (`bindingDisagreement`), its specialization constants to `words`
        /// (`specializationDisagreement`), and its push block to the layout's range of `pushBytes`
        /// (`pushDisagreement`). A disagreement ends the process as a crash naming the module and
        /// what disagrees, where the device would read a resource as another, a constant would take
        /// its default, or a push would be read past what was written.
        const void* stage(std::string_view module, const SetTables& sets, std::uint32_t pushBytes,
            std::span<const std::uint32_t> words) const;

        /// Holds the inputs of `module`, a vertex stage `stage` already read, to `attributes`
        /// (`inputDisagreement`), and ends the process as a crash naming the module where one is
        /// not fed: a location the two number apart reads nothing a vertex holds.
        void feed(std::string_view module, std::span<const VkVertexInputAttributeDescription> attributes) const;

    private:
        /// Reads `module` onto the end of `mRead`.
        void readModule(std::string_view module);

        struct Read;

        /// What this read of `module`, which it must have.
        const Read& readOf(std::string_view module) const;

        /// One file's words and the structures a stage chains, which point into the words and at
        /// one another: a deque, so a later read moves none of them.
        struct Read
        {
            std::string mModule;
            std::vector<std::uint32_t> mWords;
            ModuleInterface mInterface;
            VkDebugUtilsObjectNameInfoEXT mNamed{};
            VkShaderModuleCreateInfo mCreate{};

            /// The head of the chain: the name where the device names objects, the code after it.
            const void* mStage = nullptr;
        };

        const Device& mDevice;
        std::deque<Read> mRead;
    };
}
