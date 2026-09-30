#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <components/compiler/context.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/compiler/fileparser.hpp>
#include <components/compiler/scanner.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/interpreter/program.hpp>
#include <components/terrain/pagedcellref.hpp>

#include "apps/openmw/mwscript/visibilitygates.hpp"
#include "apps/openmw/mwscript/visibilityrun.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"

#include "testutils.hpp"

namespace
{
    using Terrain::GateState;

    // Vanilla Morrowind's own, word for word: a stage of the player's stronghold, and the
    // Ghostfence.
    constexpr std::string_view sStageScript = R"(begin Strong1_Construct

;enables phase one construction clutter for the stronghold

if ( Stronghold ==  1 )

	if ( GetDisabled == 1 )
		enable
	endif

else

	if ( GetDisabled == 0 )
		disable
	endif

endif

end Strong1_Construct
)";

    constexpr std::string_view sFenceScript = R"(Begin GhostfenceScript

if ( GetDisabled == 1 )
	Return
endif

;this journal entry gets set when heart is wacked and akul falls apart
if ( GetJournalIndex, C3_DestroyDagoth >= 20 )
	Disable
	stopsound "ghostgate sound"

else

	if ( CellChanged == 0 )
		if ( GetSoundPlaying "ghostgate sound" == 0 )
			playloopsound3dvp "ghostgate sound" 1.0 1.0
		endif
	endif

endif

