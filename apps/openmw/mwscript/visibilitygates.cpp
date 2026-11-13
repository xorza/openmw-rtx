#include "visibilitygates.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <components/compiler/context.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/exception.hpp>
#include <components/compiler/generator.hpp>
#include <components/compiler/opcodes.hpp>
#include <components/compiler/parser.hpp>
#include <components/compiler/scanner.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/interpreter/types.hpp>
#include <components/misc/strings/lower.hpp>

#include "../mwrender/objectstorage.hpp"
#include "../mwworld/esmstore.hpp"

namespace MWScript
{
    namespace
    {
        /// The global the game advances on every frame, `VisibilityGates::hourMoved`. Made where first asked
        /// rather than at load, because an id is interned and the table it goes in has its own
        /// order of construction.
        const ESM::RefId& gameHour()
        {
            static const ESM::RefId id = ESM::RefId::stringRefId("GameHour");
            return id;
        }

        bool isHour(const VisibilityInput& input)
        {
            return input.mSource == VisibilitySource::Global && input.mId == gameHour();
        }

        /// What a script's code enables or disables: the reference it runs on, and every record it
        /// names.
        struct Toggled
        {
            bool mItself = false;
            std::vector<ESM::RefId> mNames;
        };

        /// Read off the code and not off a run, which takes one path and would miss a name on another. The
        /// compiler pushes an explicit reference as an integer literal holding the name's index among the
        /// string literals, and fetches it, just before the instruction
        /// (`Compiler::Extensions::generateInstructionCode`).
        Toggled toggledBy(const Interpreter::Program& program)
        {
            using Compiler::Generator::segment5;
            const Interpreter::Type_Code enable = segment5(Compiler::Misc::opcodeEnable);
            const Interpreter::Type_Code disable = segment5(Compiler::Misc::opcodeDisable);
            const Interpreter::Type_Code enableNamed = segment5(Compiler::Misc::opcodeEnableExplicit);
            const Interpreter::Type_Code disableNamed = segment5(Compiler::Misc::opcodeDisableExplicit);
            const Interpreter::Type_Code fetchInteger = segment5(4);

            Toggled toggled;
            const std::vector<Interpreter::Type_Code>& code = program.mInstructions;
            for (std::size_t at = 0; at < code.size(); ++at)
            {
                if (code[at] == enable || code[at] == disable)
                    toggled.mItself = true;
                if ((code[at] != enableNamed && code[at] != disableNamed) || at < 2 || code[at - 1] != fetchInteger)
                    continue;

                // Segment 0, opcode 0: push the integer literal whose index is the low 24 bits.
                if ((code[at - 2] >> 24) != 0)
                    continue;
                const std::size_t integer = code[at - 2] & 0xffffff;
                if (integer >= program.mIntegers.size())
                    continue;
                const auto string = static_cast<std::size_t>(program.mIntegers[integer]);
                if (string >= program.mStrings.size())
                    continue;

                const ESM::RefId name = ESM::RefId::stringRefId(program.mStrings[string]);
                if (std::find(toggled.mNames.begin(), toggled.mNames.end(), name) == toggled.mNames.end())
                    toggled.mNames.push_back(name);
            }

            return toggled;
        }

        /// One `set id.local to …` of some script or dialogue result: the id and the local, each in
        /// lower case as the compiler compares them.
        struct MemberWrite
        {
            ESM::RefId mId;
            std::string mLocal;
        };

        /// The member writes of a script's text, read off the compiler's own tokens: the scanner knows a
        /// string, a comment and a keyword, and a member write is the four tokens `set`, a name, `.` and a
        /// name however the text spaces or quotes them. Not off the code, because a dialogue result compiles
        /// only against its speaker's locals (`CSVWorld::ScriptHighlighter` reads scripts the same way).
        class MemberWrites final : public Compiler::Parser
        {
        public:
            MemberWrites(
                Compiler::ErrorHandler& errors, const Compiler::Context& context, std::vector<MemberWrite>& found)
                : Compiler::Parser(errors, context)
                , mFound(found)
            {
            }

