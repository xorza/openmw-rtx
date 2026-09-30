#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <components/compiler/locals.hpp>
#include <components/esm/refid.hpp>
#include <components/interpreter/program.hpp>
#include <components/terrain/pagedcellref.hpp>

#include "visibilityrun.hpp"

namespace MWWorld
{
    class ESMStore;
}

namespace MWScript
{
    /// A script as a gate runs it: its code and its locals, as the script manager compiled them.
    struct GateScript
    {
        const Interpreter::Program* mProgram = nullptr;
        const Compiler::Locals* mLocals = nullptr;
    };

    /// Where the gates' scripts are compiled: the script manager, or a test.
    class GateScripts
    {
    public:
        virtual ~GateScripts() = default;

        /// The compiled script, or nothing where it does not compile.
        virtual std::optional<GateScript> compiled(const ESM::RefId& script) = 0;
    };

    /// A gate that changed what it says, for the renderers.
    struct GateChange
    {
        std::uint32_t mGate = Terrain::sNoGate;
        Terrain::GateState mState = Terrain::GateState::Unknown;
    };

    /// Whether the distance stands the references a script decides on, for the cells no one has
    /// loaded — where the script has never run, and what the content files say is not what the
    /// game shows.
    ///
    /// **A gate for every reference a local script enables or disables as its cell loads.** The
    /// player's strongholds, Raven Rock and Fort Frostmoth place every stage of their building at
    /// once and let each piece's own script take down the stages the story has not reached; the
    /// Ghostfence's script takes the fence down once the heart is struck; Thirsk's door puts up
    /// the hall's planks by name, and each of Solstheim's standing stones its parts. Such a script
    /// runs only while its cell is active, so a paging that reads the content files stands every
    /// stage at once from afar, and each goes the moment its cell loads. A gate runs the script as
    /// a frame of that cell would (`VisibilityRun`), off the globals and the journal the game
    /// holds for every cell, and runs it again when one of them moves.
    ///
    /// **Per script and not per reference**, because a run starts from what the content files say
    /// of a reference and reads nothing of its own but the locals it starts at nought: a script
    /// has a gate for the references that wear it, and one for each name it enables or disables.
    class VisibilityGates
    {
    public:
        /// Finds every activator, door, container and lamp whose script enables or disables a
        /// reference — its own or one it names — and makes the gates. At load, before anything asks
        /// `mark`: the tables are not written again, so the renderers' reader threads read them as
        /// they stand.
        void build(const MWWorld::ESMStore& store, GateScripts& scripts);

        /// Marks each reference of one cell with the gate it stands behind, or `Terrain::sNoGate`.
        ///
        /// **A named reference only where the script that names it stands in the same cell, and
        /// only where it is the one reference of its record there.** The game finds a name among
        /// the active cells first, so a script and a name in one cell load together and the name
        /// changes on the frame both arrive; a name elsewhere is found, changed and told of by the
        /// game when the script runs, or not found and left as the files have it, and the
        /// distance agrees either way. Neighbouring cells load together on some approaches and not
        /// others, and a second reference of the record makes which one the script means the
        /// game's to say: both stand by the game's word. What the script is worn by counts only
        /// where the cell holds it still — an actor starts where the files put it and walks off.
        void mark(std::span<Terrain::PagedCellRef> cell) const;

        std::size_t getGateCount() const { return mGates.size(); }
        std::size_t getScriptCount() const { return mScripts.size(); }

        /// The script a gate runs, and the name it decides where it is not the script's own
        /// reference, for a report.
        std::string describe(std::uint32_t gate) const;

        /// Every gate evaluated again and told again, on the next `update`: a new game or a load.
        void reset();

        /// Runs every script never run since a `reset`, and every script one of whose inputs moved,
        /// and appends to `changes` each gate whose answer is not what it last said.
        void update(const VisibilityReads& reads, std::vector<GateChange>& changes);

    private:
        struct Script
        {
            ESM::RefId mId;
            Interpreter::Program mProgram;
            Compiler::Locals mLocals;

            /// The gate of the references that wear it, where it enables or disables them.
            std::uint32_t mOwnGate = Terrain::sNoGate;

            /// A gate for each record it enables or disables by name: a run of `mGates` from
            /// `mFirstNamed`, each gate carrying its name.
            std::uint32_t mFirstNamed = 0;
            std::uint32_t mNamedCount = 0;

            /// What the last run read, with what it read then.
            std::vector<VisibilityInput> mInputs;

            /// The same, as places in `mWatched`.
            std::vector<std::uint32_t> mWatches;

            /// Whether it has run since the last `reset`.
            bool mRun = false;
        };

        struct Gate
        {
            std::uint32_t mScript = 0;

            /// The record it decides, or none where it decides the script's own references.
            ESM::RefId mName;

            Terrain::GateState mState = Terrain::GateState::Unknown;

            /// Whether the renderers heard the last answer.
            bool mTold = false;
        };

        /// One name some script enables or disables, and the gate: kept sorted by name, so a cell's
        /// reference finds what names it by one search. The script is the gate's.
        struct NamedBy
        {
            ESM::RefId mName;
            std::uint32_t mGate = Terrain::sNoGate;
        };

        /// One value some script's last run read, and whether this frame's read found it moved.
        struct Watched
        {
            VisibilityInput mInput;
            bool mMoved = false;
        };

        /// Whether a value a script's last run read is no longer what it read.
        static bool moved(const VisibilityInput& input, const VisibilityReads& reads);

        /// Records a gate's answer, and appends it to `changes` where the renderers have not heard
        /// it.
        void settle(std::uint32_t gate, Terrain::GateState state, std::vector<GateChange>& changes);

        /// Lists every value the scripts read once, and where each script's are. After a frame on
        /// which a script ran, which is a load or a step of the story.
        void rewatch();

        std::vector<Script> mScripts;
        std::vector<Gate> mGates;

        /// What a record's references wear: the script, and its gate for them. Apart from
        /// `mScripts`, whose runs `update` writes while a reader thread marks a cell.
        struct Worn
        {
            std::uint32_t mScript = 0;
            std::uint32_t mOwnGate = Terrain::sNoGate;
        };

        std::unordered_map<ESM::RefId, Worn> mWornBy;

        std::vector<NamedBy> mNamedBy;
        std::vector<Watched> mWatched;

        /// Whether a script has not run since the last `build` or `reset`.
        bool mUnrun = true;
        VisibilityRun mRun;

        // Refilled by every run.
        std::vector<VisibilityNamed> mRunNamed;
    };
}
