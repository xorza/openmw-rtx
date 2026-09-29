#ifndef GAME_SCRIPT_SCRIPTMANAGER_H
#define GAME_SCRIPT_SCRIPTMANAGER_H

#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include <components/compiler/fileparser.hpp>
#include <components/compiler/streamerrorhandler.hpp>

#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/types.hpp>

#include <components/esm/refid.hpp>

#include "../mwbase/scriptmanager.hpp"

#include "globalscripts.hpp"
#include "visibilitygates.hpp"

namespace MWWorld
{
    class ESMStore;
}

namespace Compiler
{
    class Context;
}

namespace Interpreter
{
    class Context;
    class Interpreter;
}

namespace MWScript
{
    class ScriptManager : public MWBase::ScriptManager
    {
        Compiler::StreamErrorHandler mErrorHandler;
        const MWWorld::ESMStore& mStore;
        Compiler::Context& mCompilerContext;
        Compiler::FileParser mParser;
        Interpreter::Interpreter mInterpreter;

        struct CompiledScript
        {
            Interpreter::Program mProgram;
            Compiler::Locals mLocals;
            std::set<ESM::RefId> mInactive;

            explicit CompiledScript(Interpreter::Program&& program, const Compiler::Locals& locals)
                : mProgram(std::move(program))
                , mLocals(locals)
            {
            }
        };

        std::unordered_map<ESM::RefId, CompiledScript> mScripts;
        GlobalScripts mGlobalScripts;
        std::unordered_map<ESM::RefId, Compiler::Locals> mOtherLocals;

        VisibilityGates mVisibilityGates;

        // Refilled by every `updateVisibilityGates`.
        std::vector<GateChange> mGateChanges;

    public:
        ScriptManager(const MWWorld::ESMStore& store, Compiler::Context& compilerContext, int warningsMode);

        void clear() override;

        bool run(const ESM::RefId& name, Interpreter::Context& interpreterContext) override;
        ///< Run the script with the given name (compile first, if not compiled yet)

        bool compile(const ESM::RefId& name) override;
        ///< Compile script with the given namen
        /// \return Success?

        std::pair<int, int> compileAll() override;
        ///< Compile all scripts
        /// \return count, success

        const Compiler::Locals& getLocals(const ESM::RefId& name) override;
        ///< Return locals for script \a name.

        GlobalScripts& getGlobalScripts() override;

        void markVisibilityGates(std::span<Terrain::PagedCellRef> cell) const override { mVisibilityGates.mark(cell); }

        /// Makes the gates, once the content is loaded and before a renderer reads a cell.
        void buildVisibilityGates();

        /// Runs the gates whose inputs moved and tells the world what they now say. Once a frame
        /// while a game is on.
        void updateVisibilityGates();

        const Compiler::Extensions& getExtensions() const override;
    };
}

#endif
