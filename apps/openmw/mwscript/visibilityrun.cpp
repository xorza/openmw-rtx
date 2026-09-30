#include "visibilityrun.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <components/compiler/locals.hpp>
#include <components/compiler/opcodes.hpp>
#include <components/interpreter/context.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/opcodes.hpp>
#include <components/interpreter/runtime.hpp>

namespace MWScript
{
    namespace
    {
        /// What makes a run's answer `Undecided`: the script asked for something only an active
        /// cell has.
        struct Undecided
        {
        };

        /// What a script may ask whose answer only a frame of an active cell has: whether the
        /// reference was activated, whether the player changed cells, whether a menu is open, and
        /// whether a sound plays. A run answers each one way, and `VisibilityRun::run` tries every
        /// way the script asked for.
        enum class Event : unsigned
        {
            Activated,
            CellChanged,
            MenuOpen,
            SoundPlaying,
            Count,
        };

        /// The run's world: the reference's answer, its locals, and the globals it set, none of
        /// which reach the game.
        class RunContext final : public Interpreter::Context
        {
        public:
            /// @param events one bit an `Event`: what each answers in this run.
            RunContext(const Compiler::Locals& locals, const VisibilityReads& reads,
                std::vector<VisibilityInput>& inputs, std::vector<VisibilityNamed>& named, unsigned events)
                : mEvents(events)
                , mShorts(locals.get('s').size(), 0)
                , mLongs(locals.get('l').size(), 0)
                , mFloats(locals.get('f').size(), 0.0f)
                , mReads(reads)
                , mInputs(inputs)
                , mNamed(named)
            {
            }

            bool isEnabled() const { return mEnabled; }
            void setEnabled(bool enabled) { mEnabled = enabled; }

            void setNamed(const ESM::RefId& name, bool enabled)
            {
                const Terrain::GateState state = enabled ? Terrain::GateState::Open : Terrain::GateState::Closed;
                const auto known = std::find_if(
                    mNamed.begin(), mNamed.end(), [&](const VisibilityNamed& named) { return named.mName == name; });
                if (known != mNamed.end())
                    known->mState = state;
                else
                    mNamed.push_back(VisibilityNamed{ .mName = name, .mState = state });
            }

            int ask(Event event)
            {
                const unsigned bit = 1u << static_cast<unsigned>(event);
                mAsked |= bit;
                return (mEvents & bit) != 0 ? 1 : 0;
            }

            /// Which events the run asked, one bit each.
            unsigned getAsked() const { return mAsked; }

            int readJournal(const ESM::RefId& quest)
            {
                const int index = mReads.getJournalIndex(quest);
                if (std::none_of(mInputs.begin(), mInputs.end(), [&](const VisibilityInput& input) {
                        const ESM::RefId* read = std::get_if<ESM::RefId>(&input.mRead);
                        return read != nullptr && *read == quest;
                    }))
                    mInputs.push_back(VisibilityInput{ .mRead = quest, .mValue = static_cast<double>(index) });
                return index;
            }

            ESM::RefId getTarget() const override { return ESM::RefId(); }

            int getLocalShort(int index) const override { return mShorts.at(static_cast<std::size_t>(index)); }
            int getLocalLong(int index) const override { return mLongs.at(static_cast<std::size_t>(index)); }
            float getLocalFloat(int index) const override { return mFloats.at(static_cast<std::size_t>(index)); }
            void setLocalShort(int index, int value) override { mShorts.at(static_cast<std::size_t>(index)) = value; }
            void setLocalLong(int index, int value) override { mLongs.at(static_cast<std::size_t>(index)) = value; }
            void setLocalFloat(int index, float value) override { mFloats.at(static_cast<std::size_t>(index)) = value; }

            // A message, a report: nothing the reference's standing rests on, and nothing a run may
            // show anyone.
            void messageBox(std::string_view, const std::vector<std::string>&) override {}
            void report(const std::string&) override {}

            int getGlobalShort(std::string_view name) const override { return static_cast<int>(readGlobal(name)); }
            int getGlobalLong(std::string_view name) const override { return static_cast<int>(readGlobal(name)); }
            float getGlobalFloat(std::string_view name) const override { return static_cast<float>(readGlobal(name)); }
            void setGlobalShort(std::string_view name, int value) override { writeGlobal(name, value); }
            void setGlobalLong(std::string_view name, int value) override { writeGlobal(name, value); }
            void setGlobalFloat(std::string_view name, float value) override { writeGlobal(name, value); }

            std::vector<std::string> getGlobals() const override { throw Undecided(); }
            char getGlobalType(std::string_view name) const override { return mReads.getGlobalType(name); }

