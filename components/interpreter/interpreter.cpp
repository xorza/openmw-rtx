#include "interpreter.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

#include "opcodes.hpp"
#include "program.hpp"

namespace Interpreter
{
    namespace
    {
        [[noreturn]] void abortUnknownCode(std::int32_t segment, std::int32_t opcode)
        {
            const std::string error = std::format("unknown opcode {} in segment {}", opcode, segment);
            throw std::runtime_error(error);
        }

        [[noreturn]] void abortUnknownSegment(Type_Code code)
        {
            const std::string error = std::format("opcode outside of the allocated segment range: {}", code);
            throw std::runtime_error(error);
        }

        template <typename T>
        auto& getDispatcher(const T& segment, std::uint32_t seg, std::int32_t opcode)
        {
            auto it = segment.find(opcode);
            if (it == segment.end())
            {
                abortUnknownCode(static_cast<std::int32_t>(seg), opcode);
            }
            return it->second;
        }
    }

    [[noreturn]] void Interpreter::abortDuplicateInstruction(std::string_view name, std::int32_t code)
    {
        throw std::invalid_argument(
            std::format("Duplicated interpreter instruction code in segment {}: {:#x}", name, code));
    }

    void Interpreter::execute(Type_Code code)
    {
        std::uint32_t segSpec = code >> 30;

        switch (segSpec)
        {
            case 0:
            {
                const auto opcode = static_cast<std::int32_t>(code >> 24);
                const std::uint32_t arg0 = code & 0xffffff;

                return getDispatcher(mSegment0, 0, opcode)->execute(mRuntime, arg0);
            }

            case 2:
            {
                const auto opcode = static_cast<std::int32_t>((code >> 20) & 0x3ff);
                const std::uint32_t arg0 = code & 0xfffff;

                return getDispatcher(mSegment2, 2, opcode)->execute(mRuntime, arg0);
            }
            default:
                break;
        }

        segSpec = code >> 26;

        switch (segSpec)
        {
            case 0x30:
            {
                const auto opcode = static_cast<std::int32_t>((code >> 8) & 0x3ffff);
                const std::uint32_t arg0 = code & 0xff;

                return getDispatcher(mSegment3, 3, opcode)->execute(mRuntime, arg0);
            }

            case 0x32:
            {
                const auto opcode = static_cast<std::int32_t>(code & 0x3ffffff);

                return getDispatcher(mSegment5, 5, opcode)->execute(mRuntime);
            }
            default:
                break;
        }

        abortUnknownSegment(code);
    }

    void Interpreter::begin()
    {
        if (mRunning)
        {
            mCallstack.push(mRuntime);
            mRuntime.clear();
        }
        else
        {
            mRunning = true;
        }
    }

    void Interpreter::end()
    {
        if (mCallstack.empty())
        {
            mRuntime.clear();
            mRunning = false;
        }
        else
        {
            mRuntime = mCallstack.top();
            mCallstack.pop();
        }
    }

    void Interpreter::run(const Program& program, Context& context)
    {
        begin();

        try
        {
            mRuntime.configure(program, context);

            while (mRuntime.getPC() >= 0 && static_cast<std::size_t>(mRuntime.getPC()) < program.mInstructions.size())
            {
                const Type_Code instruction = program.mInstructions[static_cast<std::size_t>(mRuntime.getPC())];
                mRuntime.setPC(mRuntime.getPC() + 1);
                execute(instruction);
            }
        }
        catch (...)
        {
            end();
            throw;
        }

        end();
    }
}
