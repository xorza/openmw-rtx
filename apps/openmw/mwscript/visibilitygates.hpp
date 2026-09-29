#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <components/compiler/locals.hpp>
#include <components/esm/refid.hpp>
#include <components/interpreter/program.hpp>
#include <components/terrain/objectstorage.hpp>

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
    /// **A gate per script that can enable or disable its own reference.** The player's
    /// strongholds, Raven Rock and Fort Frostmoth place every stage of their building at once and
    /// let each piece's script take down the stages the story has not reached; the Ghostfence's
    /// script takes the fence down once the heart is struck. Such a script runs only while its
    /// cell is active, so a paging that reads the content files stands every stage at once from
    /// afar, and each goes the moment its cell loads. A gate runs the script as a frame of that
    /// cell would (`VisibilityRun`), off the globals and the journal the game holds for every cell,
    /// and runs it again when one of them moves.
    ///
    /// **Per script and not per reference**, because a run starts from what the content files say
    /// of a reference and reads nothing of its own but the locals it starts at nought.
    class VisibilityGates
    {
    public:
        /// Finds every activator, door and container whose own script enables or disables it, and
        /// makes one gate for each such script. At load, before anything asks `gateOf`: the table is
        /// not written again, so the renderers' reader threads read it as it stands.
        void build(const MWWorld::ESMStore& store, GateScripts& scripts);

        /// The gate a record's references stand behind, or `Terrain::sNoGate`.
        std::uint32_t gateOf(const ESM::RefId& record) const;

        std::size_t getGateCount() const { return mGates.size(); }

        /// The script a gate runs, for a report that names it.
        const ESM::RefId& getScript(std::uint32_t gate) const { return mGates[gate].mScript; }

        /// Every gate evaluated again and told again, on the next `update`: a new game or a load.
        void reset();

        /// Runs every gate never run since a `reset`, and every gate one of whose inputs moved, and
        /// appends to `changes` each whose answer is not what it last said.
        void update(const VisibilityReads& reads, std::vector<GateChange>& changes);

    private:
        struct Gate
        {
            ESM::RefId mScript;
            Interpreter::Program mProgram;
            Compiler::Locals mLocals;
            Terrain::GateState mState = Terrain::GateState::Unknown;

            /// What the last run read, with what it read then.
            std::vector<VisibilityInput> mInputs;

            /// The same, as places in `mWatched`.
            std::vector<std::uint32_t> mWatches;

            /// Whether the renderers heard the last answer.
            bool mTold = false;
        };

        /// One value some gate's last run read, and whether this frame's read found it moved.
        struct Watched
        {
            VisibilityInput mInput;
            bool mMoved = false;
        };

        /// Whether a value a gate's last run read is no longer what it read.
        static bool moved(const VisibilityInput& input, const VisibilityReads& reads);

        /// Lists every value the gates read once, and where each gate's are. After a frame on
        /// which a gate ran, which is a load or a step of the story.
        void rewatch();

        std::vector<Gate> mGates;
        std::vector<Watched> mWatched;
        std::unordered_map<ESM::RefId, std::uint32_t> mGateOf;
        VisibilityRun mRun;
    };
}