            // Names and ranks for a message's substitutions; a message goes nowhere.
            std::string getActionBinding(std::string_view) const override { return {}; }
            std::string_view getActorName() const override { return {}; }
            std::string_view getNPCRace() const override { return {}; }
            std::string_view getNPCClass() const override { return {}; }
            std::string_view getNPCFaction() const override { return {}; }
            std::string_view getNPCRank() const override { return {}; }
            std::string_view getPCName() const override { return {}; }
            std::string_view getPCRace() const override { return {}; }
            std::string_view getPCClass() const override { return {}; }
            std::string_view getPCRank() const override { return {}; }
            std::string_view getPCNextRank() const override { return {}; }
            int getPCBounty() const override { return 0; }
            std::string_view getCurrentCellName() const override { return {}; }

            // Another script's variables: a state the run has no inputs for.
            int getMemberShort(ESM::RefId, std::string_view, bool) const override { throw Undecided(); }
            int getMemberLong(ESM::RefId, std::string_view, bool) const override { throw Undecided(); }
            float getMemberFloat(ESM::RefId, std::string_view, bool) const override { throw Undecided(); }
            void setMemberShort(ESM::RefId, std::string_view, int, bool) override { throw Undecided(); }
            void setMemberLong(ESM::RefId, std::string_view, int, bool) override { throw Undecided(); }
            void setMemberFloat(ESM::RefId, std::string_view, float, bool) override { throw Undecided(); }

        private:
            struct Written
            {
                std::string mName;
                double mValue;
            };

            double readGlobal(std::string_view name) const
            {
                const auto written = std::find_if(
                    mWritten.begin(), mWritten.end(), [&](const Written& global) { return global.mName == name; });
                if (written != mWritten.end())
                    return written->mValue;

                const double value = mReads.getGlobal(name);

                // Once, though every way `VisibilityRun::run` tries reads it again.
                if (std::none_of(mInputs.begin(), mInputs.end(), [&](const VisibilityInput& input) {
                        const std::string* read = std::get_if<std::string>(&input.mRead);
                        return read != nullptr && *read == name;
                    }))
                    mInputs.push_back(VisibilityInput{ .mRead = std::string(name), .mValue = value });
                return value;
            }

            void writeGlobal(std::string_view name, double value)
            {
                const auto written = std::find_if(
                    mWritten.begin(), mWritten.end(), [&](const Written& global) { return global.mName == name; });
                if (written != mWritten.end())
                    written->mValue = value;
                else
                    mWritten.push_back(Written{ .mName = std::string(name), .mValue = value });
            }

            /// What the content files say of a reference: it stands.
            bool mEnabled = true;

            unsigned mEvents = 0;
            unsigned mAsked = 0;

            std::vector<int> mShorts;
            std::vector<int> mLongs;
            std::vector<float> mFloats;
            std::vector<Written> mWritten;

            const VisibilityReads& mReads;
            std::vector<VisibilityInput>& mInputs;

            /// What this way left each name, the last word on it.
            std::vector<VisibilityNamed>& mNamed;
        };

        RunContext& contextOf(Interpreter::Runtime& runtime)
        {
            return static_cast<RunContext&>(runtime.getContext());
        }

        template <bool Enabled>
        class OpSetEnabled final : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override { contextOf(runtime).setEnabled(Enabled); }
        };

