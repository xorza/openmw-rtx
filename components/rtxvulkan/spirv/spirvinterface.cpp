#include "spirvinterface.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <unordered_map>

#include <spirv/unified1/spirv.hpp>

#include <components/rtx/common/error.hpp>

#include "spirvfile.hpp"

namespace Rtx
{
    namespace
    {
        /// What a module says of one member of a structure: where it starts, and how a matrix in
        /// it is laid out.
        struct MemberFacts
        {
            std::optional<std::uint32_t> mOffset;
            std::uint32_t mMatrixStride = 0;
            bool mRowMajor = false;
        };

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

            std::optional<std::uint32_t> mSpecId;
            std::optional<std::uint32_t> mLocation;
            std::optional<std::uint32_t> mArrayStride;

            /// A structure's members, as far as any decoration named them.
            std::vector<MemberFacts> mMembers;
        };

        using IdFacts = std::unordered_map<std::uint32_t, Facts>;

        /// What the module says of `id`, which `variable`'s type names: refused where it says nothing.
        const Facts& factsOf(const IdFacts& ids, const std::uint32_t id, const std::uint32_t variable)
        {
            const auto found = ids.find(id);
            if (found == ids.end())
                throw InputError(std::format("the type %{} of variable %{} is defined nowhere", id, variable));
            return found->second;
        }

        /// Operand `index` of what `facts` describes, which `variable`'s type reads: refused where the
        /// instruction is shorter than its opcode's.
        std::uint32_t operandOf(const Facts& facts, const std::size_t index, const std::uint32_t variable)
        {
            if (index >= facts.mOperands.size())
                throw InputError(std::format("variable %{} names a type of opcode {} with no operand {}", variable,
                    static_cast<std::uint32_t>(facts.mOp), index));
            return facts.mOperands[index];
        }

        /// The kind and count of the resource a variable's pointee `type` is, in `storage`.
        ModuleBinding describe(
            const IdFacts& ids, std::uint32_t type, const spv::StorageClass storage, const std::uint32_t variable)
        {
            const auto at = [&](std::uint32_t id) -> const Facts& { return factsOf(ids, id, variable); };

            ModuleBinding binding;
            const Facts* facts = &at(type);
            if (facts->mOp == spv::OpTypeArray)
            {
                binding.mCount = operandOf(at(operandOf(*facts, 1, variable)), 0, variable);
                facts = &at(operandOf(*facts, 0, variable));
            }
            else if (facts->mOp == spv::OpTypeRuntimeArray)
            {
                binding.mCount = 0;
                facts = &at(operandOf(*facts, 0, variable));
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
                    const bool buffer = operandOf(*facts, 1, variable) == spv::DimBuffer;
                    const bool storageImage = operandOf(*facts, 5, variable) == 2;
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
                    throw InputError(
                        std::format("variable %{} binds a resource of opcode {}, which is no descriptor this reads",
                            variable, static_cast<std::uint32_t>(facts->mOp)));
            }
        }

        /// The bytes of a scalar of `facts`, a number type: refused where it is no whole byte.
        std::uint32_t scalarBytes(const Facts& facts, const std::uint32_t variable)
        {
            const std::uint32_t width = operandOf(facts, 0, variable);
            if (width == 0 || width % 8 != 0)
                throw InputError(std::format("variable %{} holds a number {} bits wide", variable, width));
            return width / 8;
        }

        std::uint32_t endOf(const IdFacts& ids, std::uint32_t structure, std::uint32_t variable);