End
)";

    /// A compiler context that knows the globals and the reference the scripts here name.
    class GlobalsContext final : public TestCompilerContext
    {
    public:
        char getGlobalType(const std::string& name) const override
        {
            return name == "stronghold" || name == "flag" ? 's' : ' ';
        }

        bool isId(const ESM::RefId& name) const override
        {
            return name == "player" || name == "plank" || name == "plank_b";
        }
    };

    class FakeReads final : public MWScript::VisibilityReads
    {
    public:
        char getGlobalType(std::string_view name) const override
        {
            return mGlobals.contains(std::string(name)) ? 's' : ' ';
        }

        int getGlobalInt(std::string_view name) const override
        {
            const auto found = mGlobals.find(std::string(name));
            return found != mGlobals.end() ? found->second : 0;
        }

        float getGlobalFloat(std::string_view name) const override { return static_cast<float>(getGlobalInt(name)); }

        int getJournalIndex(const ESM::RefId& quest) const override
        {
            const auto found = mJournal.find(quest.getRefIdString());
            return found != mJournal.end() ? found->second : 0;
        }

        std::map<std::string, int> mGlobals;
        std::map<std::string, int> mJournal;
    };

    struct Compiled
    {
        Interpreter::Program mProgram;
        Compiler::Locals mLocals;
    };

    class VisibilityGatesTest : public ::testing::Test
    {
    public:
        VisibilityGatesTest()
            : mParser(mErrors, mContext)
        {
            Compiler::registerExtensions(mExtensions);
            mContext.setExtensions(&mExtensions);
        }

        Compiled compile(std::string_view text)
        {
            mParser.reset();
            mErrors.reset();
            std::istringstream input{ std::string(text) };
            Compiler::Scanner scanner(mErrors, input, mContext.getExtensions());
            scanner.scan(mParser);
            EXPECT_TRUE(mErrors.isGood()) << text;
            return Compiled{ .mProgram = mParser.getProgram(), .mLocals = mParser.getLocals() };
        }

        GateState run(std::string_view text, std::vector<MWScript::VisibilityInput>& inputs)
        {
            const Compiled compiled = compile(text);
            inputs.clear();
            return mRun.run(compiled.mProgram, compiled.mLocals, mReads, inputs, mNamed);
        }

        /// What the last `run` left the one name it toggled, or nothing where it named none.
        std::optional<GateState> plank() const
        {
            if (mNamed.empty())
                return std::nullopt;
            EXPECT_EQ(mNamed.size(), 1u);
            EXPECT_EQ(mNamed[0].mName, ESM::RefId::stringRefId("plank"));
            return mNamed[0].mState;
        }

        TestErrorHandler mErrors;
        GlobalsContext mContext;
        Compiler::Extensions mExtensions;
        Compiler::FileParser mParser;
        MWScript::VisibilityRun mRun;
        FakeReads mReads;
        std::vector<MWScript::VisibilityNamed> mNamed;
    };

    /// **A run answers what a reference's frames in an active cell would leave it as.** A
    /// stronghold's stage is up where the global names its stage and down everywhere else, and
    /// says it read the global; the Ghostfence stands until the heart is struck — whether the cell
    /// just changed and whether its sound plays decide only the sound — and once the journal
    /// passes 20 it is down.
    TEST_F(VisibilityGatesTest, aRunAnswersWhatTheReferencesFramesInAnActiveCellWouldLeaveItAs)
    {
        std::vector<MWScript::VisibilityInput> inputs;

        struct Row
        {
            int mStronghold;
            GateState mState;
        };
        for (const Row& row : { Row{ 0, GateState::Closed }, Row{ 1, GateState::Open }, Row{ 2, GateState::Closed } })
        {
            mReads.mGlobals["stronghold"] = row.mStronghold;
            EXPECT_EQ(run(sStageScript, inputs), row.mState) << "Stronghold " << row.mStronghold;
            ASSERT_EQ(inputs.size(), 1u);
            EXPECT_EQ(inputs[0].mRead, (std::variant<std::string, ESM::RefId>(std::string("stronghold"))));
            EXPECT_EQ(inputs[0].mValue, row.mStronghold);
        }

        mReads.mJournal["c3_destroydagoth"] = 10;
        EXPECT_EQ(run(sFenceScript, inputs), GateState::Open) << "the heart is not struck";
        ASSERT_EQ(inputs.size(), 1u) << "read under every answer to the cell change and the sound, and kept once";
        EXPECT_EQ(
            inputs[0].mRead, (std::variant<std::string, ESM::RefId>(ESM::RefId::stringRefId("c3_destroydagoth"))));
        EXPECT_EQ(inputs[0].mValue, 10);

        mReads.mJournal["c3_destroydagoth"] = 20;
        EXPECT_EQ(run(sFenceScript, inputs), GateState::Closed) << "struck: down, whatever the sound";
    }

    /// **Nothing a run does reaches the game, and what it cannot see it does not guess.** A global
    /// a run sets is its own and read back as set, and only the read before the write is an
    /// input; `GetDisabled` reads the run's own answer; an activation decides nothing where the
    /// reference stands the same either way, and leaves it undecided where it does not; an
    /// instruction nobody modelled is undecided too — but only on the path the run takes. A name
    /// the script enables or disables is decided apart from the reference: by its last word, on
    /// every path alike, and undecided where a path leaves it alone or another says otherwise.
    TEST_F(VisibilityGatesTest, aRunKeepsWhatItWritesAndDoesNotGuessWhatItCannotSee)
    {
        std::vector<MWScript::VisibilityInput> inputs;
        mReads.mGlobals["flag"] = 0;

        EXPECT_EQ(
            run("begin s\nif ( flag == 0 )\nset flag to 1\nendif\nif ( flag == 1 )\ndisable\nendif\nend s\n", inputs),
            GateState::Closed);
        EXPECT_EQ(mReads.mGlobals["flag"], 0) << "the game's global is untouched";
        ASSERT_EQ(inputs.size(), 1u) << "the read after the write is the run's own";
        EXPECT_EQ(inputs[0].mValue, 0);

        EXPECT_EQ(run("begin s\ndisable\nif ( GetDisabled == 1 )\nenable\nendif\nend s\n", inputs), GateState::Open);
        EXPECT_EQ(run("begin s\nif ( OnActivate == 1 )\ndisable\nendif\nend s\n", inputs), GateState::Undecided)
            << "taken down by an activation only its cell has seen";
        EXPECT_EQ(run("begin s\ndisable\nif ( OnActivate == 1 )\nmessagebox \"closed\"\nendif\nend s\n", inputs),
            GateState::Closed)
            << "an activation that only shows a message";
        EXPECT_EQ(run("begin s\nif ( CellChanged == 1 )\nif ( OnActivate == 1 )\nenable\nendif\nendif\ndisable\n"
                      "end s\n",
                      inputs),
            GateState::Closed)
            << "an event asked only under another's answer is tried too, and still decides nothing";
        EXPECT_EQ(
            run("begin s\nif ( CellChanged == 1 )\nif ( OnActivate == 1 )\ndisable\nendif\nendif\nend s\n", inputs),
            GateState::Undecided)
            << "and where it does decide, under that answer alone, the run says so";
        EXPECT_EQ(run("begin s\nif ( flag == 0 )\nlock 50\nendif\nend s\n", inputs), GateState::Undecided)
            << "an instruction the run does not model, on its path";
        EXPECT_EQ(run("begin s\nif ( flag == 1 )\nlock 50\nendif\ndisable\nend s\n", inputs), GateState::Closed)
            << "the same instruction off its path";
        EXPECT_EQ(run("begin s\nif ( MenuMode == 1 )\nreturn\nendif\nstopscript s\nend s\n", inputs), GateState::Open)
            << "a menu and a stopped script say nothing of the reference, which stands as the files have it";
        EXPECT_EQ(run("begin s\n\"plank\"->enable\ndisable\nend s\n", inputs), GateState::Closed)
            << "another reference enabled by name says nothing of this one";
        EXPECT_EQ(plank(), GateState::Open) << "and is enabled";
        run("begin s\n\"plank\"->disable\n\"plank\"->enable\nend s\n", inputs);
        EXPECT_EQ(plank(), GateState::Open) << "the last word";
        EXPECT_EQ(run("begin s\nif ( OnActivate == 1 )\n\"plank\"->disable\nendif\nend s\n", inputs), GateState::Open);
        EXPECT_EQ(plank(), GateState::Undecided) << "a path that leaves it alone leaves it to its history";
        run("begin s\nif ( CellChanged == 1 )\n\"plank\"->enable\nelse\n\"plank\"->disable\nendif\nend s\n", inputs);
        EXPECT_EQ(plank(), GateState::Undecided) << "two paths that say otherwise";
        run("begin s\nif ( OnActivate == 1 )\n\"plank\"->disable\nelse\n\"plank\"->disable\nendif\nend s\n", inputs);
        EXPECT_EQ(plank(), GateState::Closed) << "every path alike";
        EXPECT_EQ(run("begin s\n\"plank\"->disable\nlock 50\nend s\n", inputs), GateState::Undecided);
        EXPECT_EQ(plank(), std::nullopt) << "a run that could not finish decides no name";
        EXPECT_EQ(
            run("begin s\nif ( \"plank\"->GetDisabled == 1 )\ndisable\nendif\nend s\n", inputs), GateState::Undecided)
            << "another reference's state is one only its loaded cell has";
    }

    /// **A gate for the references a script toggles: its own, however many records wear it, and
    /// each it names beside it.** Two activators and a door wear the stage's script and share its
    /// gate; a container wears the Ghostfence's; the next stage's hall reads the same global as the
    /// first stage; Thirsk's door names two planks, a gate each, and the player, an actor the
    /// distance never shows and so no gate; an activator whose script only plays a sound, and one
    /// with no script, stand behind none. A plank stands behind its gate in
    /// the door's cell, and nowhere else: not alone, and not as one of two planks of a record. A
    /// run reports every gate once, and after that only a gate whose input moved and whose answer
    /// changed — both stages run when the global moves, and each reports only where it changed;
    /// the planks follow the hall's journal, one decided on each path and the other left alone on
    /// one; a reset reports them all again, for the renderers a new game clears.
    TEST_F(VisibilityGatesTest, aScriptGatesWhatItTogglesBesideItAndMovesWithItsInputs)
    {
        MWWorld::ESMStore store;
        const auto script = [&](std::string_view id, std::string_view text) {
            ESM::Script record;
            record.mId = ESM::RefId::stringRefId(id);
            record.mScriptText = std::string(text);
            store.insertStatic(record);
        };
        script("strong1_construct", sStageScript);
        script("ghostfencescript", sFenceScript);
        script("chimes", "begin chimes\nplaysound \"chimes\"\nend chimes\n");
        script("stage_two", "begin stage_two\nif ( Stronghold == 2 )\nenable\nelse\ndisable\nendif\nend stage_two\n");
        script("thirsk",
            "begin thirsk\nif ( GetJournalIndex bm_meadhall < 40 )\n\"plank\"->disable\n\"plank_b\"->disable\nelse\n"
            "\"plank\"->enable\nendif\n\"player\"->disable\nend thirsk\n");

        const auto wears = [](std::string_view id, std::string_view worn) {
            ESM::Activator record;
            record.mId = ESM::RefId::stringRefId(id);
            record.mScript = worn.empty() ? ESM::RefId() : ESM::RefId::stringRefId(worn);
            return record;
        };
        store.insertStatic(wears("stage_a", "strong1_construct"));
        store.insertStatic(wears("stage_b", "strong1_construct"));
        store.insertStatic(wears("chime", "chimes"));
        store.insertStatic(wears("rock", ""));
        store.insertStatic(wears("hall", "stage_two"));
        ESM::Door door;
        door.mId = ESM::RefId::stringRefId("stage_door");
        door.mScript = ESM::RefId::stringRefId("strong1_construct");
        store.insertStatic(door);
        ESM::Door hallDoor;
        hallDoor.mId = ESM::RefId::stringRefId("hall_door");
        hallDoor.mScript = ESM::RefId::stringRefId("thirsk");
        store.insertStatic(hallDoor);
        ESM::Container fence;
        fence.mId = ESM::RefId::stringRefId("fence_crate");
        fence.mScript = ESM::RefId::stringRefId("ghostfencescript");
        store.insertStatic(fence);
        for (const std::string_view plank : { "plank", "plank_b" })
        {
            ESM::Static board;
            board.mId = ESM::RefId::stringRefId(plank);
            store.insertStatic(board);
        }
        store.setUp();

        class FromStore final : public MWScript::GateScripts
        {
        public:
            FromStore(VisibilityGatesTest& test, const MWWorld::ESMStore& store)
                : mTest(test)
                , mStore(store)
            {
            }

            std::optional<MWScript::GateScript> compiled(const ESM::RefId& id) override
            {
                const ESM::Script* record = mStore.get<ESM::Script>().search(id);
                if (record == nullptr)
                    return std::nullopt;
                mKept.push_back(mTest.compile(record->mScriptText));
                ++mCompiled;
                return MWScript::GateScript{ .mProgram = &mKept.back().mProgram, .mLocals = &mKept.back().mLocals };
            }

            std::size_t mCompiled = 0;

        private:
            VisibilityGatesTest& mTest;
            const MWWorld::ESMStore& mStore;
            std::deque<Compiled> mKept;
        };

        FromStore scripts(*this, store);
        MWScript::VisibilityGates gates;
        gates.build(store, scripts);

        EXPECT_EQ(gates.getGateCount(), 5u)
            << "three scripts' own references, and the two planks: the player is no record the distance shows";
        EXPECT_EQ(scripts.mCompiled, 4u) << "the chimes' text names neither instruction, so it is never compiled";

        // The gates of one cell's references, in the order given.
        const auto cell = [&](std::initializer_list<std::string_view> records) {
            std::vector<Terrain::PagedCellRef> refs;
            for (const std::string_view record : records)
                refs.push_back(Terrain::PagedCellRef{ .mRefId = ESM::RefId::stringRefId(record) });
            gates.mark(refs);
            std::vector<std::uint32_t> marked;
            for (const Terrain::PagedCellRef& ref : refs)
                marked.push_back(ref.mGate);
            return marked;
        };

        const std::vector<std::uint32_t> worn
            = cell({ "stage_a", "stage_b", "stage_door", "fence_crate", "hall", "chime", "rock", "hall_door" });
        const std::uint32_t stage = worn[0];
        const std::uint32_t ghost = worn[3];
        const std::uint32_t hall = worn[4];
        EXPECT_NE(stage, Terrain::sNoGate);
        EXPECT_NE(ghost, Terrain::sNoGate);
        EXPECT_NE(hall, Terrain::sNoGate);
        EXPECT_NE(stage, ghost);
        EXPECT_EQ(worn[1], stage);
        EXPECT_EQ(worn[2], stage);
        EXPECT_EQ(worn[5], Terrain::sNoGate) << "a script that plays a sound";
        EXPECT_EQ(worn[6], Terrain::sNoGate) << "no script";
        EXPECT_EQ(worn[7], Terrain::sNoGate) << "the door's script names others and leaves the door alone";

        const std::vector<std::uint32_t> beside = cell({ "plank", "hall_door", "plank_b" });
        const std::uint32_t plank = beside[0];
        const std::uint32_t plankB = beside[2];
        EXPECT_NE(plank, Terrain::sNoGate);
        EXPECT_NE(plankB, Terrain::sNoGate);
        EXPECT_NE(plank, plankB);
        EXPECT_EQ(cell({ "plank" })[0], Terrain::sNoGate) << "with no door in its cell";
        EXPECT_EQ(cell({ "plank", "hall_door", "plank" }),
            (std::vector<std::uint32_t>{ Terrain::sNoGate, Terrain::sNoGate, Terrain::sNoGate }))
            << "two planks of one record, and which the door means is the game's to say";

        std::vector<MWScript::GateChange> changes;
        mReads.mGlobals["stronghold"] = 0;
        mReads.mJournal["c3_destroydagoth"] = 0;
        gates.update(mReads, changes);
        ASSERT_EQ(changes.size(), 5u) << "every gate, once";

        changes.clear();
        gates.update(mReads, changes);
        EXPECT_TRUE(changes.empty()) << "nothing moved";

        mReads.mGlobals["stronghold"] = 1;
        gates.update(mReads, changes);
        ASSERT_EQ(changes.size(), 1u) << "both stages ran, and the hall's answer did not change";
        EXPECT_EQ(changes[0].mGate, stage);
        EXPECT_EQ(changes[0].mState, GateState::Open);

        changes.clear();
        mReads.mGlobals["stronghold"] = 2;
        gates.update(mReads, changes);
        ASSERT_EQ(changes.size(), 2u) << "the one global moves two gates";
        EXPECT_EQ(changes[0].mGate, stage);
        EXPECT_EQ(changes[0].mState, GateState::Closed);
        EXPECT_EQ(changes[1].mGate, hall);
        EXPECT_EQ(changes[1].mState, GateState::Open);

        changes.clear();
        mReads.mJournal["c3_destroydagoth"] = 20;
        gates.update(mReads, changes);
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].mGate, ghost);
        EXPECT_EQ(changes[0].mState, GateState::Closed);

        changes.clear();
        mReads.mJournal["bm_meadhall"] = 50;
        gates.update(mReads, changes);
        ASSERT_EQ(changes.size(), 2u) << "the planks";
        EXPECT_EQ(changes[0].mGate, plank);
        EXPECT_EQ(changes[0].mState, GateState::Open);
        EXPECT_EQ(changes[1].mGate, plankB);
        EXPECT_EQ(changes[1].mState, GateState::Undecided) << "left alone on the hall's path past 40";

        changes.clear();
        gates.reset();
        gates.update(mReads, changes);
        EXPECT_EQ(changes.size(), 5u) << "a reset tells every gate again";
    }
}