            bool parseInt(int, const Compiler::TokenLoc&, Compiler::Scanner&) override { return other(); }
            bool parseFloat(float, const Compiler::TokenLoc&, Compiler::Scanner&) override { return other(); }
            bool parseComment(const std::string&, const Compiler::TokenLoc&, Compiler::Scanner&) override
            {
                return true;
            }
            void parseEOF(Compiler::Scanner&) override {}

            bool parseKeyword(int keyword, const Compiler::TokenLoc&, Compiler::Scanner&) override
            {
                mState = keyword == Compiler::Scanner::K_set ? State::Set : State::Line;
                return true;
            }

            bool parseName(const std::string& name, const Compiler::TokenLoc&, Compiler::Scanner&) override
            {
                if (mState == State::Set)
                {
                    mId = name;
                    mState = State::Id;
                }
                else if (mState == State::Member)
                {
                    mFound.push_back(MemberWrite{
                        .mId = ESM::RefId::stringRefId(mId), .mLocal = Misc::StringUtils::lowerCase(name) });
                    mState = State::Line;
                }
                else
                    mState = State::Line;
                return true;
            }

            bool parseSpecial(int code, const Compiler::TokenLoc&, Compiler::Scanner&) override
            {
                if (code == Compiler::Scanner::S_newline)
                    mState = State::Line;
                else
                    mState = code == Compiler::Scanner::S_member && mState == State::Id ? State::Member : State::Line;
                return true;
            }

        private:
            enum class State
            {
                Line,
                Set,
                Id,
                Member,
            };

            bool other()
            {
                mState = State::Line;
                return true;
            }

            State mState = State::Line;
            std::string mId;
            std::vector<MemberWrite>& mFound;
        };

        /// A compiler context the scanner asks nothing of: `MemberWrites` reads tokens and parses no
        /// expression.
        class NoContext final : public Compiler::Context
        {
        public:
            bool canDeclareLocals() const override { return false; }
            char getGlobalType(const std::string&) const override { return ' '; }
            std::pair<char, bool> getMemberType(const std::string&, const ESM::RefId&) const override
            {
                return { ' ', false };
            }
            bool isId(const ESM::RefId&) const override { return false; }
        };

        /// What a script's text says that the scanner refuses is not a member write it could find:
        /// the text compiles nowhere, and the game reports it where it runs.
        class Unheard final : public Compiler::ErrorHandler
        {
            void report(const std::string&, const Compiler::TokenLoc&, Type) override {}
            void report(const std::string&, Type) override {}
        };

        /// Appends the member writes of `text` to `found`. A text with no `.` holds none, and is not
        /// scanned: most dialogue results set nothing of anyone's.
        void findMemberWrites(std::string_view text, std::vector<MemberWrite>& found)
        {
            if (text.find('.') == std::string_view::npos)
                return;

            Unheard errors;
            const NoContext context;
            MemberWrites writes(errors, context, found);
            std::istringstream input{ std::string(text) };
            Compiler::Scanner scanner(errors, input);
            try
            {
                scanner.scan(writes);
            }
            catch (const Compiler::SourceException&)
            {
                // The writes before the fault are found; content is untrusted, and the rest of the
                // text is what the game refuses to compile.
            }
        }

        /// Every number in `program` that an hour can be: `Script::mHourLimits`.
        std::vector<double> hourLimitsOf(const Interpreter::Program& program)
        {
            std::vector<double> limits;
            for (const Interpreter::Type_Integer value : program.mIntegers)
                if (value >= 0 && value <= 24)
                    limits.push_back(static_cast<double>(value));
            for (const Interpreter::Type_Float value : program.mFloats)
                if (value >= 0.0f && value <= 24.0f)
                    limits.push_back(static_cast<double>(value));
            std::sort(limits.begin(), limits.end());
            limits.erase(std::unique(limits.begin(), limits.end()), limits.end());
            return limits;
        }

        /// Whether the text can hold either instruction at all, so a script that cannot is never
        /// compiled here: the game compiles a script the first time it runs, and most never do.
        bool mayToggle(std::string_view text)
        {
            const std::string lower = Misc::StringUtils::lowerCase(text);
            return lower.find("enable") != std::string::npos || lower.find("disable") != std::string::npos;
        }
    }