        /// How far into a value of `type` its last byte reads, which `variable`'s push block holds:
        /// a member's extent, which is what the block's end is made of. `member` is the structure's
        /// statement of the value where it is a member, which a matrix's stride is stated in.
        std::uint32_t extentOf(
            const IdFacts& ids, const std::uint32_t type, const MemberFacts* member, const std::uint32_t variable)
        {
            const Facts& facts = factsOf(ids, type, variable);
            switch (facts.mOp)
            {
                case spv::OpTypeInt:
                case spv::OpTypeFloat:
                    return scalarBytes(facts, variable);
                case spv::OpTypeVector:
                    return operandOf(facts, 1, variable)
                        * extentOf(ids, operandOf(facts, 0, variable), nullptr, variable);
                case spv::OpTypeMatrix:
                {
                    if (member == nullptr || member->mMatrixStride == 0)
                        throw InputError(std::format("variable %{} holds a matrix with no stride", variable));

                    // Strided between columns, or between rows where it is row-major, and the last
                    // of them read whole.
                    const std::uint32_t columns = operandOf(facts, 1, variable);
                    const Facts& column = factsOf(ids, operandOf(facts, 0, variable), variable);
                    const std::uint32_t rows = operandOf(column, 1, variable);
                    const std::uint32_t component
                        = scalarBytes(factsOf(ids, operandOf(column, 0, variable), variable), variable);
                    const std::uint32_t strided = member->mRowMajor ? rows : columns;
                    const std::uint32_t across = member->mRowMajor ? columns : rows;
                    return (strided - 1) * member->mMatrixStride + across * component;
                }
                case spv::OpTypeArray:
                {
                    if (!facts.mArrayStride.has_value())
                        throw InputError(std::format("variable %{} holds an array with no stride", variable));
                    const Facts& length = factsOf(ids, operandOf(facts, 1, variable), variable);
                    const std::uint32_t count = operandOf(length, 0, variable);
                    if (count == 0)
                        throw InputError(std::format("variable %{} holds an array of no elements", variable));
                    return (count - 1) * *facts.mArrayStride
                        + extentOf(ids, operandOf(facts, 0, variable), member, variable);
                }
                case spv::OpTypeStruct:
                    return endOf(ids, type, variable);
                case spv::OpTypePointer:
                    // A device address, which `buffer_reference` makes a pointer of: eight bytes.
                    if (static_cast<spv::StorageClass>(operandOf(facts, 0, variable))
                        == spv::StorageClassPhysicalStorageBuffer)
                        return 8;
                    break;
                default:
                    break;
            }
            throw InputError(std::format("variable %{} holds a member of opcode {}, whose size this does not read",
                variable, static_cast<std::uint32_t>(facts.mOp)));
        }

        /// Where the last member of `structure` ends, in bytes from its start.
        std::uint32_t endOf(const IdFacts& ids, const std::uint32_t structure, const std::uint32_t variable)
        {
            const Facts& facts = factsOf(ids, structure, variable);
            std::uint32_t end = 0;
            for (std::size_t at = 0; at < facts.mOperands.size(); ++at)
            {
                const MemberFacts* const member = at < facts.mMembers.size() ? &facts.mMembers[at] : nullptr;
                if (member == nullptr || !member->mOffset.has_value())
                    throw InputError(
                        std::format("variable %{} holds member {} of %{} at no offset", variable, at, structure));
                end = std::max(end, *member->mOffset + extentOf(ids, facts.mOperands[at], member, variable));
            }
            return end;
        }

        /// The 32-bit components an input of `type` reads at its location.
        std::uint32_t componentsOf(const IdFacts& ids, const std::uint32_t type, const std::uint32_t variable)
        {
            const Facts& facts = factsOf(ids, type, variable);
            const auto scalar = [&](const Facts& number) {
                if ((number.mOp != spv::OpTypeInt && number.mOp != spv::OpTypeFloat)
                    || scalarBytes(number, variable) != 4)
                    throw InputError(
                        std::format("input %{} is of a type that is no 32-bit number or vector of them", variable));
            };
            if (facts.mOp == spv::OpTypeVector)
            {
                scalar(factsOf(ids, operandOf(facts, 0, variable), variable));
                return operandOf(facts, 1, variable);
            }
            scalar(facts);
            return 1;
        }

