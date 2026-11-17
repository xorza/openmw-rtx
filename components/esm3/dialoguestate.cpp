#include "dialoguestate.hpp"

#include <cstdint>

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{

    void DialogueState::load(ESMReader& esm)
    {
        while (esm.isNextSub("TOPI"))
            mKnownTopics.push_back(esm.getRefId());

        while (esm.isNextSub("FACT"))
        {
            ESM::RefId faction = esm.getRefId();

            while (esm.isNextSub("REA2"))
            {
                ESM::RefId faction2 = esm.getRefId();
                std::int32_t reaction = 0;
                esm.getHNT(reaction, "INTV");
                mChangedFactionReaction[faction][faction2] = reaction;
            }
        }
    }

    void DialogueState::save(ESMWriter& esm) const
    {
        for (auto knownTopic : mKnownTopics)
        {
            esm.writeHNRefId("TOPI", knownTopic);
        }

        for (const auto& entry : mChangedFactionReaction)
        {
            esm.writeHNRefId("FACT", entry.first);

            for (const auto& [faction, reaction] : entry.second)
            {
                esm.writeHNRefId("REA2", faction);
                esm.writeHNT("INTV", reaction);
            }
        }
    }

}