    void VisibilityGates::build(const MWWorld::ESMStore& store, GateScripts& scripts)
    {
        mScripts.clear();
        mGates.clear();
        mWornBy.clear();
        mNamedBy.clear();
        mWatched.clear();
        mHourReaders.clear();
        mUnrun = true;

        std::unordered_map<ESM::RefId, std::uint32_t> known;
        const auto wear = [&](const ESM::RefId& record, const ESM::RefId& scriptId) {
            if (scriptId.empty())
                return;

            auto found = known.find(scriptId);
            if (found == known.end())
            {
                std::uint32_t index = Terrain::sNoGate;
                const ESM::Script* text = store.get<ESM::Script>().search(scriptId);
                const std::optional<GateScript> compiled
                    = text != nullptr && mayToggle(text->mScriptText) ? scripts.compiled(scriptId) : std::nullopt;
                if (compiled.has_value())
                {
                    Toggled toggled = toggledBy(*compiled->mProgram);
                    std::erase_if(toggled.mNames, [&](const ESM::RefId& name) {
                        return !MWRender::ObjectStorage::handsOver(store.findStatic(name));
                    });
                    if (toggled.mItself || !toggled.mNames.empty())
                    {
                        index = static_cast<std::uint32_t>(mScripts.size());
                        Script& script = mScripts.emplace_back();
                        script.mId = scriptId;
                        script.mProgram = *compiled->mProgram;
                        script.mLocals = *compiled->mLocals;
                        script.mHourLimits = hourLimitsOf(script.mProgram);

                        if (toggled.mItself)
                        {
                            script.mOwnGate = static_cast<std::uint32_t>(mGates.size());
                            mGates.push_back(Gate{
                                .mScript = index, .mName = {}, .mState = Terrain::GateState::Unknown, .mTold = false });
                        }
                        script.mFirstNamed = static_cast<std::uint32_t>(mGates.size());
                        script.mNamedCount = static_cast<std::uint32_t>(toggled.mNames.size());
                        for (const ESM::RefId& name : toggled.mNames)
                        {
                            mNamedBy.push_back(
                                NamedBy{ .mName = name, .mGate = static_cast<std::uint32_t>(mGates.size()) });
                            mGates.push_back(Gate{ .mScript = index,
                                .mName = name,
                                .mState = Terrain::GateState::Unknown,
                                .mTold = false });
                        }
                    }
                }
                found = known.emplace(scriptId, index).first;
            }

            if (found->second != Terrain::sNoGate)
                mWornBy.emplace(record, Worn{ .mScript = found->second, .mOwnGate = mScripts[found->second].mOwnGate });
        };

        // The record types `MWRender::ObjectStorage::handsOver` names that carry a script. A static
        // carries none.
        for (const ESM::Activator& record : store.get<ESM::Activator>())
            wear(record.mId, record.mScript);
        for (const ESM::Door& record : store.get<ESM::Door>())
            wear(record.mId, record.mScript);
        for (const ESM::Container& record : store.get<ESM::Container>())
            wear(record.mId, record.mScript);
        for (const ESM::Light& record : store.get<ESM::Light>())
            wear(record.mId, record.mScript);

        std::sort(mNamedBy.begin(), mNamedBy.end(),
            [](const NamedBy& left, const NamedBy& right) { return left.mName < right.mName; });

        // **What another script or a dialogue result writes of a gate script's locals.** Lua writes
        // them too (`getLocalScript(obj).variables`), which no text in the content files shows.
        std::vector<MemberWrite> writes;
        for (const ESM::Script& script : store.get<ESM::Script>())
            findMemberWrites(script.mScriptText, writes);
        for (const ESM::Dialogue& dialogue : store.get<ESM::Dialogue>())
            for (const ESM::DialInfo& info : dialogue.mInfo)
                findMemberWrites(info.mResultScript, writes);
        for (const MemberWrite& write : writes)
        {
            const auto worn = mWornBy.find(write.mId);
            if (worn == mWornBy.end())
                continue;

            Script& script = mScripts[worn->second.mScript];
            const char type = script.mLocals.getType(write.mLocal);
            if (type == ' ')
                continue;
            const VisibilityLocal local{ .mType = type, .mIndex = script.mLocals.getIndex(write.mLocal) };
            if (std::find(script.mWrittenOutside.begin(), script.mWrittenOutside.end(), local)
                == script.mWrittenOutside.end())
                script.mWrittenOutside.push_back(local);
        }
    }