        /// `Enable` or `Disable` of a reference by name, which is the one argument.
        template <bool Enabled>
        class OpSetNamed final : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const ESM::RefId name = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                contextOf(runtime).setNamed(name, Enabled);
            }
        };

        class OpGetDisabled final : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(contextOf(runtime).isEnabled() ? 0 : 1);
            }
        };

        class OpGetJournalIndex final : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const ESM::RefId quest = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                runtime.push(contextOf(runtime).readJournal(quest));
            }
        };

        template <Event Asked>
        class OpAsk final : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override { runtime.push(contextOf(runtime).ask(Asked)); }
        };

        /// `GetSoundPlaying`, which names its sound first.
        class OpSoundPlaying final : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.pop();
                runtime.push(contextOf(runtime).ask(Event::SoundPlaying));
            }
        };

        /// An instruction that is the game's to carry out once the reference's cell is active — a
        /// sound, a global script stopped — and says nothing of whether the reference stands:
        /// only its `Arguments` are taken off the stack, as the game's own takes them.
        template <int Arguments>
        class OpNothing final : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                for (int at = 0; at < Arguments; ++at)
                    runtime.pop();
            }
        };

    }

    VisibilityRun::VisibilityRun()
    {
        Interpreter::installOpcodes(mInterpreter);
        mInterpreter.installSegment5<OpSetEnabled<true>>(Compiler::Misc::opcodeEnable);
        mInterpreter.installSegment5<OpSetEnabled<false>>(Compiler::Misc::opcodeDisable);
        mInterpreter.installSegment5<OpGetDisabled>(Compiler::Misc::opcodeGetDisabled);
        mInterpreter.installSegment5<OpGetJournalIndex>(Compiler::Dialogue::opcodeGetJournalIndex);
        mInterpreter.installSegment5<OpAsk<Event::Activated>>(Compiler::Misc::opcodeOnActivate);
        mInterpreter.installSegment5<OpAsk<Event::CellChanged>>(Compiler::Cell::opcodeCellChanged);
        mInterpreter.installSegment5<OpAsk<Event::MenuOpen>>(Compiler::Misc::opcodeMenuMode);
        mInterpreter.installSegment5<OpSoundPlaying>(Compiler::Sound::opcodeGetSoundPlaying);
        mInterpreter.installSegment5<OpNothing<1>>(Compiler::Misc::opcodeStopScript);

        mInterpreter.installSegment5<OpSetNamed<true>>(Compiler::Misc::opcodeEnableExplicit);
        mInterpreter.installSegment5<OpSetNamed<false>>(Compiler::Misc::opcodeDisableExplicit);

        // `cXX` takes the sound's name and ignores the rest; `cff` takes the volume and the pitch
        // after it.
        mInterpreter.installSegment5<OpNothing<1>>(Compiler::Sound::opcodePlaySound);
        mInterpreter.installSegment5<OpNothing<3>>(Compiler::Sound::opcodePlaySoundVP);
        mInterpreter.installSegment5<OpNothing<1>>(Compiler::Sound::opcodePlaySound3D);
        mInterpreter.installSegment5<OpNothing<3>>(Compiler::Sound::opcodePlaySound3DVP);
        mInterpreter.installSegment5<OpNothing<1>>(Compiler::Sound::opcodePlayLoopSound3D);
        mInterpreter.installSegment5<OpNothing<3>>(Compiler::Sound::opcodePlayLoopSound3DVP);
        mInterpreter.installSegment5<OpNothing<1>>(Compiler::Sound::opcodeStopSound);
    }

    Terrain::GateState VisibilityRun::run(const Interpreter::Program& program, const Compiler::Locals& locals,
        const VisibilityReads& reads, std::vector<VisibilityInput>& inputs, std::vector<VisibilityNamed>& named)
    {
        named.clear();
        // **Every way the events the script asked about could answer, and one answer from all of
        // them or none.** A script that asks whether it was activated only to show a message
        // stands the same whatever the answer; one that takes itself down when activated stands
        // by a history only its cell has. A path taken under one answer can ask an event the
        // others did not, so the ways are tried until no run asks anything new.
        constexpr unsigned ways = 1u << static_cast<unsigned>(Event::Count);
        std::uint32_t tried = 0;
        unsigned asked = 0;
        std::optional<bool> answer;
        // Kept on after the reference's own answer is lost, for the names that may still agree.
        Terrain::GateState own = Terrain::GateState::Open;
        for (unsigned before = ~0u; before != asked;)
        {
            before = asked;
            for (unsigned events = 0; events < ways; ++events)
            {
                if ((events & ~asked) != 0 || (tried & (1u << events)) != 0)
                    continue;
                tried |= 1u << events;

                mWay.clear();
                RunContext context(locals, reads, inputs, mWay, events);
                try
                {
                    mInterpreter.run(program, context);
                }
                // The interpreter answers an instruction nobody installed with an exception of its
                // own: the one kind of answer a run cannot give is a wrong one.
                catch (const Undecided&)
                {
                    named.clear();
                    return Terrain::GateState::Undecided;
                }
                catch (const std::exception&)
                {
                    named.clear();
                    return Terrain::GateState::Undecided;
                }

                asked |= context.getAsked();
                agree(named, answer.has_value());
                if (answer.has_value() && *answer != context.isEnabled())
                    own = Terrain::GateState::Undecided;
                answer = context.isEnabled();
            }
        }

        if (own == Terrain::GateState::Undecided)
            return own;
        return *answer ? Terrain::GateState::Open : Terrain::GateState::Closed;
    }

    void VisibilityRun::agree(std::vector<VisibilityNamed>& named, const bool before) const
    {
        // The first way says what each name is; every way after it must say the same of every
        // name, and a name one way left alone is a name the frame's history decides.
        if (!before)
        {
            named = mWay;
            return;
        }

        for (VisibilityNamed& was : named)
        {
            const auto now = std::find_if(
                mWay.begin(), mWay.end(), [&](const VisibilityNamed& way) { return way.mName == was.mName; });
            if (now == mWay.end() || now->mState != was.mState)
                was.mState = Terrain::GateState::Undecided;
        }

        for (const VisibilityNamed& way : mWay)
            if (std::none_of(
                    named.begin(), named.end(), [&](const VisibilityNamed& was) { return was.mName == way.mName; }))
                named.push_back(VisibilityNamed{ .mName = way.mName, .mState = Terrain::GateState::Undecided });
    }
}
