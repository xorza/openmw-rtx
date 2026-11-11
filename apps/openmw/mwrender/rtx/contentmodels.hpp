#pragma once

#include <string>
#include <vector>

#include <components/rtx/prepare/contentpreparation.hpp>

namespace MWWorld
{
    class ESMStore;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    /// Every model the loaded content names, each once, by its corrected path, sorted: what the loading
    /// screen prepares (`Rtx::ContentPreparation`). The models of every record a reference or an actor
    /// stands, the body parts an actor is put together from among them; beside each, the `x` model an
    /// animated object or an actor is loaded as where one stands (`correctActorModelPath`); the `_sh`
    /// scabbard or holstered shield of a weapon, an armour or a body part where one stands and the
    /// game's sheathing settings wear it; and the sky's.
    std::vector<std::string> contentModels(const MWWorld::ESMStore& store, Resource::ResourceSystem& resources);

    /// Every ground layer the loaded content names, each once by its diffuse, sorted, with the files
    /// the ground reader draws it with: the land textures and the default one, through the terrain
    /// storage's own rule, under the settings the game's terrain storage is made with.
    std::vector<Rtx::PreparationLayer> contentLayers(
        const MWWorld::ESMStore& store, Resource::ResourceSystem& resources);
}
