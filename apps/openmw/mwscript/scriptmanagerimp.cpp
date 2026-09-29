#include "scriptmanagerimp.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <exception>
#include <optional>
#include <sstream>
#include <string_view>

#include <components/debug/debuglog.hpp>

#include <components/esm/refid.hpp>
#include <components/esm3/loadscpt.hpp>

#include <components/misc/strings/lower.hpp>

#include <components/compiler/context.hpp>
#include <components/compiler/exception.hpp>
#include <components/compiler/quickfileparser.hpp>
#include <components/compiler/scanner.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/esmstore.hpp"

#include "extensions.hpp"
#include "interpretercontext.hpp"

namespace MWScript
{
    namespace
    {
        /// The globals and the journal of the game in progress, as a gate reads them.
        class GameReads final : public VisibilityReads
        {
        public:
            char getGlobalType(std::string_view name) const override
            {
                return MWBase::Environment::get().getWorld()->getGlobalVariableType(name);
            }

            int getGlobalInt(std::string_view name) const override
            {
                return MWBase::Environment::get().getWorld()->getGlobalInt(name);
            }

            float getGlobalFloat(std::string_view name) const override
            {
                return MWBase::Environment::get().getWorld()->getGlobalFloat(name);
            }

            int getJournalIndex(const ESM::RefId& quest) const override
            {
                return MWBase::Environment::get().getJournal()->getJournalIndex(quest);
            }
        };
    }

    ScriptManager::ScriptManager(const MWWorld::ESMStore& store, Compiler::Context& compilerContext, int warningsMode)
        : mErrorHandler()
        , mStore(store)
        , mCompilerContext(compilerContext)
        , mParser(mErrorHandler, mCompilerContext)
        , mGlobalScripts(store)
    {
        installOpcodes(mInterpreter);

        mErrorHandler.setWarningsMode(warningsMode);
    }

    bool ScriptManager::compile(const ESM::RefId& name)
    {
        mParser.reset();
        mErrorHandler.reset();

        if (const ESM::Script* script = mStore.get<ESM::Script>().find(name))
        {
            mErrorHandler.setContext(script->mId.getRefIdString());

            bool success = true;
            try
            {
                std::istringstream input(script->mScriptText);

                Compiler::Scanner scanner(mErrorHandler, input, mCompilerContext.getExtensions());

                scanner.scan(mParser);

                if (!mErrorHandler.isGood())
                    success = false;
            }
            catch (const Compiler::SourceException&)
            {
                // error has already been reported via error handler
                success = false;
            }
            catch (const std::exception& error)
            {
                Log(Debug::Error) << "Error: An exception has been thrown: " << error.what();
                success = false;
            }

            if (!success)
            {
                Log(Debug::Error) << "Error: script compiling failed: " << name;
            }

            if (success)
            {
                mScripts.emplace(name, CompiledScript(mParser.getProgram(), mParser.getLocals()));

                return true;
            }
        }

        return false;
    }

    bool ScriptManager::run(const ESM::RefId& name, Interpreter::Context& interpreterContext)
    {
        // compile script
        auto iter = mScripts.find(name);

        if (iter == mScripts.end())
        {
            if (!compile(name))
            {
                // failed -> ignore script from now on.
                mScripts.emplace(name, CompiledScript({}, Compiler::Locals()));
                return false;
            }

            iter = mScripts.find(name);
            assert(iter != mScripts.end());
        }

        // execute script
        const auto& target = interpreterContext.getTarget();
        if (!iter->second.mProgram.mInstructions.empty()
            && iter->second.mInactive.find(target) == iter->second.mInactive.end())
        {
            try
            {
                mInterpreter.run(iter->second.mProgram, interpreterContext);
                return true;
            }
            catch (const MissingImplicitRefError& e)
            {
                Log(Debug::Error) << "Execution of script " << name << " failed: " << e.what();
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Execution of script " << name << " failed: " << e.what();

                iter->second.mInactive.insert(target); // don't execute again.
            }
        }
        return false;
    }