    void VisibilityGates::mark(const std::span<Terrain::PagedCellRef> cell) const
    {
        for (Terrain::PagedCellRef& ref : cell)
        {
            const auto worn = mWornBy.find(ref.mRefId);
            ref.mGate = worn != mWornBy.end() ? worn->second.mOwnGate : Terrain::sNoGate;
        }

        for (Terrain::PagedCellRef& ref : cell)
        {
            // A reference its own script decides is decided: the two run on one frame, and its own
            // runs every frame after.
            if (ref.mGate != Terrain::sNoGate)
                continue;

            const auto first = std::lower_bound(mNamedBy.begin(), mNamedBy.end(), ref.mRefId,
                [](const NamedBy& named, const ESM::RefId& name) { return named.mName < name; });
            if (first == mNamedBy.end() || first->mName != ref.mRefId)
                continue;

            const auto sameRecord = [&](const Terrain::PagedCellRef& other) { return other.mRefId == ref.mRefId; };
            if (std::count_if(cell.begin(), cell.end(), sameRecord) != 1)
                continue;

            std::uint32_t gate = Terrain::sNoGate;
            std::size_t namers = 0;
            for (auto named = first; named != mNamedBy.end() && named->mName == ref.mRefId; ++named)
            {
                const auto wears = [&](const Terrain::PagedCellRef& other) {
                    const auto worn = mWornBy.find(other.mRefId);
                    return worn != mWornBy.end() && worn->second.mScript == mGates[named->mGate].mScript;
                };
                if (std::any_of(cell.begin(), cell.end(), wears))
                {
                    gate = named->mGate;
                    ++namers;
                }
            }

            // Two scripts in the cell that name it take turns at it in an order the game keeps.
            if (namers == 1)
                ref.mGate = gate;
        }
    }

    std::string VisibilityGates::describe(const std::span<const std::uint32_t> gates) const
    {
        std::string out;
        for (std::size_t at = 0; at < gates.size(); ++at)
        {
            const std::uint32_t script = mGates[gates[at]].mScript;
            const auto sameScript = [&](const std::uint32_t gate) { return mGates[gate].mScript == script; };
            if (std::any_of(gates.begin(), gates.begin() + at, sameScript))
                continue;

            out += std::format("{}{} (", out.empty() ? "" : "; ", mScripts[script].mId.toDebugString());
            bool first = true;
            for (const std::uint32_t gate : gates | std::views::drop(at) | std::views::filter(sameScript))
            {
                const Gate& described = mGates[gate];
                out += first ? "" : ", ";
                out += described.mName.empty() ? "its own" : described.mName.toDebugString();
                first = false;
            }
            out += ')';
        }
        return out;
    }

    void VisibilityGates::reset()
    {
        for (Script& script : mScripts)
        {
            script.mInputs.clear();
            script.mWatches.clear();
            script.mHourRead.reset();
            script.mRun = false;
        }
        for (Gate& gate : mGates)
        {
            gate.mState = Terrain::GateState::Unknown;
            gate.mTold = false;
        }
        mWatched.clear();
        mHourReaders.clear();
        mUnrun = true;
    }

    bool VisibilityGates::moved(const Watched& watched, const VisibilityReads& reads)
    {
        const VisibilityInput& input = watched.mInput;
        if (input.mSource == VisibilitySource::Journal)
            return reads.getJournalIndex(input.mId) != input.mValue;
        return reads.getGlobal(input.mId.getRefIdString()) != input.mValue;
    }

    bool VisibilityGates::hourMoved(const Script& script, const double read, const double now)
    {
        if (read == now)
            return false;
        if (std::floor(read) != std::floor(now))
            return true;

        // Both ends closed: a test `>` changes past a limit the last run stood on, and `>=` on the
        // frame the hour reaches it.
        const auto [low, high] = std::minmax(read, now);
        const auto first = std::lower_bound(script.mHourLimits.begin(), script.mHourLimits.end(), low);
        return first != script.mHourLimits.end() && *first <= high;
    }

