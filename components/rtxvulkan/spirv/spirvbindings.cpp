#include "spirvbindings.hpp"

#include <cstddef>
#include <format>
#include <stdexcept>
#include <unordered_map>

#include <spirv/unified1/spirv.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::size_t sHeaderWords = 5;

        /// What a module says of one id, gathered in one walk: its decorations, and what it is
        /// where it is a type, a constant or a variable.
        struct Facts
        {
            spv::Op mOp = spv::OpNop;

            /// The operands after the result id: a type's own, a constant's value, a variable's
            /// storage class.
            std::span<const std::uint32_t> mOperands;

            /// The type of a variable or a constant, which comes before its result id.
            std::uint32_t mType = 0;

            std::uint32_t mSet = 0;
            std::uint32_t mBinding = 0;
            bool mHasSet = false;
            bool mHasBinding = false;
            bool mBufferBlock = false;
        };

        /// The kind and count of the resource a variable's pointee `type` is, in `storage`.
        ModuleBinding describe(const std::unordered_map<std::uint32_t, Facts>& ids, std::uint32_t type,
            const spv::StorageClass storage, const std::uint32_t variable)
        {
            const auto at = [&](std::uint32_t id) -> const Facts& {
                const auto found = ids.find(id);
                if (found == ids.end())
                    throw std::runtime_error(
                        std::format("the type %{} of variable %{} is defined nowhere", id, variable));
                return found->second;
            };

            ModuleBinding binding;
            const Facts* facts = &at(type);
            if (facts->mOp == spv::OpTypeArray)
            {
                binding.mCount = at(facts->mOperands[1]).mOperands[0];
                facts = &at(facts->mOperands[0]);
            }
            else if (facts->mOp == spv::OpTypeRuntimeArray)
            {
                binding.mCount = 0;
                facts = &at(facts->mOperands[0]);
            }

            switch (facts->mOp)
            {
                case spv::OpTypeSampler:
                    binding.mKind = DescriptorKind::BareSampler;
                    return binding;
                case spv::OpTypeSampledImage:
                    binding.mKind = DescriptorKind::CombinedImageSampler;
                    return binding;
                case spv::OpTypeAccelerationStructureKHR:
                    binding.mKind = DescriptorKind::AccelerationStructure;
                    return binding;
                case spv::OpTypeImage:
                {
                    // `Dim` is the second operand and `Sampled` the sixth: one where a sampler reads
                    // it, two where it is read and written as storage.
                    const bool buffer = facts->mOperands[1] == spv::DimBuffer;
                    const bool storageImage = facts->mOperands[5] == 2;
                    if (buffer)
                        binding.mKind
                            = storageImage ? DescriptorKind::StorageTexelBuffer : DescriptorKind::UniformTexelBuffer;
                    else
                        binding.mKind = storageImage ? DescriptorKind::StorageImage : DescriptorKind::SampledImage;
                    return binding;
                }
                case spv::OpTypeStruct:
                    // A `buffer` block is storage, whichever of the two ways a compiler says so.
                    binding.mKind = storage == spv::StorageClassStorageBuffer || facts->mBufferBlock
                        ? DescriptorKind::StorageBuffer
                        : DescriptorKind::UniformBuffer;
                    return binding;
                default:
                    throw std::runtime_error(
                        std::format("variable %{} binds a resource of opcode {}, which is no descriptor this reads",
                            variable, static_cast<std::uint32_t>(facts->mOp)));
            }
        }
    }

    void readBindings(const std::span<const std::uint32_t> module, std::vector<ModuleBinding>& into)
    {
        into.clear();
        if (module.size() < sHeaderWords || module[0] != spv::MagicNumber)
            throw std::runtime_error("not a SPIR-V module in this machine's byte order");

        std::unordered_map<std::uint32_t, Facts> ids;
        std::vector<std::uint32_t> variables;
        for (std::size_t at = sHeaderWords; at < module.size();)
        {
            const std::uint32_t count = module[at] >> 16;
            const auto op = static_cast<spv::Op>(module[at] & 0xffffu);
            if (count == 0 || at + count > module.size())
                throw std::runtime_error(std::format("an instruction at word {} runs past the module", at));

            const std::span<const std::uint32_t> operands = module.subspan(at + 1, count - 1);
            at += count;

            switch (op)
            {
                case spv::OpDecorate:
                {
                    Facts& target = ids[operands[0]];
                    const auto decoration = static_cast<spv::Decoration>(operands[1]);
                    if (decoration == spv::DecorationDescriptorSet)
                    {
                        target.mSet = operands[2];
                        target.mHasSet = true;
                    }
                    else if (decoration == spv::DecorationBinding)
                    {
                        target.mBinding = operands[2];
                        target.mHasBinding = true;
                    }
                    else if (decoration == spv::DecorationBufferBlock)
                        target.mBufferBlock = true;
                    break;
                }
                case spv::OpTypeSampler:
                case spv::OpTypeImage:
                case spv::OpTypeSampledImage:
                case spv::OpTypeArray:
                case spv::OpTypeRuntimeArray:
                case spv::OpTypeStruct:
                case spv::OpTypePointer:
                case spv::OpTypeAccelerationStructureKHR:
                {
                    Facts& type = ids[operands[0]];
                    type.mOp = op;
                    type.mOperands = operands.subspan(1);
                    break;
                }
                case spv::OpConstant:
                case spv::OpSpecConstant:
                case spv::OpVariable:
                {
                    Facts& value = ids[operands[1]];
                    value.mOp = op;
                    value.mType = operands[0];
                    value.mOperands = operands.subspan(2);
                    if (op == spv::OpVariable)
                        variables.push_back(operands[1]);
                    break;
                }
                default:
                    break;
            }
        }

        for (const std::uint32_t variable : variables)
        {
            const Facts& facts = ids.at(variable);
            if (!facts.mHasSet || !facts.mHasBinding)
                continue;

            const Facts& pointer = ids.at(facts.mType);
            const auto storage = static_cast<spv::StorageClass>(pointer.mOperands[0]);
            ModuleBinding binding = describe(ids, pointer.mOperands[1], storage, variable);
            binding.mSet = facts.mSet;
            binding.mBinding = facts.mBinding;
            into.push_back(binding);
        }
    }
}