    void ScriptManager::clear()
    {
        for (auto& script : mScripts)
        {
            script.second.mInactive.clear();
        }

        mGlobalScripts.clear();
        mVisibilityGates.reset();
    }

    void ScriptManager::buildVisibilityGates()
    {
        class Compiled final : public GateScripts
        {
        public:
            explicit Compiled(ScriptManager& manager)
                : mManager(manager)
            {
            }

            std::optional<GateScript> compiled(const ESM::RefId& script) override
            {
                auto found = mManager.mScripts.find(script);
                if (found == mManager.mScripts.end() && mManager.compile(script))
                    found = mManager.mScripts.find(script);
                if (found == mManager.mScripts.end())
                    return std::nullopt;

                return GateScript{ .mProgram = &found->second.mProgram, .mLocals = &found->second.mLocals };
            }

        private:
            ScriptManager& mManager;
        };

        Compiled compiled(*this);
        mVisibilityGates.build(mStore, compiled);
        Log(Debug::Info) << "Visibility gates: " << mVisibilityGates.getGateCount()
                         << " scripts decide whether their references stand in the distance";
    }

    void ScriptManager::updateVisibilityGates()
    {
        mGateChanges.clear();
        mVisibilityGates.update(GameReads(), mGateChanges);
        if (mGateChanges.empty())
            return;

        std::size_t open = 0;
        std::size_t undecided = 0;
        for (const GateChange& change : mGateChanges)
        {
            open += change.mState == Terrain::GateState::Open ? 1 : 0;
            undecided += change.mState == Terrain::GateState::Undecided ? 1 : 0;
            MWBase::Environment::get().getWorld()->setVisibilityGate(change.mGate, change.mState);
        }

        // A load or a step of the story, and never a frame's routine.
        Log(Debug::Verbose) << "Visibility gates: " << mGateChanges.size() << " changed, " << open << " open, "
                            << undecided << " undecided, " << mGateChanges.size() - open - undecided << " closed";
        for (const GateChange& change : mGateChanges)
            if (change.mState == Terrain::GateState::Undecided)
                Log(Debug::Verbose) << "Visibility gate undecided: " << mVisibilityGates.getScript(change.mGate);
    }

    std::pair<int, int> ScriptManager::compileAll()
    {
        int count = 0;
        int success = 0;

        for (auto& script : mStore.get<ESM::Script>())
        {
            ++count;

            if (compile(script.mId))
                ++success;
        }

        return std::make_pair(count, success);
    }

    const Compiler::Locals& ScriptManager::getLocals(const ESM::RefId& name)
    {
        {
            auto iter = mScripts.find(name);

            if (iter != mScripts.end())
                return iter->second.mLocals;
        }

        {
            auto iter = mOtherLocals.find(name);

            if (iter != mOtherLocals.end())
                return iter->second;
        }

        if (const ESM::Script* script = mStore.get<ESM::Script>().search(name))
        {
            Compiler::Locals locals;

            const Compiler::ContextOverride override(mErrorHandler, name.getRefIdString() + "[local variables]");

            std::istringstream stream(script->mScriptText);
            Compiler::QuickFileParser parser(mErrorHandler, mCompilerContext, locals);
            Compiler::Scanner scanner(mErrorHandler, stream, mCompilerContext.getExtensions());
            try
            {
                scanner.scan(parser);
            }
            catch (const Compiler::SourceException&)
            {
                // error has already been reported via error handler
                locals.clear();
            }
            catch (const std::exception& error)
            {
                Log(Debug::Error) << "Error: An exception has been thrown: " << error.what();
                locals.clear();
            }

            auto iter = mOtherLocals.emplace(name, locals).first;

            return iter->second;
        }

        throw std::logic_error("script " + name.toDebugString() + " does not exist");
    }

    GlobalScripts& ScriptManager::getGlobalScripts()
    {
        return mGlobalScripts;
    }

    const Compiler::Extensions& ScriptManager::getExtensions() const
    {
        return *mCompilerContext.getExtensions();
    }
}