        /// What specializes the constant `id`, of `facts`.
        SpecKind specKindOf(const IdFacts& ids, const std::uint32_t id, const Facts& facts)
        {
            if (facts.mOp == spv::OpSpecConstantTrue || facts.mOp == spv::OpSpecConstantFalse)
                return SpecKind::Bool;
            if (facts.mOp == spv::OpSpecConstant)
            {
                const Facts& type = factsOf(ids, facts.mType, id);
                if ((type.mOp == spv::OpTypeInt || type.mOp == spv::OpTypeFloat) && scalarBytes(type, id) == 4)
                    return SpecKind::Word;
            }
            throw InputError(
                std::format("specialization constant %{} is of opcode {} or of a type that is no 32-bit number", id,
                    static_cast<std::uint32_t>(facts.mOp)));
        }
    }

    ModuleInterface readInterface(const std::span<const std::uint32_t> module)
    {
        IdFacts ids;
        std::vector<std::uint32_t> variables;
        std::vector<std::uint32_t> specialized;
        forEachInstruction(
            module, [&](const spv::Op op, const std::span<const std::uint32_t> operands, std::size_t at) {
                // An instruction shorter than its opcode needs is a module no compiler wrote, and is refused
                // rather than read past.
                const auto need = [&](const std::size_t words) {
                    if (operands.size() < words)
                        throw InputError(
                            std::format("the instruction at word {} has {} operands of the {} its opcode {} "
                                        "needs",
                                at, operands.size(), words, static_cast<std::uint32_t>(op)));
                };

                switch (op)
                {
                    case spv::OpDecorate:
                    {
                        need(2);
                        Facts& target = ids[operands[0]];
                        switch (static_cast<spv::Decoration>(operands[1]))
                        {
                            case spv::DecorationDescriptorSet:
                                need(3);
                                target.mSet = operands[2];
                                target.mHasSet = true;
                                break;
                            case spv::DecorationBinding:
                                need(3);
                                target.mBinding = operands[2];
                                target.mHasBinding = true;
                                break;
                            case spv::DecorationSpecId:
                                need(3);
                                target.mSpecId = operands[2];
                                specialized.push_back(operands[0]);
                                break;
                            case spv::DecorationLocation:
                                need(3);
                                target.mLocation = operands[2];
                                break;
                            case spv::DecorationArrayStride:
                                need(3);
                                target.mArrayStride = operands[2];
                                break;
                            case spv::DecorationBufferBlock:
                                target.mBufferBlock = true;
                                break;
                            default:
                                break;
                        }
                        break;
                    }
                    case spv::OpMemberDecorate:
                    {
                        need(3);
                        std::vector<MemberFacts>& members = ids[operands[0]].mMembers;
                        if (members.size() <= operands[1])
                            members.resize(operands[1] + std::size_t{ 1 });
                        MemberFacts& member = members[operands[1]];
                        switch (static_cast<spv::Decoration>(operands[2]))
                        {
                            case spv::DecorationOffset:
                                need(4);
                                member.mOffset = operands[3];
                                break;
                            case spv::DecorationMatrixStride:
                                need(4);
                                member.mMatrixStride = operands[3];
                                break;
                            case spv::DecorationRowMajor:
                                member.mRowMajor = true;
                                break;
                            default:
                                break;
                        }
                        break;
                    }
                    case spv::OpTypeInt:
                    case spv::OpTypeFloat:
                    case spv::OpTypeVector:
                    case spv::OpTypeMatrix:
                    case spv::OpTypeSampler:
                    case spv::OpTypeImage:
                    case spv::OpTypeSampledImage:
                    case spv::OpTypeArray:
                    case spv::OpTypeRuntimeArray:
                    case spv::OpTypeStruct:
                    case spv::OpTypePointer:
                    case spv::OpTypeAccelerationStructureKHR:
                    {
                        need(1);
                        Facts& type = ids[operands[0]];
                        type.mOp = op;
                        type.mOperands = operands.subspan(1);
                        break;
                    }
                    case spv::OpConstant:
                    case spv::OpSpecConstant:
                    case spv::OpSpecConstantTrue:
                    case spv::OpSpecConstantFalse:
                    case spv::OpVariable:
                    {
                        need(2);
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
            });

        ModuleInterface interface;
        for (const std::uint32_t id : specialized)
            interface.mSpecConstants.push_back(
                ModuleSpecConstant{ .mId = *ids.at(id).mSpecId, .mKind = specKindOf(ids, id, ids.at(id)) });

        for (const std::uint32_t variable : variables)
        {
            const Facts& facts = ids.at(variable);
            const Facts& pointer = factsOf(ids, facts.mType, variable);
            if (pointer.mOp != spv::OpTypePointer)
                throw InputError(std::format("variable %{} is typed by no pointer", variable));
            const auto storage = static_cast<spv::StorageClass>(operandOf(pointer, 0, variable));
            const std::uint32_t pointee = operandOf(pointer, 1, variable);

            if (storage == spv::StorageClassPushConstant)
            {
                const std::uint32_t end = endOf(ids, pointee, variable);
                interface.mPushEnd = std::max(interface.mPushEnd.value_or(0), end);
            }
            else if (storage == spv::StorageClassInput && facts.mLocation.has_value())
                interface.mInputs.push_back(
                    ModuleInput{ .mLocation = *facts.mLocation, .mComponents = componentsOf(ids, pointee, variable) });
            else if (facts.mHasSet && facts.mHasBinding)
            {
                ModuleBinding binding = describe(ids, pointee, storage, variable);
                binding.mSet = facts.mSet;
                binding.mBinding = facts.mBinding;
                interface.mBindings.push_back(binding);
            }
        }
        return interface;
    }
}
