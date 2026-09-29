#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/terrain/objectstorage.hpp>

namespace Compiler
{
    class Locals;
}

namespace Interpreter
{
    struct Program;
}

namespace MWScript
{
    /// What a visibility run may read of the game: its globals and its journal. An interface, so a
    /// test answers without a world.
    class VisibilityReads
    {
    public:
        virtual ~VisibilityReads() = default;

        /// The global's type as `MWBase::World::getGlobalVariableType` spells it: `s`, `l`, `f`, or
        /// a space where there is none.
        virtual char getGlobalType(std::string_view name) const = 0;

        virtual int getGlobalInt(std::string_view name) const = 0;
        virtual float getGlobalFloat(std::string_view name) const = 0;
        virtual int getJournalIndex(const ESM::RefId& quest) const = 0;

        /// A global's value whatever its type, exactly: a double holds a long whole.
        double getGlobal(std::string_view name) const
        {
            return getGlobalType(name) == 'f' ? getGlobalFloat(name) : getGlobalInt(name);
        }
    };

    /// One value a run read, and what it was: a gate is evaluated again when one of these moves.
    struct VisibilityInput
    {
        enum class Kind
        {
            Global,
            Journal,
        };

        Kind mKind = Kind::Global;

        /// The global's name, where it is one.
        std::string mGlobal;

        /// The quest, where it is a journal entry.
        ESM::RefId mQuest;

        /// Doubles, so a long global compares exactly.
        double mValue = 0.0;
    };

    /// Runs a reference's own script as a frame in an active cell would, and answers whether the
    /// reference stands after it — for a reference in a cell no one has loaded, whose script has
    /// never run.
    ///
    /// **An interpreter of its own, with only the instructions it models.** Control flow,
    /// arithmetic, locals and globals are the interpreter's own; `Enable`, `Disable` and
    /// `GetDisabled` act on the run's answer; `GetJournalIndex` reads the journal; `StopScript`,
    /// the sound instructions and an `Enable` or `Disable` of another reference do nothing,
    /// because none of them says anything about this one. An activation, a cell change, an open
    /// menu and a playing sound are what only a frame of an active cell knows, and the script
    /// is run under every answer to the ones it asks: the reference's answer is the run's where
    /// they all agree, and `Undecided` where they do not. Every other instruction is one the run
    /// does not model, and makes the answer `Undecided` too. Nothing the run does reaches the
    /// game: a global it sets is its own for the rest of the run.
    class VisibilityRun
    {
    public:
        VisibilityRun();

        VisibilityRun(const VisibilityRun&) = delete;
        VisibilityRun& operator=(const VisibilityRun&) = delete;

        /// Runs `program` over a reference the content files stand, with every local at nought,
        /// and appends to `inputs` every global and journal entry it read.
        Terrain::GateState run(const Interpreter::Program& program, const Compiler::Locals& locals,
            const VisibilityReads& reads, std::vector<VisibilityInput>& inputs);

    private:
        Interpreter::Interpreter mInterpreter;
    };
}