    void VisibilityGates::settle(
        const std::uint32_t gate, const Terrain::GateState state, std::vector<GateChange>& changes)
    {
        Gate& settled = mGates[gate];
        if (settled.mTold && settled.mState == state)
            return;

        settled.mState = state;
        settled.mTold = true;
        changes.push_back(GateChange{ .mGate = gate, .mState = state });
    }

    void VisibilityGates::update(const VisibilityReads& reads, std::vector<GateChange>& changes)
    {
        // Each value once a frame, however many scripts read it: a colony's stages read one global.
        bool anyMoved = false;
        for (Watched& watched : mWatched)
        {
            watched.mMoved = moved(watched, reads);
            anyMoved = anyMoved || watched.mMoved;
        }

        // The hour, by each script that reads it: each has its own limits.
        const double hour = mHourReaders.empty() ? 0.0 : reads.getGlobal(gameHour().getRefIdString());
        const auto hourDue = [&](const Script& script) {
            return script.mHourRead.has_value() && hourMoved(script, *script.mHourRead, hour);
        };
        if (!anyMoved)
            anyMoved = std::any_of(mHourReaders.begin(), mHourReaders.end(),
                [&](std::uint32_t reader) { return hourDue(mScripts[reader]); });

        // What nearly every frame is: nothing read moved, and every script has run.
        if (!anyMoved && !mUnrun)
            return;
        mUnrun = false;

        bool ran = false;
        for (Script& script : mScripts)
        {
            const bool due = !script.mRun
                || std::any_of(script.mWatches.begin(), script.mWatches.end(),
                    [&](std::uint32_t watch) { return mWatched[watch].mMoved; })
                || hourDue(script);
            if (!due)
                continue;

            ran = true;
            script.mRun = true;
            script.mInputs.clear();
            const Terrain::GateState own = mRun.run(VisibilityScript{ .mProgram = &script.mProgram,
                                                        .mLocals = &script.mLocals,
                                                        .mWrittenOutside = script.mWrittenOutside },
                reads, script.mInputs, mRunNamed);
            if (script.mOwnGate != Terrain::sNoGate)
                settle(script.mOwnGate, own, changes);

            // A name the run did not leave the same on every path, or a run that could not finish,
            // is the game's to answer.
            for (std::uint32_t gate = script.mFirstNamed; gate < script.mFirstNamed + script.mNamedCount; ++gate)
            {
                const ESM::RefId& name = mGates[gate].mName;
                const auto left = std::find_if(
                    mRunNamed.begin(), mRunNamed.end(), [&](const VisibilityNamed& run) { return run.mName == name; });
                settle(gate, left != mRunNamed.end() ? left->mState : Terrain::GateState::Undecided, changes);
            }
        }

        if (ran)
            rewatch();
    }

    void VisibilityGates::rewatch()
    {
        // Every script that read a value that moved ran again this frame, so what each script holds
        // is what its value is now, and the first holder's is as good as any. Not the hour, which
        // each script holds as its own last run read it (`hourMoved`).
        mWatched.clear();
        mHourReaders.clear();
        for (std::uint32_t index = 0; index < mScripts.size(); ++index)
        {
            Script& script = mScripts[index];
            script.mWatches.clear();
            script.mHourRead.reset();
            for (const VisibilityInput& input : script.mInputs)
            {
                if (isHour(input))
                {
                    script.mHourRead = input.mValue;
                    mHourReaders.push_back(index);
                    continue;
                }

                const auto same = std::find_if(mWatched.begin(), mWatched.end(),
                    [&](const Watched& watched) { return watched.mInput.reads(input); });
                if (same == mWatched.end())
                {
                    script.mWatches.push_back(static_cast<std::uint32_t>(mWatched.size()));
                    mWatched.push_back(Watched{ .mInput = input, .mMoved = false });
                }
                else
                    script.mWatches.push_back(static_cast<std::uint32_t>(same - mWatched.begin()));
            }
        }
    }
}
