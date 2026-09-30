#include "visibilitygates.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include <components/compiler/generator.hpp>
#include <components/compiler/opcodes.hpp>
#include <components/esm/defs.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/misc/strings/lower.hpp>

#include "../mwrender/objectstorage.hpp"
#include "../mwworld/esmstore.hpp"

namespace MWScript
{
    namespace
    {
        /// What a script's code enables or disables: the reference it runs on, and every record it
        /// names.
        struct Toggled
        {
            bool mItself = false;
            std::vector<ESM::RefId> mNames;
        };

        /// **Read off the code and not off a run**, because a run takes one path and a name on
        /// another would be a name no gate decides. The compiler pushes an explicit reference as an
        /// integer literal holding the name's index among the string literals, and fetches it, just
        /// before the instruction (`Compiler::Extensions::generateInstructionCode`).
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

    std::string VisibilityGates::describe(const std::uint32_t gate) const
    {
        const Gate& described = mGates[gate];
        const std::string script = mScripts[described.mScript].mId.toDebugString();
        return described.mName.empty() ? script : script + " naming " + described.mName.toDebugString();
    }

    void VisibilityGates::reset()
    {
        for (Script& script : mScripts)
        {
            script.mInputs.clear();
            script.mWatches.clear();
            script.mRun = false;
        }
        for (Gate& gate : mGates)
        {
            gate.mState = Terrain::GateState::Unknown;
            gate.mTold = false;
        }
        mWatched.clear();
        mUnrun = true;
    }

    bool VisibilityGates::moved(const VisibilityInput& input, const VisibilityReads& reads)
    {
        if (const ESM::RefId* quest = std::get_if<ESM::RefId>(&input.mRead))
            return reads.getJournalIndex(*quest) != input.mValue;

        return reads.getGlobal(std::get<std::string>(input.mRead)) != input.mValue;
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
            watched.mMoved = moved(watched.mInput, reads);
            anyMoved = anyMoved || watched.mMoved;
        }

        // What nearly every frame is: nothing read moved, and every script has run.
        if (!anyMoved && !mUnrun)
            return;
        mUnrun = false;

        bool ran = false;
        for (Script& script : mScripts)
        {
            const bool due = !script.mRun
                || std::any_of(script.mWatches.begin(), script.mWatches.end(),
                    [&](std::uint32_t watch) { return mWatched[watch].mMoved; });
            if (!due)
                continue;

            ran = true;
            script.mRun = true;
            script.mInputs.clear();
            const Terrain::GateState own = mRun.run(script.mProgram, script.mLocals, reads, script.mInputs, mRunNamed);
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
        // is what its value is now, and the first holder's is as good as any.
        mWatched.clear();
        for (Script& script : mScripts)
        {
            script.mWatches.clear();
            for (const VisibilityInput& input : script.mInputs)
            {
                const auto same = std::find_if(mWatched.begin(), mWatched.end(),
                    [&](const Watched& watched) { return watched.mInput.mRead == input.mRead; });
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
