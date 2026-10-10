#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <spirv/unified1/spirv.hpp>

#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>
#include <components/rtxvulkan/shaders/shared/gui.h>
#include <components/rtxvulkan/shaders/shared/pane.h>
#include <components/rtxvulkan/shaders/shared/sets.h>
#include <components/rtxvulkan/spirv/spirvfile.hpp>
#include <components/rtxvulkan/spirv/spirvinterface.hpp>

namespace Rtx
{
    namespace
    {
        ModuleInterface interfaceOf(const char* module, const char* directory = OPENMW_RTX_SHADER_DIR)
        {
            return readInterface(readSpirv(std::filesystem::path(directory) / module));
        }

        std::vector<ModuleBinding> bindingsOf(const char* module)
        {
            return interfaceOf(module).mBindings;
        }

        /// **A module's bindings are read as its GLSL declares them**: the pane filter's seven images
        /// in its pass's set, one each, read and written as storage — and nothing else in that set
        /// but the census, where the build counts.
        TEST(RtxSpirvInterfaceTest, thePaneFiltersSevenStorageImagesAreReadOffItsModule)
        {
            std::vector<ModuleBinding> pass;
            for (const ModuleBinding& bound : bindingsOf("pane.comp.spv"))
                if (bound.mSet == Shaders::SET_PASS && bound.mBinding != Shaders::BIND_CENSUS)
                    pass.push_back(bound);
            std::ranges::sort(pass, {}, &ModuleBinding::mBinding);

            ASSERT_EQ(pass.size(), std::size_t{ Shaders::PANE_BINDINGS });
            for (std::uint32_t at = 0; at < Shaders::PANE_BINDINGS; ++at)
                EXPECT_EQ(pass[at],
                    (ModuleBinding{ .mSet = Shaders::SET_PASS,
                        .mBinding = at,
                        .mKind = DescriptorKind::StorageImage,
                        .mCount = 1 }))
                    << "binding " << at;
        }

        /// **The trace's own kinds read as theirs**: the scene's top-level structure, and the
        /// bindless textures as an array of no stated length, which is a count of nought.
        TEST(RtxSpirvInterfaceTest, aStructureAndAnArrayOfNoLengthAreReadAsWhatTheyAre)
        {
            const std::vector<ModuleBinding> bindings = bindingsOf("visibility.rgen.spv");
            EXPECT_TRUE(std::ranges::any_of(bindings, [](const ModuleBinding& bound) {
                return bound.mKind == DescriptorKind::AccelerationStructure && bound.mCount == 1;
            })) << "no top-level structure";
            EXPECT_TRUE(std::ranges::any_of(bindings, [](const ModuleBinding& bound) {
                return bound.mKind == DescriptorKind::CombinedImageSampler && bound.mCount == 0;
            })) << "no array of textures of no length";

            const std::vector<std::uint32_t> notAModule{ 1u, 2u, 3u, 4u, 5u };
            EXPECT_THROW(readInterface(notAModule), InputError);

            // **A module cut short is refused as the installation's**, as its file is: the pane
            // filter's words to the first word of its first instruction longer than one, whose length
            // then runs past the end.
            std::vector<std::uint32_t> cut = readSpirv(std::filesystem::path(OPENMW_RTX_SHADER_DIR) / "pane.comp.spv");
            std::size_t at = 5;
            while ((cut[at] >> 16) < 2)
                at += cut[at] >> 16;
            cut.resize(at + 1);
            EXPECT_THROW(readInterface(cut), InputError);

            // **An instruction shorter than its opcode is refused, and not read past**: a module that
            // ends in a decoration of a set with no set in it, a type with no id, and a variable with
            // no id after its type, each a whole instruction the module holds.
            const std::vector<std::uint32_t> header{ spv::MagicNumber, 0x00010600u, 0u, 1u, 0u };
            const std::uint32_t set = spv::DecorationDescriptorSet;
            for (const std::vector<std::uint32_t>& last :
                { std::vector<std::uint32_t>{ (3u << 16) | spv::OpDecorate, 1u, set },
                    std::vector<std::uint32_t>{ (1u << 16) | spv::OpTypeStruct },
                    std::vector<std::uint32_t>{ (2u << 16) | spv::OpVariable, 7u } })
            {
                std::vector<std::uint32_t> shortened = header;
                shortened.insert(shortened.end(), last.begin(), last.end());
                EXPECT_THROW(readInterface(shortened), InputError) << "opcode " << (last[0] & 0xffffu);
            }
            std::vector<std::uint32_t> whole = header;
            whole.insert(whole.end(), { (4u << 16) | spv::OpDecorate, 1u, set, 0u });
            EXPECT_NO_THROW(readInterface(whole)) << "a decoration of a set, whole";
        }

