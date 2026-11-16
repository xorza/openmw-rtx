#include "locals.hpp"

#include <utility>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/scriptmanager.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/compiler/locals.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/locals.hpp>
#include <components/esm3/variant.hpp>

#include "globalscripts.hpp"

namespace MWScript
{
    void Locals::ensure(const ESM::RefId& scriptName)
    {
        if (!mInitialised)
        {
            const ESM::Script* script = MWBase::Environment::get().getESMStore()->get<ESM::Script>().find(scriptName);

            configure(*script);
        }
    }

    Locals::Locals()
        : mInitialised(false)
    {
    }

    bool Locals::configure(const ESM::Script& script)
    {
        if (mInitialised)
            return false;

        const GlobalScriptDesc* global
            = MWBase::Environment::get().getScriptManager()->getGlobalScripts().getScriptIfPresent(script.mId);
        if (global)
        {
            mShorts = global->mLocals.mShorts;
            mLongs = global->mLocals.mLongs;
            mFloats = global->mLocals.mFloats;
        }
        else
        {
            const Compiler::Locals& locals = MWBase::Environment::get().getScriptManager()->getLocals(script.mId);

            mShorts.clear();
            mShorts.resize(locals.get('s').size(), 0);
            mLongs.clear();
            mLongs.resize(locals.get('l').size(), 0);
            mFloats.clear();
            mFloats.resize(locals.get('f').size(), 0);
        }

        mScriptId = script.mId;
        mInitialised = true;
        return true;
    }

    bool Locals::isEmpty() const
    {
        return (mShorts.empty() && mLongs.empty() && mFloats.empty());
    }

    bool Locals::hasVar(const ESM::RefId& script, std::string_view var)
    {
        ensure(script);

        const Compiler::Locals& locals = MWBase::Environment::get().getScriptManager()->getLocals(script);
        int index = locals.getIndex(var);
        return (index != -1);
    }

    double Locals::getVarAsDouble(const ESM::RefId& script, std::string_view var)
    {
        ensure(script);

        const Compiler::Locals& locals = MWBase::Environment::get().getScriptManager()->getLocals(script);
        int index = locals.getIndex(var);
        if (index == -1)
            return 0;
        switch (locals.getType(var))
        {
            case 's':
                return mShorts.at(static_cast<std::size_t>(index));

            case 'l':
                return mLongs.at(static_cast<std::size_t>(index));

            case 'f':
                return mFloats.at(static_cast<std::size_t>(index));
            default:
                return 0;
        }
    }

    bool Locals::setVar(const ESM::RefId& script, std::string_view var, double val)
    {
        ensure(script);

        const Compiler::Locals& locals = MWBase::Environment::get().getScriptManager()->getLocals(script);
        int index = locals.getIndex(var);
        if (index == -1)
            return false;
        switch (locals.getType(var))
        {
            case 's':
                mShorts.at(static_cast<std::size_t>(index)) = static_cast<Interpreter::Type_Short>(val);
                break;

            case 'l':
                mLongs.at(static_cast<std::size_t>(index)) = static_cast<Interpreter::Type_Integer>(val);
                break;

            case 'f':
                mFloats.at(static_cast<std::size_t>(index)) = static_cast<Interpreter::Type_Float>(val);
                break;
            default:
                break;
        }
        return true;
    }

    std::size_t Locals::getSize(const ESM::RefId& script)
    {
        ensure(script);
        return mShorts.size() + mLongs.size() + mFloats.size();
    }

    bool Locals::write(ESM::Locals& locals, const ESM::RefId& script) const
    {
        if (!mInitialised)
            return false;

        const Compiler::Locals& declarations = MWBase::Environment::get().getScriptManager()->getLocals(script);

        for (int i = 0; i < 3; ++i)
        {
            char type = 0;

            switch (i)
            {
                case 0:
                    type = 's';
                    break;
                case 1:
                    type = 'l';
                    break;
                case 2:
                    type = 'f';
                    break;
                default:
                    break;
            }

            const std::vector<std::string>& names = declarations.get(type);

            for (int i2 = 0; std::cmp_less(i2, names.size()); ++i2)
            {
                ESM::Variant value;

                switch (i)
                {
                    case 0:
                        value.setType(ESM::VT_Int);
                        value.setInteger(mShorts.at(static_cast<std::size_t>(i2)));
                        break;
                    case 1:
                        value.setType(ESM::VT_Int);
                        value.setInteger(mLongs.at(static_cast<std::size_t>(i2)));
                        break;
                    case 2:
                        value.setType(ESM::VT_Float);
                        value.setFloat(mFloats.at(static_cast<std::size_t>(i2)));
                        break;
                    default:
                        break;
                }

                locals.mVariables.emplace_back(names[static_cast<std::size_t>(i2)], value);
            }
        }

        return true;
    }

    void Locals::read(const ESM::Locals& locals, const ESM::RefId& script)
    {
        ensure(script);

        const Compiler::Locals& declarations = MWBase::Environment::get().getScriptManager()->getLocals(script);

        int index = 0, numshorts = 0, numlongs = 0;
        for (const auto& [_, value] : locals.mVariables)
        {
            ESM::VarType type = value.getType();
            if (type == ESM::VT_Short)
                ++numshorts;
            else if (type == ESM::VT_Int)
                ++numlongs;
        }

        for (const auto& [name, value] : locals.mVariables)
        {
            if (name.empty())
            {
                // no variable names available (this will happen for legacy, i.e. ESS-imported savegames only)
                try
                {
                    if (index >= numshorts + numlongs)
                        mFloats.at(static_cast<std::size_t>(index - (numshorts + numlongs))) = value.getFloat();
                    else if (index >= numshorts)
                        mLongs.at(static_cast<std::size_t>(index - numshorts)) = value.getInteger();
                    else
                        mShorts.at(static_cast<std::size_t>(index))
                            = static_cast<Interpreter::Type_Short>(value.getInteger());
                }
                catch (std::exception& e)
                {
                    Log(Debug::Error) << "Failed to read local variable state for script '" << script
                                      << "' (legacy format): " << e.what() << "\nNum shorts: " << numshorts << " / "
                                      << mShorts.size() << " Num longs: " << numlongs << " / " << mLongs.size();
                }
            }
            else
            {
                char type = declarations.getType(name);
                int index2 = declarations.getIndex(name);

                // silently ignore locals that don't exist anymore
                if (type == ' ' || index2 == -1)
                    continue;

                try
                {
                    switch (type)
                    {
                        case 's':
                            mShorts.at(static_cast<std::size_t>(index2))
                                = static_cast<Interpreter::Type_Short>(value.getInteger());
                            break;
                        case 'l':
                            mLongs.at(static_cast<std::size_t>(index2)) = value.getInteger();
                            break;
                        case 'f':
                            mFloats.at(static_cast<std::size_t>(index2)) = value.getFloat();
                            break;
                        default:
                            break;
                    }
                }
                // A local whose type changed keeps its default.
                // NOLINTNEXTLINE(bugprone-empty-catch)
                catch (...)
                {
                    // ignore type changes
                    /// \todo write to log
                }
            }
            ++index;
        }
    }
}
