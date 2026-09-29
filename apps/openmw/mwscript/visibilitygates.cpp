#include "visibilitygates.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include <components/compiler/generator.hpp>
#include <components/compiler/opcodes.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/misc/strings/lower.hpp>

#include "../mwworld/esmstore.hpp"

namespace MWScript
{
    namespace
    {
        /// Whether a script's code enables or disables the reference it runs on. The explicit forms
        /// act on another reference, which the game resolves and tells the renderers of itself.
        bool togglesItself(const Interpreter::Program& program)
        {
            const Interpreter::Type_Code enable = Compiler::Generator::segment5(Compiler::Misc::opcodeEnable);
            const Interpreter::Type_Code disable = Compiler::Generator::segment5(Compiler::Misc::opcodeDisable);
            return std::any_of(program.mInstructions.begin(), program.mInstructions.end(),
                [&](Interpreter::Type_Code code) { return code == enable || code == disable; });
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
        mGates.clear();
        mGateOf.clear();

        std::unordered_map<ESM::RefId, std::uint32_t> gateOfScript;
        const auto gate = [&](const ESM::RefId& record, const ESM::RefId& scriptId) {
            if (scriptId.empty())
                return;

            auto known = gateOfScript.find(scriptId);
            if (known == gateOfScript.end())
            {
                std::uint32_t index = Terrain::sNoGate;
                const ESM::Script* script = store.get<ESM::Script>().search(scriptId);
                if (script != nullptr && mayToggle(script->mScriptText))
                {
                    const std::optional<GateScript> compiled = scripts.compiled(scriptId);
                    if (compiled.has_value() && togglesItself(*compiled->mProgram))
                    {
                        index = static_cast<std::uint32_t>(mGates.size());
                        Gate& made = mGates.emplace_back();
                        made.mScript = scriptId;
                        made.mProgram = *compiled->mProgram;
                        made.mLocals = *compiled->mLocals;
                    }
                }
                known = gateOfScript.emplace(scriptId, index).first;
            }

            if (known->second != Terrain::sNoGate)
                mGateOf.emplace(record, known->second);
        };

        // The record types a paging stands whose records carry a script. A static carries none.
        for (const ESM::Activator& record : store.get<ESM::Activator>())
            gate(record.mId, record.mScript);
        for (const ESM::Door& record : store.get<ESM::Door>())
            gate(record.mId, record.mScript);
        for (const ESM::Container& record : store.get<ESM::Container>())
            gate(record.mId, record.mScript);
    }

    std::uint32_t VisibilityGates::gateOf(const ESM::RefId& record) const
    {
        const auto found = mGateOf.find(record);
        return found != mGateOf.end() ? found->second : Terrain::sNoGate;
    }

    void VisibilityGates::reset()
    {
        for (Gate& gate : mGates)
        {
            gate.mState = Terrain::GateState::Unknown;
            gate.mInputs.clear();
            gate.mWatches.clear();
            gate.mTold = false;
        }
        mWatched.clear();
    }

    bool VisibilityGates::moved(const VisibilityInput& input, const VisibilityReads& reads)
    {
        if (input.mKind == VisibilityInput::Kind::Journal)
            return reads.getJournalIndex(input.mQuest) != input.mValue;

        return reads.getGlobal(input.mGlobal) != input.mValue;
    }

    void VisibilityGates::update(const VisibilityReads& reads, std::vector<GateChange>& changes)
    {
        // Each value once a frame, however many gates read it: a colony's stages read one global.
        for (Watched& watched : mWatched)
            watched.mMoved = moved(watched.mInput, reads);

        bool ran = false;
        for (std::size_t at = 0; at < mGates.size(); ++at)
        {
            Gate& gate = mGates[at];
            const bool due = gate.mState == Terrain::GateState::Unknown
                || std::any_of(gate.mWatches.begin(), gate.mWatches.end(),
                    [&](std::uint32_t watch) { return mWatched[watch].mMoved; });
            if (!due)
                continue;

            ran = true;
            gate.mInputs.clear();
            const Terrain::GateState state = mRun.run(gate.mProgram, gate.mLocals, reads, gate.mInputs);
            if (gate.mTold && state == gate.mState)
                continue;

            gate.mState = state;
            gate.mTold = true;
            changes.push_back(GateChange{ .mGate = static_cast<std::uint32_t>(at), .mState = state });
        }

        if (ran)
            rewatch();
    }

    void VisibilityGates::rewatch()
    {
        // Every gate that read a value that moved ran again this frame, so what each gate holds is
        // what its value is now, and the first holder's is as good as any.
        mWatched.clear();
        for (Gate& gate : mGates)
        {
            gate.mWatches.clear();
            for (const VisibilityInput& input : gate.mInputs)
            {
                const auto same = std::find_if(mWatched.begin(), mWatched.end(), [&](const Watched& watched) {
                    return watched.mInput.mKind == input.mKind && watched.mInput.mGlobal == input.mGlobal
                        && watched.mInput.mQuest == input.mQuest;
                });
                if (same == mWatched.end())
                {
                    gate.mWatches.push_back(static_cast<std::uint32_t>(mWatched.size()));
                    mWatched.push_back(Watched{ .mInput = input, .mMoved = false });
                }
                else
                    gate.mWatches.push_back(static_cast<std::uint32_t>(same - mWatched.begin()));
            }
        }
    }
}