        /// **The rest of a module's interface reads as its GLSL states it**: the line pass's vertex
        /// stage reads a position of three components and a colour of four, and its push block
        /// ends where `LineConstants` does, eighty bytes in; the interface's fragment stage
        /// specializes on one `bool` and its vertex stage on nothing, and a module the build counts
        /// in specializes on its census's word as well.
        TEST(RtxSpirvInterfaceTest, inputsThePushBlocksEndAndTheSpecializationConstantsReadAsTheGlslStatesThem)
        {
            const ModuleInterface line = interfaceOf("line.vert.spv");
            EXPECT_EQ(line.mInputs,
                (std::vector<ModuleInput>{
                    { .mLocation = 0, .mComponents = 3 }, { .mLocation = 1, .mComponents = 4 } }));
            EXPECT_EQ(line.mPushEnd, std::optional<std::uint32_t>(80u));

            const ModuleInterface gui = interfaceOf("gui.vert.spv");
            EXPECT_EQ(gui.mInputs,
                (std::vector<ModuleInput>{ { .mLocation = 0, .mComponents = 3 }, { .mLocation = 1, .mComponents = 4 },
                    { .mLocation = 2, .mComponents = 2 } }));
            EXPECT_EQ(gui.mPushEnd, std::nullopt) << "a stage pushed nothing has a push block";
            EXPECT_TRUE(gui.mSpecConstants.empty());

            EXPECT_EQ(interfaceOf("gui.frag.spv").mSpecConstants,
                (std::vector<ModuleSpecConstant>{
                    { .mId = Shaders::GUI_SPEC_PREMULTIPLIED, .mKind = SpecKind::Bool } }));
            const std::vector<ModuleSpecConstant> counted
                = interfaceOf("gui.frag.spv", OPENMW_RTX_CENSUS_SHADER_DIR).mSpecConstants;
            EXPECT_EQ(counted.size(), 2u);
            EXPECT_TRUE(std::ranges::find(
                            counted, ModuleSpecConstant{ .mId = Shaders::SPEC_CENSUS_KERNEL, .mKind = SpecKind::Word })
                != counted.end())
                << "the census's word is no specialization of a counted module";
        }

        /// **A push block's end is where its last byte is read**, through a nested structure, an
        /// array and a matrix: a block of a `float` at nought and, at sixteen, a structure of a
        /// three-element array of `vec2` strided by eight and a matrix strided by sixteen at
        /// twenty-four. The array ends at 2·8 + 8 = 24 into the structure. A column-major `mat2x3`,
        /// two columns of three, ends at 24 + 16 + 3·4 = 52, its second column read whole, so the
        /// block at 16 + 52 = 68; row-major, its three rows of two end at 24 + 2·16 + 2·4 = 64, and
        /// the block at 80. A `mat3x2`, three columns of two, is the same two the other way round.
        TEST(RtxSpirvInterfaceTest, aPushBlocksEndIsItsLastByteThroughStructuresArraysAndMatrices)
        {
            const auto instruction = [](spv::Op op, std::vector<std::uint32_t> operands) {
                operands.insert(operands.begin(), (static_cast<std::uint32_t>(operands.size() + 1) << 16) | op);
                return operands;
            };
            enum : std::uint32_t
            {
                Float = 1,
                Uint,
                Vec2,
                Vec3,
                Mat2x3,
                Three,
                Array,
                Inner,
                Block,
                Pointer,
                Variable,
            };
            const auto module = [&](const bool rowMajor, const std::uint32_t columns) {
                std::vector<std::uint32_t> words{ spv::MagicNumber, 0x00010600u, 0u, Variable + 1, 0u };
                for (const std::vector<std::uint32_t>& each :
                    { instruction(spv::OpDecorate, { Array, spv::DecorationArrayStride, 8 }),
                        instruction(spv::OpMemberDecorate, { Inner, 0, spv::DecorationOffset, 0 }),
                        instruction(spv::OpMemberDecorate, { Inner, 1, spv::DecorationOffset, 24 }),
                        instruction(spv::OpMemberDecorate, { Inner, 1, spv::DecorationMatrixStride, 16 }),
                        rowMajor ? instruction(spv::OpMemberDecorate, { Inner, 1, spv::DecorationRowMajor })
                                 : instruction(spv::OpMemberDecorate, { Inner, 1, spv::DecorationColMajor }),
                        instruction(spv::OpMemberDecorate, { Block, 0, spv::DecorationOffset, 0 }),
                        instruction(spv::OpMemberDecorate, { Block, 1, spv::DecorationOffset, 16 }),
                        instruction(spv::OpTypeFloat, { Float, 32 }), instruction(spv::OpTypeInt, { Uint, 32, 0 }),
                        instruction(spv::OpTypeVector, { Vec2, Float, 2 }),
                        instruction(spv::OpTypeVector, { Vec3, Float, 3 }),
                        instruction(spv::OpTypeMatrix, { Mat2x3, columns == 2 ? Vec3 : Vec2, columns }),
                        instruction(spv::OpConstant, { Uint, Three, 3 }),
                        instruction(spv::OpTypeArray, { Array, Vec2, Three }),
                        instruction(spv::OpTypeStruct, { Inner, Array, Mat2x3 }),
                        instruction(spv::OpTypeStruct, { Block, Float, Inner }),
                        instruction(spv::OpTypePointer, { Pointer, spv::StorageClassPushConstant, Block }),
                        instruction(spv::OpVariable, { Pointer, Variable, spv::StorageClassPushConstant }) })
                    words.insert(words.end(), each.begin(), each.end());
                return words;
            };

            EXPECT_EQ(readInterface(module(false, 2)).mPushEnd, std::optional<std::uint32_t>(68u));
            EXPECT_EQ(readInterface(module(true, 2)).mPushEnd, std::optional<std::uint32_t>(80u))
                << "a row-major matrix strided by its rows";
            EXPECT_EQ(readInterface(module(false, 3)).mPushEnd, std::optional<std::uint32_t>(80u));
            EXPECT_EQ(readInterface(module(true, 3)).mPushEnd, std::optional<std::uint32_t>(68u));
        }

        /// **A layout C++ cannot mirror is refused**: a structure of a 64-bit address at nought and a
        /// `uint` at eight ends at twelve, and C++ rounds it to its eight-byte alignment, sixteen. A
        /// member after it at twelve stands where C++ has padding, and one at sixteen where C++ has
        /// the next member; an array of it strided by twelve is short of C++'s stride, and one
        /// strided by sixteen is C++'s.
        TEST(RtxSpirvInterfaceTest, aLayoutCppCannotMirrorIsRefused)
        {
            const auto instruction = [](spv::Op op, std::vector<std::uint32_t> operands) {
                operands.insert(operands.begin(), (static_cast<std::uint32_t>(operands.size() + 1) << 16) | op);
                return operands;
            };
            enum : std::uint32_t
            {
                Float = 1,
                Uint,
                Uint64,
                Two,
                Inner,
                Array,
                Block,
                Pointer,
                Variable,
            };
            const auto module = [&](const std::uint32_t after, const std::uint32_t stride) {
                std::vector<std::uint32_t> words{ spv::MagicNumber, 0x00010600u, 0u, Variable + 1, 0u };
                for (const std::vector<std::uint32_t>& each :
                    { instruction(spv::OpDecorate, { Array, spv::DecorationArrayStride, stride }),
                        instruction(spv::OpMemberDecorate, { Inner, 0, spv::DecorationOffset, 0 }),
                        instruction(spv::OpMemberDecorate, { Inner, 1, spv::DecorationOffset, 8 }),
                        instruction(spv::OpMemberDecorate, { Block, 0, spv::DecorationOffset, 0 }),
                        instruction(spv::OpMemberDecorate, { Block, 1, spv::DecorationOffset, after }),
                        instruction(spv::OpMemberDecorate, { Block, 2, spv::DecorationOffset, 64 }),
                        instruction(spv::OpTypeFloat, { Float, 32 }), instruction(spv::OpTypeInt, { Uint, 32, 0 }),
                        instruction(spv::OpTypeInt, { Uint64, 64, 0 }), instruction(spv::OpConstant, { Uint, Two, 2 }),
                        instruction(spv::OpTypeStruct, { Inner, Uint64, Uint }),
                        instruction(spv::OpTypeArray, { Array, Inner, Two }),
                        instruction(spv::OpTypeStruct, { Block, Inner, Float, Array }),
                        instruction(spv::OpTypePointer, { Pointer, spv::StorageClassPushConstant, Block }),
                        instruction(spv::OpVariable, { Pointer, Variable, spv::StorageClassPushConstant }) })
                    words.insert(words.end(), each.begin(), each.end());
                return words;
            };

            EXPECT_EQ(readInterface(module(16, 16)).mPushEnd, std::optional<std::uint32_t>(64u + 16u + 12u))
                << "the layout C++ has";
            EXPECT_THROW(readInterface(module(12, 16)), InputError) << "a member in the padding C++ gives a structure";
            EXPECT_THROW(readInterface(module(16, 12)), InputError) << "an array strided short of C++'s";
        }
    }
}
